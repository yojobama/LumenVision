# 12-hour soak on the RK3588

Question: does the service stay healthy for hours with every accelerator and pipeline loaded at once?

Answer: it stayed up and kept every pipeline at full rate for the whole 12 hours, with no restart, no API error and no throttling. It also exposed
two bugs. Both were fixed afterwards (see "Fixes" below).

1. Recording tore down its H.264 encoder at every 60 s segment. Each teardown made MPP free frames into the wrong pool, encoder buffers leaked
   (dma-buf +4.8 MB/h, 46 MB of encoder buffers left behind after the graph was removed), and nine times an encode job ran on a buffer whose
   descriptor had already been closed.
2. RGA jobs on heap memory above 4 GB failed twice when scheduled on the RGA2 core, which cannot address it.

## Setup

- Board: Orange Pi 5 Plus (RK3588, 16 GB), imagetest37 reflashed, then the dev88 package (the IPv4-only name fix); big cores on `performance`.
- Run: `profiles/harness/soak.py 12`, 2026-10-28 22:27 to 2026-10-29 10:27 UTC; a WebRTC viewer (`soak_webrtc_viewer.mjs`, headless Edge) on a PC on
  the same network. Raw data and the report are in `profiles/soak-12h/`.
- Graph, built from the API, next to the user's own Vulkan AprilTag sink on the Lenovo camera:

| Node | Source | Accelerator |
|---|---|---|
| stereo camera 2560x720@60 MJPEG, split into halves | USB | JPEG decoder |
| CPU AprilTag (libapriltag) | left half | CPU |
| Vulkan AprilTag (vkapriltag, ultrafast refine) | right half | GPU |
| RKNN detector, YOLO11n COCO int8 | left half | NPU |
| RKNN detector, apriltagV4 | Lenovo 1280x720@30 | NPU, RGA letterbox |
| H.264 recordings, 60 s segments, 8 GB cap each | whole stereo frame; Lenovo | H.264 encoder |
| MJPEG previews polled at 10 fps | CPU AprilTag output; apriltagV4 output | JPEG encoder |
| WebRTC preview, watched for 12 h | Vulkan AprilTag output | H.264 encoder, RGA colour conversion |
| snapshot every 60 s (taken and deleted) | Lenovo | JPEG encoder |

## What held

- Service: 715 minutes, 0 restarts, 0 API errors (slowest API response 22 ms), threads flat at 70.
- Rates, every quarter of the run: stereo decode 60.4 fps, Lenovo 30.2, Vulkan AprilTag 60.4, apriltagV4 30.2, YOLO11n 45-50. The CPU AprilTag
  ran 59 fps at night and 28 fps in daylight (its detect time follows the scene: 9 ms on a dark, empty frame, 36 ms on a busy one).
- Previews: 837,209 MJPEG frames polled, none bad or empty; 718 of 718 snapshots; the WebRTC viewer was connected for all 723 minutes it ran, with
  0 packets lost and 0 freezes (its 2 reconnects were the soak restarting its graph at the start and removing it at the end).
- Thermal: peak 73 C (big cores) in the warm afternoon load of 2.5 cores, every clock at its maximum throughout (A76 2352 MHz, GPU and NPU 1000 MHz,
  memory 2112 MHz). Stage times rose with the daytime scene (Vulkan detect 2.4 -> 4.8 ms, stereo decode 2.2 -> 3.0 ms), not with time.
- Recording: 16.5 GB on disk at the end, held by the folder caps; 434 files.

## What did not

