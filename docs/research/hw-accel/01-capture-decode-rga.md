# 01 - Capture, decode and RGA (front of the pipeline)

Research notes for offloading LumenVision's per-frame front end (capture -> decode -> unpack/transform)
to RK3588 hardware (MPP/VPU, RGA2/RGA3, DMA-BUF). Read-only investigation; no code changed.

## Status

- [x] Read README / ROADMAP / PERFORMANCE_ANALYSIS
- [x] Read capture code (V4l2CameraBackend, CameraSource)
- [x] Read decode code (MppJpegDecoder)
- [x] Read Frame / FramePool / PixelUnpack / FrameTransform / RoiSource
- [x] Read RgaColorConverter / WebRTCSink usage
- [x] Web research: MPP caching (F1), MPP jpegd output formats (F2b), multi-core (F2c), RGA (F3), MIPI (F7), UVC (F8)
- [x] Rejected ideas, open questions, ranked recommendations (bottom of file). DONE 2026-10-24.
- Finding numbering is by topic, not order in the file (F1, F2, F2b, F2c, F7, F6, F8, F9, F10, F11, F3, F4).
- Note: no RK3588 MJPEG-decode-time figure exists publicly (searched); must be measured on the board.

## What is already implemented (verified in code, 2026-10-24, branch feature/native-api)

- `V4l2CameraBackend` (LumenCore/V4l2CameraBackend.cpp): raw V4L2, `V4L2_MEMORY_MMAP`, 4 buffers (L33), poll + DQBUF,
  drain-to-newest loop (L265-278), timestamps from `buf.timestamp` (L285-295). **Does a `VIDIOC_G_FMT` ioctl every frame**
  (L297-300) to learn the fourcc/size. No `VIDIOC_EXPBUF` (no DMA-BUF export) anywhere.
- MJPEG branch (L324-350): `FramePool::Acquire` a CPU buffer (BGR or GRAY), then `MppJpegDecoder::Decode`, falling back to
  `cv::imdecode`. `preferGray` comes from `!HasActiveColorFrameConsumer()` (CameraSource.cpp:129) - so as soon as any colour
  consumer (preview, YOLO, MJPEG sink) is bound, the whole frame goes NV12 -> BGR on the CPU on the capture thread.
- `MppJpegDecoder` (LumenCore/MppJpegDecoder.cpp): advanced interface (`decode_put_packet` + `decode_get_frame`), one
  context per camera (member of the backend, lazily initialised and reused - good). Per frame:
  1. `mpp_buffer_get` input buffer from an internal group + **memcpy of the JPEG** from the V4L2 mmap (L122-129);
  2. `mpp_buffer_get` output buffer of `align16(w)*align16(h)*4` bytes (L142-149) from an internal DMA_HEAP>DRM>ION group
     created with **no cache flag** (L21-31, `MPP_BUFFER_INTERNAL`, no `MPP_BUFFER_FLAGS_CACHABLE`);
  3. synchronous put/get (decoder idle while the CPU converts; no pipelining);
  4. CPU reads the MPP output through `mpp_buffer_get_ptr`: grey = `yView.copyTo(dst)` (L178), colour =
     `cv::cvtColor(NV12->BGR)` (L183). **No `mpp_buffer_sync_begin/end`**, and the output never leaves MPP as a DMA-BUF;
     every frame is copied into a malloc'd FramePool block.
  5. Only `MPP_FMT_YUV420SP` accepted; 4:2:2 JPEGs (most UVC webcams send YUV 4:2:2 MJPEG!) fall back to software - see F2.
- Measured (docs/PERFORMANCE_ANALYSIS.md:152): 720p MJPEG camera, "hardware JPEG decode 3.9 ms" (this is `decodeUs`, which
  includes memcpy-in, HW decode, and the CPU NV12->BGR/Y-copy-out; YOLO sink was bound so the colour path ran). Camera
  age 15-16 ms, "the rest is the USB transfer".
- `Frame` (LumenCore/Frame.h/.cpp): cv::Mat + format tag, lazy cached `AsGray()` (BGR/RGB->grey cvtColor, NV12 Y-plane
  copy) / `AsBgr()` (RGB/GRAY only - **no NV12->BGR path**, so nothing upstream can hand over NV12 today). No DMA-BUF fd,
  no plane/stride metadata beyond cv::Mat.
- `FramePool` (LumenCore/FramePool.cpp): page-aligned `aligned_alloc` blocks, never freed (so vkapriltag host import
  stays valid). Plain cached anonymous memory; not DMA-heap, not importable by RGA/MPP as an fd.
- `FrameTransform::Apply` (LumenCore/FrameTransform.cpp:31-57): crop = `clone()`, rotate = `cv::rotate`, flip = `cv::flip`,
  each a separate full CPU pass with a fresh (unpooled) allocation; runs on the capture thread (CameraSource.cpp:140-145).
- `RoiSource` (LumenCore/RoiSource.cpp:37): zero-copy `Mat` ROI; vkapriltag then repacks non-continuous frames with a CPU
  copy (VkApriltagBackend.cpp:205-213).
- `PixelUnpack` (LumenCore/PixelUnpack.cpp): scalar per-pixel loops for Y10/Y16/Y10P/Y10BPACK -> 8-bit (no NEON, no
  OpenCV vectorised op); only used for raw mono MIPI/USB sensors (OV9281 etc.).
- RGA: only `RgaColorConverter::ConvertBgrToNv12` (WebRTCSink). Uses `wrapbuffer_virtualaddr_t` on a cv::Mat and a
  std::vector scratch, then two `av_image_copy_plane` memcpys into the AVFrame (RgaColorConverter.cpp:23-35). The resize
  for the preview divisor is still `cv::resize(INTER_AREA)` on the CPU (WebRTCSink.cpp:272) before RGA. `LUMEN_WITH_RGA`
  defaults OFF (cmake/LumenFeatures.cmake:39), ON in one preset (CMakePresets.json:164). librga = prebuilt
  airockchip/librga head, labelled 1.10.0 (scripts/install-deps.sh:775-795).