| Finding | Evidence | Cause |
|---|---|---|
| dma-buf grew 4.8 MB/h (119 -> 177 MB), dma-buf fds 77 -> 110 | per-minute samples; after the graph was removed, 18 buffers attached to `rkvenc-core` (16 x 2.3 MB, 2 x 4.6 MB, 46 MB) were still allocated with no encoder left | encoder teardown at each segment (below) |
| 9 bursts of failed encode jobs (02:19, 03:47, 05:11, 05:38, 05:41, 06:45, 08:59, 09:51, 09:54) | kernel: `dma_buf_get fd N failed(-9)` / `fd -1 failed`, `alloc_task failed`; MPP: `Assertion fd >= 0 failed at mpp_buffer_get_fd` | encode jobs on buffers already freed; more frequent as the run went on |
| MPP pool corruption at every stereo segment rotation | 555 x `mpp_mem_pool_put invalid mem pool ptr ... caller mpp_frame_deinit` in 7 h, two per minute | tearing down an `h264_rkmpp` encoder |
| 2 RGA job failures (22:51, 23:02) | `RGA_MMU unsupported memory larger than 4G`, `scheduler core[4] unsupported mm_flag` | an RGA2 job on heap memory above 4 GB; that frame fell back to the CPU |
| service RSS +5 MB/h (722 -> 778 MB) | per-minute samples | not traced; small, re-measured after the fixes |

Each failed-encode burst lost a few frames of one recording; no file was lost and nothing restarted. The H.264 encoder was the only one affected:
the JPEG decoder and encoder, the NPU and the GPU logged nothing.

## Fixes

- `RecordSink` keeps one encoder for the whole recording (`d97e4be`). A due segment requests a keyframe; the first keyframe packet from that frame
  on starts the next file, each file's timestamps offset to start at 0. The encoder is drained and destroyed only when the recording stops or the
  frame size changes. Test: `segments rotate on a keyframe from one running encoder` (1 s segments; every file decodes from a frame at time 0, and
  the files hold exactly the frames the encoder was given).
- RGA jobs are kept on the two RGA3 cores (`RgaCores.h`, `e4344ff`).

## Verification of the fixes

The same graph for 2 hours on dev90 (both fixes), 2026-10-29 10:48 to 12:48, with the WebRTC viewer. Raw data in `profiles/soak-2h-fixed/`.

| | 12 h, before | 2 h, after |
|---|---|---|
| H.264 encoder set-ups | ~125 an hour | 3 in the run (one per encoder) |
| MPP `invalid mem pool` frees | 2 a minute while running | none while running; 2 per encoder when the graph was removed |
| encode jobs on closed or invalid buffers | 9 bursts, the first after 3.8 h | none |
| RGA failures (above 4 GB) | 2, after 25 and 35 min | none |
| buffers left after the graph was removed | 18 H.264-encoder buffers (46 MB) and 2 JPEG-encoder buffers (12 MB) | none; only the remaining camera's decoder buffers |
| dma-buf in use | 119 -> 177 MB, still rising | 119 -> 132 MB in the first 80 min (one more JPEG-decoder buffer for the 60 fps stereo stream), then flat |
| restarts, API errors | 0, 0 | 0, 0 |

- Segments still rotate on time (every 60 s) and each file stands alone: a rotated Lenovo segment probes as H.264 1280x720 starting at 0.000 s on a
  keyframe, 60.07 s and 1802 frames, and decodes with no error in ffmpeg.
- The NPU detectors' letterbox stays on RGA (`rknn-rga` 2.0 ms a frame) and the WebRTC conversion on RGA (2.8 ms), so restricting jobs to the
  RGA3 cores costs no fallback to the CPU.
- MPP still frees into the wrong pool when an `h264_rkmpp` encoder is destroyed: it now happens once per recording or preview stop instead of
  every minute. That remains an MPP/ffmpeg-rockchip defect to report upstream.
- The service's resident memory rose 15 MB in the first hour and 3 MB in the last half hour, the same warm-up the 12 h run showed (722 -> 778 MB
  over 12 h, mostly early); worth another long run on this build before calling it flat.
- The WebRTC viewer saw 1 freeze (12:05, no packet loss, a normal frame count that minute) against none in 12 h before; not conclusive at one
  event.