- udev rule gives the `video` group /dev/mpp_service, /dev/dma_heap/*, /dev/rga (install-deps.sh:806-810) - so DMA-heap
  allocation from userspace is already permitted for the service.
- Other CPU 2D work on frames that RGA could do (owned by other areas, listed for completeness): YOLO letterbox
  `cv::resize`+`copyMakeBorder`+`BGR2RGB` (YoloPostProcess.cpp:23-31, RknnDetectionBackend.cpp:135), MJPEG sink resize
  (MjpegSink.cpp:72), annotate copies (ApriltagDetector.cpp:391-395, ObjectDetectionSink.cpp:53-58), stereo `cv::remap`
  (StereoDepthNode.cpp:229-306 - RGA cannot remap; Vulkan/GPU territory).

## Findings

### F1. MPP decode output lands in UNCACHED (write-combined) memory, then the CPU reads all of it - likely most of the "3.9 ms"

- Claim: `MppJpegDecoder` creates its groups with plain `MPP_BUFFER_TYPE_DMA_HEAP` (no flags). In MPP's dma-heap allocator,
  flags index a heap table and **flags==0 selects `"system-uncached"`**; `MPP_ALLOC_FLAG_CACHABLE` selects `"system"`.
  On the Rockchip BSP kernel the `system-uncached` heap maps userspace with `pgprot_writecombine` (ARM Normal
  Non-Cacheable) and skips CPU cache maintenance. So `yView.copyTo()` / `cvtColor(NV12->BGR)` in MppJpegDecoder.cpp:175-184
  read every byte of the decoded frame with **no data cache and no hardware prefetch benefit**. Non-cacheable reads on
  Cortex-A cores typically run at a fraction (often 5-20x lower) of cached memcpy bandwidth (general ARM behaviour - not
  measured on this board yet; see test below). At 720p that is 0.92 MB (grey) or 1.38 MB (colour) of uncached reads; at
  1080p 2.1/3.1 MB.
- Evidence:
  - rockchip-linux/mpp `osal/allocator/allocator_dma_heap.c`: `info = &heap_infos[flags];` index 0 = `"system-uncached"`,
    index with CACHABLE = `"system"`; `try_flip_flag()` falls back if a heap is missing.
    https://github.com/rockchip-linux/mpp/blob/develop/osal/allocator/allocator_dma_heap.c
  - rockchip-linux/mpp `inc/mpp_buffer.h`: `MPP_BUFFER_FLAGS_CACHABLE 0x00020000`, `MPP_BUFFER_FLAGS_DMA32 0x00200000`,
    and `mpp_buffer_sync_begin/end` (+ `_ro_` and `partial` variants) for cache maintenance.
    https://github.com/rockchip-linux/mpp/blob/develop/inc/mpp_buffer.h
  - rockchip-linux/kernel develop-5.10 `drivers/dma-buf/heaps/system_heap.c`: `system-uncached` heap,
    `if (buffer->uncached) vma->vm_page_prot = pgprot_writecombine(...)`; begin/end_cpu_access skip the sgtable sync for
    uncached buffers. https://github.com/rockchip-linux/kernel/blob/develop-5.10/drivers/dma-buf/heaps/system_heap.c
  - nyanmisaka/ffmpeg-rockchip `libavcodec/rkmppdec.c` (the de-facto reference MPP integration) deliberately sets
    `MPP_BUFFER_FLAGS_CACHABLE` on the decoder frame pool, and creates the MJPEG input ("misc") group as
    `MPP_BUFFER_TYPE_DRM | MPP_BUFFER_FLAGS_DMA32 | MPP_BUFFER_FLAGS_CACHABLE`.
    https://github.com/nyanmisaka/ffmpeg-rockchip/blob/master/libavcodec/rkmppdec.c
  - Magnitude reference (other arm64 SoC, Librem 5 / Cortex-A53): copying from normal memory ~2 ms/MB vs from (uncached)
    DMA-BUFs ~20 ms/MB, i.e. 10x; kernel devs note WC reads are "typically very slow since the CPU doesn't prefetch".
    https://lkml.iu.edu/hypermail/linux/kernel/2507.1/08310.html ,
    https://lkml.iu.edu/hypermail/linux/kernel/2401.3/06257.html . If the A76 sees a similar ratio, the 1.38 MB NV12 read at
    720p costs ~1.5-3 ms uncached vs ~0.2-0.3 ms cached - consistent with the measured 3.9 ms `decode` (HW decode itself
    should be ~1 ms at 720p; unverified).
- Fix (small): create the output group with `MPP_BUFFER_TYPE_DMA_HEAP | MPP_BUFFER_FLAGS_CACHABLE` (and DMA32 for the
  input/output if the JPEG block is a 32-bit-address master - see Open questions), and bracket CPU reads with
  `mpp_buffer_sync_ro_begin(buf)` / `mpp_buffer_sync_ro_end(buf)` (invalidate before read; the decoder wrote via DMA).
  For the INPUT buffer the CPU only writes (memcpy) - cached + `mpp_buffer_sync_end` (clean) or keep uncached/WC
  (WC is fine for streaming writes). Keep the CreateInternalGroup fallback chain but OR the flag into every type.
- Confidence: high that the buffers are uncached on the BSP kernel (code-level evidence); medium on the size of the gain
  (needs the measurement below). Cache invalidate of 1-3 MB costs ~0.1-0.3 ms on A76 (rough), still far below an
  uncached read pass.
- Estimated gain: decode stage 3.9 ms -> ~1.5-2.5 ms at 720p colour; larger at 1080p (rough estimate, verify).
- Effort/risk: ~10 lines; low risk (MPP falls back to another heap if `system` is missing). Must add the sync calls or
  risk reading stale lines (cache invalidation is mandatory for correctness once cached).
- How to test on the Orange Pi: `ls /dev/dma_heap/` (expect `system`, `system-uncached`, ...); log
  `/proc/<pid>/maps` or `cat /sys/kernel/debug/dma_buf/bufinfo` for the exporter name; A/B the `decode` stage in
  `GET /api/profile` with grey-only and colour consumers, 720p and 1080p. Micro-benchmark: time `yView.copyTo()` alone
  (StageLap around L175-184) before/after.

### F2. 4:2:2 MJPEG (most UVC webcams) silently falls back to software decode

- Claim: the decoder accepts only `MPP_FMT_YUV420SP` (MppJpegDecoder.cpp:164-165); any NV16 (4:2:2) output returns false
  and V4l2CameraBackend falls back to `cv::imdecode` with no log line. MPP's JPEG decoder supports YUV420, YUV422 and
  YUV444 JPEG sampling, producing NV12 / NV16 / NV24 (ffmpeg-rockchip maps these and lists "YUV420(2*2:1*1:1*1) |
  YUV422(2*1:1*1:1*1) | YUV444(1*1:1*1:1*1)" as the supported set). Many UVC webcams emit 4:2:2 MJPEG (yuvj422p) - camera
  dependent, not verified for the team's cameras.
- Evidence: MppJpegDecoder.cpp:164-165; ffmpeg-rockchip rkmppdec.c MJPEG format mapping (URL above).
- Fix: accept `MPP_FMT_YUV422SP` (and `YUV444SP`): for grey the Y plane is identical (just copy/hand over Y); for colour
  OpenCV has no NV16->BGR code, so either use RGA (`RK_FORMAT_YCbCr_422_SP` -> BGR888, supported by RGA2/RGA3) or a
  manual 2-row-per-chroma-row view. Also: log once which path is used (hw/sw, chroma format) so this cannot hide again.
- Confidence: high on the code behaviour; camera-dependent impact.
- Estimated gain: for a 4:2:2 camera, 12 ms (sw 1080p colour) -> ~2-4 ms (hw).
- Test: `ffprobe -f v4l2 -input_format mjpeg -video_size 1280x720 -i /dev/videoX` (pix_fmt yuvj420p vs yuvj422p), or
  log `mpp_frame_get_fmt()`; compare `decode` stage in /api/profile.

### F2b. The RK3588 JPEG decoder can itself output NV12 from 4:2:2, YUYV, or RGB888 - one `control()` call

- Claim: on RK3588 MPP routes MJPEG to the dedicated "RK jpeg decoder" (`rkjpegd`, `VPU_CLIENT_JPEG_DEC`, HAL
  `hal_jpegd_rkv`), ahead of VDPU2. That HAL has a built-in post-processor: with `MPP_DEC_SET_OUTPUT_FORMAT` it converts
  any JPEG sampling to `MPP_FMT_YUV420SP` (NV12), `MPP_FMT_YUV422_YUYV`, or - **RK3588-specific - `MPP_FMT_RGB888` and
  RGB555** (with full/limited range chosen from the frame's colour range). Without the control, `set_output_fmt_flag = 0`
  and the output is the JPEG's native layout (NV16 for 4:2:2), which LumenVision then rejects (F2). Hardware downscale
  registers (1/2, 1/4, 1/8) exist but are not exposed (`//TODO support scale and tile output`).
- Evidence (rockchip-linux/mpp develop):
  - `osal/mpp_soc.c` rk3588 entry: `{ &vdpu38x, &rkjpegd, &vdpu2, &vdpu2_jpeg_pp_fix, ... }`, comment "4 - RK jpeg decoder";
    `mpp/hal/mpp_hal.c:256-262`: MJPEG tries `VPU_CLIENT_JPEG_DEC` first, then VDPU2/VDPU1.
  - `mpp/hal/vpu/jpegd/hal_jpegd_rkv.c` `setup_output_fmt()` (YUV_OUT_FMT_2_NV12 / _2_YUYV / _2_RGB888 / _2_RGB565) and
    `hal_jpegd_rkv_control()`: "soc_type >= ROCKCHIP_SOC_RK3588 ... only rgb565be and rgb888 supported".
    https://github.com/rockchip-linux/mpp/blob/develop/mpp/hal/vpu/jpegd/hal_jpegd_rkv.c
- Use in LumenVision:
  1. Always call `api->control(ctx, MPP_DEC_SET_OUTPUT_FORMAT, &fmt)` with `MPP_FMT_YUV420SP` in `EnsureInitialized()`:
     fixes F2 in hardware (4:2:2 -> NV12 in the decoder, zero CPU), Y plane = grey for free.
  2. For colour consumers, a *second* option is `MPP_FMT_RGB888` straight out of the VPU, deleting the CPU
     `cvtColor(NV12->BGR)` (MppJpegDecoder.cpp:183). Caveats: byte order is RGB (need `FrameFormat::RGB24`, which `Frame`
     already supports: AsGray RGB2GRAY, AsBgr RGB2BGR - YOLO wants RGB anyway, RknnDetectionBackend.cpp:135 currently
     converts BGR->RGB); output is 3 B/px instead of 1.5 (more DRAM traffic, more bytes to read if buffers stay uncached);
     the grey consumer then needs RGB2GRAY (~0.3 ms) unless NV12 is kept. Best overall is still "keep NV12, convert
     lazily with RGA" (F6), but RGB888 output is the smallest code change that removes the CPU colour conversion.
  3. The output format is per-context state, so switching grey/colour at runtime = re-issue the control (or keep NV12).
- Confidence: high for code paths; byte order and PP speed must be verified on the board (MPP is the stock RK3588
  build; the board's MPP version must be recent enough - RK3588 rkjpegd RGB support check exists in develop; check the
  installed version, install-deps pins `MPP_REF=1.1.0`).
- Estimated gain: removes the CPU colour conversion (~1-3 ms at 720p, ~3-6 ms at 1080p from uncached memory; ~1-2 ms
  cached) from the capture thread; fixes the 4:2:2 software fallback (~8-12 ms at 1080p).
- Test: `mpi_dec_test -t 8 -i frame.jpg -w 1280 -h 720 -f <fmt>` style runs (or a scratch build of MppJpegDecoder) with
  each output format; dump one frame and check colours; compare `decode` stage in /api/profile.

### F2c. Multi-camera: the JPEG decoder is one shared core; VDPU2 can be selected as a second JPEG decoder

- Each `V4l2CameraBackend` owns its own MPP context (good - no cross-thread sharing), but all contexts land on the single
  `rkjpegd` core, serialised by the kernel mpp_service. MPP's dec cfg has `base:hw_type`; when > 0, `mpp_hal_init` uses
  that client (`mpp/hal/mpp_hal.c:221-229`), so a second/third camera could be put on `VPU_CLIENT_VDPU2` (the Hantro-style
  JPEG path, `vdpu2_jpeg_pp_fix`) for real parallelism. Unverified: VDPU2 JPEG speed and its output-format support on
  RK3588 (VDPU2 PP is RK3399-era; may cap at 4096 px and be slower).
- Relevance: at 3 cameras x 720p30 = 90 decodes/s; if one decode is ~2 ms, one core is ~18 % busy - only worth doing if
  the measured HW time per frame is high or the team goes to 1080p60/multi-camera. Mostly a latency (queueing) tool.
- Test: run 2-3 cameras, compare per-camera `decode` stage vs one camera; then set `base:hw_type` for one context.
- Confidence: medium (code path verified; behaviour on board untested).

(F2b note: ffmpeg-rockchip maps `AV_PIX_FMT_RGB24 <-> MPP_FMT_RGB888` for the *encoder* (libavcodec/rkmppenc.c:57), so
`MPP_FMT_RGB888` should be R,G,B in memory, but the jpegd PP's actual byte order is unverified - dump a frame.)

### F7. MIPI-CSI cameras: the V4L2 backend cannot open RK3588 ISP/CIF nodes (multi-planar only); this is the real zero-copy/low-latency path

- Claim: the Rockchip ISP capture nodes register only `V4L2_CAP_VIDEO_CAPTURE_MPLANE` (rkisp `capture.c`:
  `vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_STREAMING`, `vidioc_s_fmt_vid_cap_mplane`, and
  `vidioc_expbuf = vb2_ioctl_expbuf`). `V4l2CameraBackend::Open` requires `V4L2_CAP_VIDEO_CAPTURE` (V4l2CameraBackend.cpp:127-129)
  and uses `V4L2_BUF_TYPE_VIDEO_CAPTURE` / `fmt.pix` throughout, so on an Orange Pi the ISP (`rkisp_mainpath`,
  `rkisp_selfpath`) - and very likely the rkcif raw nodes too (unverified; rkcif's `out_fmts[]` entries all carry
  `.mplanes = 1`) - fail Open and fall back to OpenCV's backend (CameraSource.cpp:13-20). The code comments name
  Arducam MIPI OV9281/OV9782 modules (V4l2CameraBackend.cpp:568-570), so this matters for the planned global-shutter cameras.
  Evidence: https://github.com/rockchip-linux/kernel/blob/develop-5.10/drivers/media/platform/rockchip/isp/capture.c ,
  https://github.com/rockchip-linux/kernel/blob/develop-5.10/drivers/media/platform/rockchip/cif/capture.c
- Why it matters for performance: MIPI removes (a) the in-camera JPEG encode, (b) the USB transfer (the measured ~11-12 ms of
  the 15-16 ms camera age at 720p - PERFORMANCE_ANALYSIS.md:152,173), (c) the JPEG decode. The ISP/CIF writes NV12 / GREY /
  Y10 directly into IOMMU-mapped vb2 buffers that can be exported with `VIDIOC_EXPBUF` and imported by RGA
  (`importbuffer_fd`), MPP (encode) and Vulkan (`ImportDmaBufFrame`, vkapriltag already supports it) - true zero-copy.
  For a mono global-shutter sensor (OV9281) via rkcif the output is GREY/Y10 with no ISP/rkaiq needed; for colour Bayer
  sensors the ISP needs Rockchip's `rkaiq` 3A server (camera_engine_rkaiq) + a tuning IQ file for the sensor, which is the
  main integration cost.
- Fix: add MPLANE support to V4l2CameraBackend (`V4L2_CAP_VIDEO_CAPTURE_MPLANE`, `v4l2_pix_format_mplane`,
  `v4l2_plane` array in QBUF/DQBUF/QUERYBUF, `bytesperline` per plane); then EXPBUF (F6).
- Confidence: high for rkisp (code quoted), medium-high for rkcif (verify `v4l2-ctl -d /dev/videoN --info` shows
  "Video Capture Multiplanar").
- Estimated gain: camera age 15-16 ms -> roughly exposure + readout (~1 frame period of sensor readout at most, often
  3-8 ms at 120 fps OV9281) + ~0 decode. This is the single biggest front-end latency lever, but needs hardware.
- Test: `v4l2-ctl --list-devices`, `media-ctl -p -d /dev/mediaX`, `v4l2-ctl -d /dev/videoN --info --list-formats-ext`;
  `v4l2-ctl --stream-mmap --stream-count=300` for fps; then the same camera in LumenVision (check which backend opened it
  in the log: "V4L2 backend failed to open ... falling back to OpenCV").

### F6. Keep the decoded NV12 in its DMA-BUF and let consumers pull what they need (the zero-copy front end)

- Today every MJPEG frame is: V4L2 mmap -> memcpy -> MPP input buffer -> VPU -> MPP output (DMA heap) -> CPU copy/convert ->
  malloc FramePool block -> (vkapriltag) CPU->GPU upload / (RGA) virtual-address import / (YOLO) CPU letterbox.
- Proposal: a `Frame` variant that owns a ref-counted MPP output buffer (`mpp_buffer_inc_ref` + put in the deleter; the
  group is fixed-size via `mpp_buffer_group_limit_config`, e.g. 6-8 buffers so sinks holding a frame never starve the
  decoder), tagged `FrameFormat::NV12`, carrying `fd` (`mpp_buffer_get_fd`), size, hor/ver stride, and a cached CPU mapping.
  - `AsGray()` = a `cv::Mat` view of the Y plane (no copy; `hor_stride == width` for 640/800/1280/1920 widths, which are
    16-aligned, so the view is continuous and vkapriltag will not repack).
  - vkapriltag: `GpuDetector::ImportDmaBufFrame(key, fd, bytes)` is already in the pinned library (GpuDetector.h:199-206;
    "reads an NV12 buffer's Y plane as a packed 8-bit frame") and the Mali exposes `VK_EXT_external_memory_dma_buf`
    (PERFORMANCE_ANALYSIS.md:133-139). Key the import cache on the MppBuffer (finite group => bounded import count,
    like `kMaxImportedFrames` for host import). Removes the upload (0.12-0.17 ms) and the CPU Y copy.
  - `AsBgr()` / `AsRgb()` = RGA `importbuffer_fd(nv12Fd)` -> `imcvtcolor` into a pooled DMA-heap BGR buffer (RGA3 cores
    take it; NV12 needs 16-px stride alignment on RGA3, satisfied by MPP's align-16). Lazy: paid only if a colour consumer
    asks, and off the CPU.
  - Downstream (other areas, enabled by this): YOLO letterbox straight from NV12 with one RGA `improcess` (scale + CSC +
    placement in a pre-filled padded RGB buffer; RKNN also accepts NV12 input), WebRTC passing the fd as
    `AV_PIX_FMT_DRM_PRIME` to `h264_rkmpp`, record sink likewise.
- Cache policy: if the frame is mostly consumed by devices (GPU/RGA/VPU) an uncached (WC) buffer is cheapest and the CPU
  should only do sparse reads (vkapriltag's tag decode samples bits) - but anything that streams the whole frame on the CPU
  (calibration, CPU AprilTag backend, cv::imencode) must then go through a cached copy or a cached mapping + `DMA_BUF_IOCTL_SYNC`
  (`mpp_buffer_sync_ro_begin/end`). Recommendation: cached heap + explicit sync (F1), since the CPU AprilTag backend and
  annotate-copies read whole frames; sync cost is a cache-maintenance walk (~0.1-0.3 ms per MB on A76 - rough, verify).
- Lifetime hazards: frames are shared across sink threads (SourceResult copies); the MppBuffer ref must live in
  `Frame::Storage` (like `matPoolOwner`), never be recycled under a reader; buffer-group exhaustion must fall back to the
  copy path rather than block the capture thread.
- Confidence: medium-high (all APIs exist and are in use elsewhere: ffmpeg-rockchip exports MPP buffers as DRM PRIME; this
  repo already has the Vulkan import). The exact gain needs the measurements in F1.
- Estimated gain at 720p (vs today, colour consumer bound): capture thread decode stage 3.9 ms -> ~1-1.5 ms (HW decode only;
  unverified HW time), vkapriltag -0.15 ms upload, AsGray 0.3 ms -> 0, YOLO letterbox 0.7 ms CPU -> RGA, preview BGR->NV12
  copy chain gone. Roughly 3-4 ms less CPU per frame per camera on the A76s and ~2-3 ms less camera->detector latency.
  More at 1080p (x2.25 pixels).
- Effort/risk: medium-large (Frame API + ownership + every consumer's fallback). Do F1/F2b first (small, most of the gain
  on the capture thread), then this.
- Test: StageProfile already records `decode`, `gray`, `vk-gpu:upload` - compare before/after; `cat /sys/kernel/debug/dma_buf/bufinfo`
  for buffer counts/leaks; run for hours watching RSS and the MPP group (`mpp_buffer_group_debug`).

### F8. UVC "camera age" (~11-12 ms after exposure at 720p) is mostly the USB frame transfer - a camera/mode choice, not an accelerator problem

- Evidence: PERFORMANCE_ANALYSIS.md:152,173 (exposure 2 ms; "the 12 ms before decode is the USB transfer"). uvcvideo picks the
  smallest isochronous alt setting that satisfies the camera's `dwMaxPayloadTransferSize` (kernel patch discussion,
  https://patchew.org/linux/20250324022120.216923-1-ccc194101@163.com ); a camera that budgets bandwidth for its nominal
  frame rate spreads each frame over most of the frame interval. High-speed isochronous max is 3 x 1024 B per 125 us
  microframe = 24.6 MB/s, so a ~100 KB 720p MJPEG frame needs >= ~4 ms even at full bandwidth.
- Options (none needs code beyond mode selection):
  1. Select a higher-fps mode of the same resolution (60/100/120 fps) - the camera usually reserves a larger alt setting, so each
     frame arrives sooner; the existing drain-to-newest (V4l2CameraBackend.cpp:265-278) discards the extra frames cheaply
     (only the newest is decoded). Hypothesis, unverified - measure camera age per mode.
  2. USB 3 UVC cameras sending YUYV/NV12 uncompressed: no in-camera JPEG encode, no decode, transfer at 5 Gb/s.
  3. MIPI CSI (F7).
- Also: uvcvideo copies URB payloads into the vb2 buffer on the CPU in a kworker (`uvc_video_copy_data_work`); that work is
  unpinned and competes with the A76 workload - check with `top -H` / `perf sched`.
- Test: `cat /sys/kernel/debug/usb/devices` (or `lsusb -v -d <vid:pid>`) for the active `wMaxPacketSize`/alt setting per mode;
  camera `age` vs `decode` in /api/profile per mode; `v4l2-ctl --stream-mmap` with `--verbose` timestamps.
- Confidence: medium (mechanism is well established; per-camera behaviour varies).

### F9. FrameTransform (crop / rotate / flip) is 1-3 full CPU passes with fresh allocations on the capture thread

- FrameTransform.cpp:37-55: crop = `clone()` (not pooled), each of rotate and flip allocates a new Mat and does a full pass.
  A 180-degree-mounted 1080p BGR camera pays `cv::rotate` (~1-2 ms) and any crop pays an extra copy. RGA does crop +
  rotate(90/180/270) + mirror (+ scale + CSC) in **one** pass (`improcess` with `IM_HAL_TRANSFORM_ROT_*`/`FLIP_*` usage),
  and can do it straight from the decoded NV12 DMA-BUF (F6), producing the transformed NV12 for every consumer.
- Cheaper interim fixes without RGA: (a) for 180 deg, `cv::flip(src, dst, -1)` == ROTATE_180 in one pass - combine rotate and
  flip into a single `cv::flip`/`cv::transpose`+`flip` call (the composition of rotations and flips is always one of 8 dihedral
  ops = at most transpose + one flip); (b) write into `FramePool` buffers instead of fresh Mats; (c) for grey-only pipelines
  apply the transform to the 1-byte grey image, not BGR (3x fewer bytes) - already true when preferGray.
- RGA constraints to respect: RGA3 min width 68, NV12 16-px stride, even offsets/sizes; rotation 90/270 output size swaps.
  Rotating NV12 crops require even x/y.
- Gain: 1-3 ms CPU at 1080p BGR per frame when a transform is configured; 0 otherwise. Effort small (dihedral collapse) /
  medium (RGA). Confidence: high on the code; RGA rotate timing unmeasured (rotation is generally slower than straight
  copy on RGA due to memory access pattern).

### F10. YUYV / NV12 / raw-mono capture paths

- YUYV (V4l2CameraBackend.cpp:351-363): grey = `COLOR_YUV2GRAY_YUYV` (a byte de-interleave - memory bound, ~0.3-0.5 ms at
  720p on an A76, cheap); colour = `COLOR_YUV2BGR_YUYV` on the CPU. vkapriltag can read YUYV luma directly from a dma-buf
  (`ImportDmaBufFrame(..., yuyv=true)`, GpuDetector.h:204-206), and RGA3/RGA2 take YUYV input for CSC/scale. With
  `VIDIOC_EXPBUF` on the uvcvideo vb2 buffers (vb2-vmalloc supports DMABUF export/import - uvc_queue.c sets
  `io_modes |= VB2_DMABUF`) the YUYV frame could go straight to the GPU. Caveat: vmalloc memory is scattered and cached; the
  Mali/RGA importers handle sg-lists via IOMMU, but cache sync costs apply, and RGA2 cannot touch >4 GiB pages. Low priority
  unless YUYV cameras are actually used (USB 2 YUYV caps at ~720p10/ 480p30, so mostly irrelevant for UVC; relevant for
  USB 3 cameras).
- NV12 (L372-381): converts to BGR on the CPU unconditionally - **even when `preferGray` is set** (no grey branch). Trivial
  fix: when preferGray, copy/view the Y plane (or publish `FrameFormat::NV12`, which `Frame::AsGray()` already handles at
  Frame.cpp:69-73). Gain: ~2-4 ms CPU at 1080p for NV12 cameras (rare on UVC; relevant for ISP/MIPI paths once F7 exists).
- Raw mono (PixelUnpack.cpp): scalar loops. Y10 -> 8 bit is `(v & 0x3FF) >> 2` per pixel; Y16 picks the high byte; Y10P drops
  every 5th byte. All are memory-bound byte shuffles that NEON (vld2/vld4/vshrn) or OpenCV's vectorised ops
  (`cv::extractChannel` on a CV_8UC2 view for Y16; `convertScaleAbs`/`Mat::convertTo(CV_8U, 1/4)` on a CV_16U view for Y10)
  run several times faster. Y10P/Y10BPACK with `(x/4)*5 + x%4` indexing defeats auto-vectorisation. At 1280x800 (OV9281)
  this is ~1 M px: scalar ~1-2 ms vs NEON ~0.2-0.3 ms (estimate). RGA does not take 10-bit mono (its 10-bit formats are
  YUV SP), so this stays on the CPU (or GPU). Note rkcif can often deliver 8-bit GREY directly from a 10-bit sensor if the
  sensor driver offers an 8-bit mode (OV9281 supports RAW8) - prefer that mode.
- Confidence: high on code; NEON gains are estimates.

### F11. Small per-frame waste in the capture path (cheap fixes)

- `VIDIOC_G_FMT` ioctl every frame (V4l2CameraBackend.cpp:297-300) - the format only changes in SetMode; cache it at
  StartStreaming. ~5-20 us per frame; trivial.
- MPP output buffer sized `align16(w)*align16(h)*4` (MppJpegDecoder.cpp:142-144) = 2.7x what NV12 needs; harmless for speed
  but with RGB888 output (F2b) the size must be >= 3*w*h anyway; with the F6 design (frames held by sinks) a tighter size
  saves DMA memory (6 x 1080p x 4 = 50 MB vs 19 MB).
- JPEG input memcpy (L129) is 50-300 KB per frame (~0.02-0.1 ms) - not worth V4L2 DMABUF import into uvcvideo (rejected
  below).
- `decodeUs` conflates VPU time and CPU copy-out; split it into `decode:hw` and `decode:copy` StageProfile entries so F1/F2b
  gains are visible. (Instrumentation, no speed change.)

### F3. RGA facts that constrain any offload design (RK3588: 2x RGA3 + 1x RGA2-Enhance)

From the librga developer guide and FAQ (primary Rockchip docs):
https://github.com/airockchip/librga/blob/main/docs/Rockchip_Developer_Guide_RGA_EN.md ,
https://github.com/airockchip/librga/blob/main/docs/Rockchip_FAQ_RGA_EN.md

| | RGA3 (x2 cores) | RGA2-Enhance (x1 core) |
|---|---|---|
| src range | 68x2 .. 8176x8176 | 2x2 .. 8192x8192 |
| dst range | 68x2 .. 8128x8128 | 2x2 .. 4096x4096 |
| scale | 1/8..8 (up: bicubic, down: average) | 1/16..16 (bilinear/bicubic) |
| rotate 90/180/270, mirror, crop | yes | yes |
| YUYV/UYVY 4:2:2 packed | in + out | in + out |
| NV12/NV16 (YCbCr 420/422 SP) | in + out | in + out |
| Y400 (grey) | not listed | in + out |
| colour fill, planar YUV (I420) | no | yes |
| pixels/clock | 3 bypass, 2 scale | 2 |
| IOMMU | 40-bit | **32-bit: buffers must be below 4 GiB** |
| width-stride alignment | BGR888 16 px, NV12/NV16 16 px, YUYV 8 px, RGBA 4 px | BGR888 4, NV12 4, YUYV 2 |

- YUV offsets/sizes must be even. The doc says that when several cores might be used, design to the strictest alignment.
- **4 GiB rule:** on the multi-core driver (RK3588) RGA2 jobs on memory above 4 GiB *fail* ("RGA2 only support under 4G
  memory!", "job assign failed") - there is no swiotlb bounce. The Orange Pi 5 / 5 Plus / 5 Max ship with 4-32 GB, so a
  malloc'd cv::Mat may well live above 4 GiB. Anything that needs RGA2 (grey/Y400 output, I420, colour fill, width < 68) must
  use a DMA32 allocation (`/dev/dma_heap/system-dma32` / `system-uncached-dma32`, MPP `MPP_BUFFER_FLAGS_DMA32`).
- Buffer-type cost (FAQ A1.1): physical ~1.1-1.2x theoretical, **dma_fd 1.3-1.5x**, **virtual address 1.8-2.1x and
  "greatly affected by CPU"** - the driver builds page tables on the CPU and force-syncs the cache every frame for
  virtual addresses. Rockchip recommends uncached dma-bufs for RGA-only data (cached costs cache sync; RK3566 1080p
  example 4.18 ms uncached vs 5.5 ms cached).
- Throughput model (FAQ): time = w*h / (pixels per clock * clock). At 1080p a copy-class op on RGA3 is ~0.6-1.7 ms
  depending on the RGA clock (300-800 MHz; RK3588's actual RGA clock not verified here). Two RGA3 cores + RGA2 can run
  concurrently, so three camera streams can be serviced in parallel.
- Async: `imbeginJob`/`imendJob`/`improcessTask` (librga >= 1.9.0, multi_rga driver >= 1.2.25) batch several ops in one
  submission; `IM_ASYNC` + release fence fd lets the CPU continue; `acquire_fence_fd` chains after another device.
  `imconfig(IM_CONFIG_SCHEDULER_CORE, ...)` pins a thread's jobs to a core (Rockchip advises against it in products).
  `imcheck()` validates parameters (debug use).
- Real-world throughput data points (no vendor per-op table exists for RK3588):
  - Mainline RGA3 driver series (tested on Radxa Rock 5T): "Around 80 fps ... scaling and converting from RGBA 480x360 to
    NV12 3840x2160 in a single gstreamer pipeline" => ~660 Mpx/s output on one RGA3 core => roughly **1.4 ms per 720p, 3.1 ms
    per 1080p output frame** (pipeline-throughput number, single core). https://lwn.net/Articles/1056189/
  - gstreamer-rga: "production-tested for 6 x 1080p30 pipelines on RK3588", "RGA3 core0 and core1 are 88 % occupied"
    (decode-convert-scale branches; no per-op timing). https://github.com/corenel/gstreamer-rga
  - Implication: RGA is **not faster in wall-clock than OpenCV's multi-threaded NEON cvtColor on 4 A76 cores** for a cached
    720p frame (~1 ms); its value is that it frees the A76s (which PERFORMANCE_ANALYSIS shows are saturated by vkapriltag CPU
    tail, RKNN post-processing, encoders) and that it can read uncached/device memory at full speed. Use it for CPU
    offload and for fused ops (crop+rotate+scale+CSC in one pass), not for single-op latency.
- RGA has no remap/undistort and no arbitrary-angle warp, so it cannot replace `cv::remap` (stereo rectification).
- Confidence: high (vendor docs). The RK3588 RGA clock and real CSC timings need measuring (`rgaImDemo --querystring`,
  or time a loop of `imcvtcolor`).

### F4. The existing RGA use (WebRTCSink) uses the slowest buffer mode and adds two memcpys

- Claim: `RgaColorConverter::ConvertBgrToNv12` wraps a malloc'd `cv::Mat` and a `std::vector` scratch with
  `wrapbuffer_virtualaddr_t` (RgaColorConverter.cpp:23-28) - the 1.8-2.1x virtual-address mode with per-frame page-table
  building and cache maintenance - then copies Y and UV into the AVFrame with two `av_image_copy_plane` calls
  (L34-35). Since the source is malloc memory on a >4 GiB board, only the RGA3 cores can take the job (fine for
  BGR888->NV12, but it explains any sporadic "RGA failed, falling back to sws_scale"). The preview downscale is a
  separate CPU `cv::resize(INTER_AREA)` (WebRTCSink.cpp:272) although RGA can scale and convert in a single `improcess`.
- Owner: area 04 (encode/stream) owns WebRTCSink; listed here because the fix is the same shared infrastructure as F5
  (DMA-heap-backed frames + `importbuffer_fd`). Gain: roughly halves the RGA call time and removes ~1.5 MB (720p) / 3 MB
  (1080p) of memcpy per preview frame; folding the resize in removes a CPU resize pass.
- Confidence: high on the code; gain magnitude from Rockchip's ratios, unmeasured here.

(F7 addendum: the RK3588 ISP has two output paths per camera (mainpath + selfpath) that scale/crop/CSC in hardware, so one
MIPI camera can produce full-res NV12/grey for AprilTag and a downscaled stream for YOLO/preview at zero CPU and zero RGA
cost. Relation to the roadmap: F6/F7 are ROADMAP.md B1 ("export DMA-BUF fds"), B2 (refcounted multi-format Frame) and C3
(RGA); B1's raw-V4L2 half is done, the DMA-BUF half is not.)

## Ideas rejected (and why)

- **V4L2 `V4L2_MEMORY_DMABUF` import into uvcvideo to skip the JPEG memcpy into the MPP input buffer.** uvcvideo supports it
  (vb2-vmalloc, `io_modes |= VB2_DMABUF`), but the copy is 50-300 KB (~0.02-0.1 ms) and the kernel would vmap the dma-buf
  and CPU-write it (cache-maintenance questions for a cached heap). Not worth the complexity.
- **Async/pipelined MPP decode (put packet N+1 while the CPU handles N).** Decode is a few ms against a 33 ms frame interval;
  overlapping only adds throughput, and holding one frame back adds latency. Each camera already decodes on its own capture
  thread, so cameras are parallel. Better to remove the CPU work after the decode (F1/F2b/F6).
- **RGA for YUYV->grey or NV12->grey.** Grey is a byte de-interleave or simply the Y plane (free with F6). RGA3 has no
  Y400 output; RGA2 does but needs <4 GiB DMA32 buffers; the per-job ioctl/scheduling overhead is comparable to the CPU cost.
- **RGA for raw mono unpack (Y10/Y16/Y10P) and for undistort/remap.** Unsupported formats/operations (F3); NEON / Vulkan instead.
- **MPP JPEG hardware downscale (1/2, 1/4, 1/8) for preview/YOLO.** The registers exist in hal_jpegd_rkv.c but the control
  path is `//TODO support scale` - not reachable through the public API.
- **Vulkan compute for NV12->BGR / letterbox.** Feasible, but the Mali is already the busiest stage for vkapriltag (GPU 8.7 ms
  at 720p decimation 1, PERFORMANCE_ANALYSIS.md:153); RGA is otherwise idle - put 2D work there.
- **Using the ISP for UVC cameras.** The RK ISP takes input from MIPI (or raw Bayer from memory); it cannot process MJPEG/YUYV
  from USB.
- **Further libjpeg-turbo tuning.** Superseded by the MPP path; the software path is only a fallback now.

## Open questions

1. Real `rkjpegd` time per frame at 720p/1080p (no public figure found). Needs the `decode:hw` / `decode:copy` split (F11) or
   `mpi_dec_test` on the board.
2. Which `/dev/dma_heap/*` heaps the deployed kernel exposes (`system`, `system-uncached`, `*-dma32`, `cma`)? If `system-uncached`
   is missing, MPP's `try_flip_flag()` may already fall back to cached `system` and F1 changes nothing on that image - check first.
3. Does `rkjpegd` need DMA32 buffers on 16/32 GB boards? ffmpeg-rockchip allocates the MJPEG bitstream group with
   `MPP_BUFFER_FLAGS_DMA32`; LumenVision does not and works on the test board - may be luck of allocation placement.
4. Chroma subsampling of the team's actual MJPEG cameras (4:2:0 vs 4:2:2) - decides whether F2 is a live bug.
5. Byte order and colour range of `rkjpegd` `MPP_FMT_RGB888` output.
6. RK3588 RGA3 clock and real `imcvtcolor`/`improcess` latency with fd buffers (`rgaImDemo`, `/sys/kernel/debug/rkrga/load`).
7. Whether rkcif (raw sensor) video nodes are multi-planar like rkisp (`v4l2-ctl --info`).
8. Is the uvcvideo copy kworker landing on the A76s and competing with the pinned threads?

## Ranked recommendations (front of pipeline)

| # | Change | Where | Effort / risk | Expected gain (RK3588, per camera) | Confidence |
|---|---|---|---|---|---|
| 1 | Allocate MPP output (and input) groups **cacheable** (`MPP_BUFFER_FLAGS_CACHABLE`) and bracket CPU reads with `mpp_buffer_sync_ro_begin/end` (F1) | MppJpegDecoder.cpp:21-31, 175-184 | XS / low | `decode` 3.9 ms -> ~1.5-2.5 ms at 720p colour, more at 1080p | med-high (verify heap list first) |
| 2 | Split `decode` into `decode:hw` and `decode:copy`; cache the format instead of `VIDIOC_G_FMT` per frame (F11) | V4l2CameraBackend.cpp:297-300, 340-350 | XS / none | measurement for 1/3/6; ~10 us | high |
| 3 | `MPP_DEC_SET_OUTPUT_FORMAT = MPP_FMT_YUV420SP` at init (4:2:2 JPEGs -> NV12 in hardware) + log hw/sw path and chroma once (F2, F2b) | MppJpegDecoder.cpp:73-110, 164-165 | XS / low | 4:2:2 cameras: ~8-12 ms (1080p sw) -> HW time | high |
| 4 | NV12 capture with `preferGray`: publish Y plane / `FrameFormat::NV12` instead of BGR (F10) | V4l2CameraBackend.cpp:372-381 | XS / low | ~2-4 ms at 1080p for NV12 sources | high |
| 5 | Optional quick colour win: `MPP_FMT_RGB888` output when a colour consumer exists, publish `FrameFormat::RGB24` (F2b) | MppJpegDecoder, CameraSource | S / med (byte order, reconfig) | removes CPU NV12->BGR (~1-3 ms 720p) | medium |
| 6 | Collapse rotate+flip into one dihedral op, pool the outputs; later RGA `improcess` (F9) | FrameTransform.cpp:31-57 | S / low | 1-3 ms at 1080p BGR when a transform is set | high |
| 7 | **Zero-copy NV12 DMA-BUF `Frame`**: hold the MppBuffer, `AsGray()` = Y view, vkapriltag `ImportDmaBufFrame`, lazy RGA (fd) for BGR/RGB/letterbox; share a DMA-heap pool with WebRTC/RKNN (F6, F3, F4) | Frame, FramePool, MppJpegDecoder, VkApriltagBackend, RgaColorConverter | M-L / med (lifetimes) | ~3-4 ms CPU/frame at 720p (x2.25 at 1080p), ~2-3 ms latency; enables NPU/encoder zero-copy in areas 03/04 | medium |
| 8 | Multi-planar V4L2 + `VIDIOC_EXPBUF` so RK ISP/CIF (MIPI) cameras open natively; zero-copy into 7 (F7) | V4l2CameraBackend.cpp | M / med (needs MIPI hardware; rkaiq for colour) | camera age ~15 ms -> ~exposure + readout; no decode at all | high (mechanism), hw-dependent |
| 9 | Camera-mode experiment: higher-fps UVC modes / USB 3 cameras to shorten USB transfer (F8) | config only | XS / none | possibly several ms of the ~11 ms transfer | low-medium (hypothesis) |
| 10 | NEON/vectorised `PixelUnpack` for Y10/Y16/Y10P; prefer 8-bit GREY sensor modes (F10) | PixelUnpack.cpp | S / low | ~1-1.5 ms at 1280x800 for raw-mono sensors | medium |
| 11 | Second JPEG decoder (VDPU2 via `base:hw_type`) for 3+ cameras, only if contention is measured (F2c) | MppJpegDecoder.cpp:86-104 | S / med | queueing latency under multi-camera load | low-medium |
