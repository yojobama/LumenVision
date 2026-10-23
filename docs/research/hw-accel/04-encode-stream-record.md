# 04 - Encode, stream and record (output side) on RK3588

Research notes, agent 4 of 5. Read-only investigation of the output side of LumenVision's per-frame pipeline
(WebRTC sink, MJPEG sink, record sink, RGA conversion, server streaming endpoints, web UI players).

## Status

Complete (2026-10-24). All code paths read; web sources cited inline; ranked recommendations at the end. Numbers marked
"unverified" are estimates that need the stage profiler (R0) on the board.

- [x] PERFORMANCE_ANALYSIS.md, ROADMAP.md (C2/C3/C4) read
- [x] LumenCore sinks read (WebRTCSink, MjpegSink, RecordSink, RgaColorConverter, Manager encoder probe)
- [x] Server WebRTC controller, SinkManager, DB restore, MJPEG module + janitor
- [x] ISink/ISource frame-consumer gating, Frame formats, MPP decoder output
- [x] Web UI players and preview lifecycle
- [x] Web research (ffmpeg-rockchip rkmppenc + hwcontext, libdatachannel, librga FAQ, browsers, FRC bandwidth)
- [x] Ranked recommendations

## What is implemented today (verified in code)

- WebRTCSink: one FFmpeg encoder per sink shared by all viewers (`LumenCore/WebRTCSink.cpp:197-325`). Encoder name is a string
  (`libx264` or `h264_rkmpp`). Per frame: `result.frame->AsBgr()` (line 331) -> optional CPU `cv::resize(INTER_AREA)` (line 272) ->
  RGA `imcvtcolor` BGR->NV12 into a malloc'd scratch buffer then two `av_image_copy_plane` memcpys into the AVFrame
  (`RgaColorConverter.cpp:12-37`), fallback `sws_scale` -> `avcodec_send_frame` -> each packet copied into an `rtc::binary`
  and `sendFrame` to every open peer. Settings: `bit_rate`, `gop_size = 2*fps`, `max_b_frames = 0`, and x264-only privopts
  `preset=ultrafast`, `tune=zerolatency` (silently ignored by h264_rkmpp). No `rc_mode`, `profile`, `level`, `qp_*` set.
- MjpegSink: CPU `cv::imencode(".jpg")` on every frame (`MjpegSink.cpp:61-81`), optional CPU resize, then base64 of the
  whole JPEG, stored as a string for the HTTP thread to poll.
- RecordSink: FFmpeg encoder (name from config; server defaults to preferred = h264_rkmpp if present), always CPU `sws_scale`
  BGR24->YUV420P (`RecordSink.cpp:154,258-260`, no RGA), `preset=medium` (x264 only), MP4 via libavformat, segment
  every 300 s by default; encoder is destroyed and recreated at each segment (`RecordSink.cpp:244-246`); a JSONL sidecar line is
  written and `flush()`ed for every frame (`RecordSink.cpp:274-284`).
- Encoder probe: `Manager::GetPreferredWebRTCEncoder()` returns `h264_rkmpp` if `avcodec_find_encoder_by_name` finds it
  (`Manager.cpp:1736-1742`); FFmpeg is nyanmisaka/ffmpeg-rockchip built with `--enable-rkmpp --enable-rkrga --enable-libdrm`
  (`scripts/install-deps.sh:828-901`).

## Findings

### F1. Web UI forces libx264 for every preview it creates (software encode on the A76s) - HIGH
- Claim: `ApiService.createWebRTCSink(name, bitrateKbps = 4000, fps = 30, encoderName = 'libx264')` always sends
  `encoderName=libx264` (`webui/src/services/ApiService.ts:508-509`); both callers omit the argument
  (`webui/src/hooks/useAppData.ts:132`, `webui/src/graph/Inspector.tsx:294`). The server only substitutes the preferred encoder when
  the query value is null (`Server/Controllers/sinks/WebRTCSinkController.cs:19`), so every preview created from the UI encodes
  with libx264 even on an Orange Pi with h264_rkmpp. After a service restart `RestoreWebRTCSink` recreates it with
  `GetPreferredWebRTCEncoder()` (`Server/SinkManager.cs:324`), so the behaviour differs between "just created" and "restored",
  which would explain inconsistent on-device measurements.
- Confidence: high (code path is unambiguous; not yet confirmed by a log line on the board -
  `WebRTCSink constructed, encoder=...` is logged at `WebRTCSink.cpp:25`).
- Gain: libx264 ultrafast/zerolatency 1080p30 on an A76 is roughly 15-30 ms/frame of one-to-several cores (x264 threads
  default to ~1.5x cores); h264_rkmpp is ~1-3 ms of CPU (submit/poll) with the VPU doing the work. Freeing 1-3 A76 cores
  directly reduces the contention described in PERFORMANCE_ANALYSIS.md section 6. Estimated: 25-100% of one A76 per preview.
- Effort/risk: trivial (drop the default, or send nothing so the server decides). Risk: none on x86 dev boxes (server falls back
  to libx264).
- Test: create a preview from the UI, `journalctl -u lumenvision | grep "WebRTCSink constructed"`; `top -H -p $(pidof ...)`
  look for x264 threads; `cat /sys/kernel/debug/mpp_service/load` or `/proc/mpp_service/sessions-summary` for VEPU sessions.

### F2. New viewer -> full encoder teardown and rebuild; RTP timestamps rewind for existing viewers - HIGH
- Claim: when a peer's track first opens, `EncodeAndSend` calls `ShutdownEncoder()` and rebuilds the codec context to get a
  keyframe (`WebRTCSink.cpp:284-290`). This resets `m_FrameCounter = 0` (line 247), and the RTP time passed to `sendFrame` is
  `m_FrameCounter / fps` (line 314), so every already-connected viewer sees its RTP timestamp jump backwards when a second viewer
  joins (and also on every `SetSettings`). Browsers' jitter buffers treat that as a large discontinuity (typically a freeze or a
  burst of dropped frames until resync). A rebuild of an h264_rkmpp context also costs an MPP init (tens of ms, unverified number).
- Better: keep the encoder; force an IDR by setting `frame->pict_type = AV_PICTURE_TYPE_I` (+ `AV_FRAME_FLAG_KEY` in FFmpeg 7)
  on the next frame - ffmpeg-rockchip's rkmpp encoder honours `pict_type == I` via `MPP_ENC_SET_IDR_FRAME` (to verify in
  `libavcodec/rkmppenc.c`), libx264 honours it natively. Use a monotonic timestamp source (capture time) for RTP, not a
  per-encoder counter.
- Confidence: high for the code behaviour; medium for the visible artefact in Chrome (test).
- Gain: correctness/UX for multi-viewer; removes a 1-frame stall per join.
- Test: open preview in two tabs, watch `chrome://webrtc-internals` framesDropped / freezeCount / jitterBufferDelay on tab 1
  when tab 2 joins.

### F3. Work done when nobody is watching - HIGH
- WebRTCSink: `Process` calls `AsBgr()` and `EncodeAndSend` does `cv::resize` *before* checking for open peers
  (`WebRTCSink.cpp:271-283`, 331). Encoding itself is skipped, but the resize (if scaleDivisor > 1) and any BGR materialisation
  are not.
- MjpegSink: encodes every frame whether or not any HTTP client is connected (`MjpegSink.cpp:61-81`): ~8-15 ms CPU per
  1080p frame with libjpeg-turbo at q80 plus ~1-2 ms base64 (PERFORMANCE_ANALYSIS.md section 7), i.e. 30-45% of one A76 at
  30 fps for a fallback preview that may have no viewer.
- (to check) Upstream detectors annotate + copy a BGR frame whenever a frame sink is bound (`wantsFrame`), regardless of viewers.
- Fix: a cheap `HasViewers()` per sink (WebRTC: any open track; MJPEG: last poll within N ms, set by the HTTP module) and gate
  both the sink work and the upstream `wantsFrame` on it.
- Confidence: high.
- **Upstream cost (verified):** the frame-consumer registry's `isActive` is just `alive && GetToggleStatus()`
  (`LumenCore/ISink.cpp:132-135`), not "has a viewer". So a toggled-on WebRTC preview sink with zero browsers keeps
  `HasActiveFrameConsumer()`/`HasActiveColorFrameConsumer()` true, which makes (a) the camera decode to colour instead of grey
  (`CameraSource.cpp:129`), (b) ApriltagDetector/ObjectDetectionSink copy + annotate a BGR frame every frame
  (`ApriltagDetector.cpp:385-598`, `ObjectDetectionSink.cpp:51-105`; ~3-4 ms at 1080p per PERFORMANCE_ANALYSIS.md section 2).
- **The UI leaves preview sinks running after the viewer stops watching (verified):** `useAppData.stopStream` only removes the id
  from a client-side Set (`webui/src/hooks/useAppData.ts:98-105`); `WebRTCStream.stopStream` only closes the RTCPeerConnection
  ("No server-side stop endpoint exists", `WebRTCStream.tsx:116-121`). The sink stays toggled on until someone toggles it off or the
  service restarts (at boot WebRTC sinks are restored but deliberately *not* enabled, `Server/DB.cs:205`). So in a session where
  someone previewed a node in the pits and then closed the tab or pressed Stop, that node keeps paying colour decode + annotate +
  BGR materialisation every frame through the match. (The Inspector's toggle, `Inspector.tsx:291-292`, does toggle the sink off.)
- Fix options (cheapest first): make WebRTCSink's isActive callback return "has an open track" (it already tracks
  peers; needs a virtual `ISink::WantsFrames()` used in the RegisterFrameConsumer lambda); same for MjpegSink via a "last
  fetched" timestamp updated by `GetLatestJpegBase64`. RecordSink stays "toggled on = active". `RequestFrameOnce()` already exists
  for snapshot behaviour so the first frame after a viewer joins is still produced.
- Gain: per previewed node, ~3-4 ms detector critical path (annotate copy), colour vs grey decode (MPP: NV12->BGR CPU conversion
  ~2-3 ms at 1080p, to check), plus whatever AsBgr costs in the sink thread. On a match day this is the largest output-side win
  because it is paid with zero viewers.

### F4. RGA used for colour conversion only, via virtual addresses, with two extra memcpys - MEDIUM
- `RgaColorConverter` wraps the BGR Mat and a malloc'd scratch buffer with `wrapbuffer_virtualaddr_t` (`RgaColorConverter.cpp:23-28`),
  then copies Y and UV planes into the AVFrame (lines 34-35). Scaling is done separately on the CPU with `cv::resize` (INTER_AREA)
  (`WebRTCSink.cpp:272`). Virtual-address RGA jobs make the kernel driver map/pin user pages (and on RGA2 with >4 GB RAM,
  copy through a bounce buffer) per call; librga docs recommend `importbuffer_fd` / DMA-BUF (to cite).
- Fix: one `improcess`/`imresize`+`imcvtcolor` RGA call does BGR->NV12 *and* the downscale; write straight into the AVFrame
  (wrap its data pointers with the right wstride, or allocate the AVFrame from a DRM/dma-heap buffer and pass an fd), removing
  the scratch buffer and both copies (~3 MB of memcpy per 1080p frame, ~0.5-1 ms).
- Evidence (librga FAQ, https://github.com/airockchip/librga/blob/main/docs/Rockchip_FAQ_RGA_EN.md): Q1.1 "efficiency ...
  physical address > dma_fd > virtual address", measured at 1.1-1.2x / 1.3-1.5x / **1.8-2.1x** the theoretical time; Q1.7 with
  virtual addresses the page table is computed by the CPU and the cache is force-synced every frame, and "efficiency ... in a
  scenario with a high CPU load will be greatly reduced" (exactly LumenVision's situation: the A76s are saturated); Q1.8 "not
  recommended to call RGA using virtual addresses in actual product scenarios". Q1.1 theoretical 1080p copy at 300 MHz: RGA3
  ~1.7 ms, RGA2 ~3.5 ms; Q1.9/Q4.5 on RK3588 YUV420 *planar* formats go to RGA2 (needs <4 GB memory), NV12 can use RGA3.
  Orange Pi 5 boards ship with 4-32 GB, so virt-addr buffers above 4 GB on RGA2 can fail -> sws fallback (logged once).
- So: today's BGR->NV12 call is likely ~3-4 ms wall time under load (unmeasured), most of it driver overhead, not RGA work.
- Next step beyond that: hand the encoder an AV_PIX_FMT_DRM_PRIME frame (ffmpeg-rockchip's rkmpp encoder accepts
  `hw_frames_ctx` DRM_PRIME input - to verify) so the NV12 buffer the RGA wrote is imported zero-copy by MPP.

### F5. RecordSink never uses RGA; always CPU sws_scale BGR->YUV420P - MEDIUM
- `RecordSink.cpp:154-161, 258-260`. A 1080p BGR24->YUV420P bilinear `sws_scale` on an A76 is ~4-8 ms/frame (unverified; ARM
  NEON paths in swscale are limited for BGR24 input). With h264_rkmpp, NV12 input is what the VEPU wants natively; YUV420P
  is accepted but MPP may convert/stride-copy internally (to verify).
- Fix: reuse `RgaColorConverter` (with NV12 pix_fmt) as WebRTCSink does.

### F6. RecordSink: per-segment encoder rebuild and per-frame sidecar flush - LOW/MEDIUM
- Encoder destroyed and recreated at each 300 s segment boundary (`RecordSink.cpp:244-246`) - a small hiccup every 5 min; PTS could
  instead be offset per segment and an IDR forced. Sidecar `flush()` per frame = 30 small `write()` syscalls/s; the page cache
  absorbs them (no fsync), so disk write amplification is small, but a 4 KiB-page rewrite pattern on SD/eMMC is avoidable by
  flushing once per second.
- MP4 is not crash-safe: a power loss mid-segment leaves an unplayable file without `moov` (up to 300 s lost). Fragmented MP4
  (`movflags=frag_keyframe+empty_moov+default_base_moof`) or MPEG-TS/MKV makes every GOP recoverable at negligible CPU cost.
  On an FRC robot, power is cut at the end of every match - so this matters more than it seems.

### F7. MJPEG sink: encodes 30 fps, server consumes at most 10 fps; base64 round trip still present - MEDIUM
- `MjpegStreamModule.PollInterval = 100 ms` (`Server/HttpModules/MjpegStreamModule.cs:14`) and the stream loop only sends a frame
  when it differs from the last one. The sink encodes every input frame (`MjpegSink.cpp:61-81`) - at a 30 fps source, 2 of every 3
  JPEG encodes (each ~8-15 ms CPU at 1080p) are thrown away.
- Per client per poll: the whole base64 string is copied out under a mutex, marshalled through SWIG into a .NET UTF-16
  string (~2.7x the JPEG size in bytes), string-compared against the previous one, and `Convert.FromBase64String`ed
  (`MjpegStreamModule.cs:60-63`). Item 7 of PERFORMANCE_ANALYSIS.md is still open in code.
- Sharing: one encode is shared by all HTTP clients of one sink (good); the `MjpegPreviewJanitor` deletes idle `preview-mjpeg-*`
  sinks 30 s after the last read (`Server/MjpegPreviewJanitor.cs`) - so the fallback MJPEG path is already viewer-gated at
  ~30 s granularity, unlike WebRTC.
- Fix (in order): (1) encode lazily - the sink keeps the latest Frame (refcount bump) and encodes on the first `GetLatest...`
  call after a new frame arrives, so encodes = min(source fps, poll rate x distinct sinks); (2) return bytes via a SWIG byte-array
  typemap (`%apply (char *STRING, size_t LENGTH)` / `std::vector<uint8_t>` -> `byte[]`) and a frame sequence number instead of
  comparing strings; (3) replace polling with a wait on a frame-ready event; (4) encode with MPP's JPEG encoder (VEPU, see F8)
  or TurboJPEG directly from the NV12/YUV planes (`tjCompressFromYUVPlanes`), skipping BGR->YCbCr.
- Gain: at 1080p30 source: from ~30 x 10 ms = 300 ms CPU/s (30% of an A76) to ~10 x 10 ms = 100 ms/s with lazy encode; ~10 x 1 ms
  with MPP JPEG. Applies only while the fallback is in use.

### F8. h264_rkmpp (nyanmisaka fork) is non-blocking by default -> packets come out 1+ frames late; LumenVision never sets LOW_DELAY - HIGH
- Evidence (ffmpeg-rockchip `libavcodec/rkmppenc.c`, master, fetched 2026-10-24,
  https://github.com/nyanmisaka/ffmpeg-rockchip/blob/master/libavcodec/rkmppenc.c): `rkmpp_encode_frame` polls the MPP output with
  `MPP_TIMEOUT_NON_BLOCK` *unless* `avctx->flags & AV_CODEC_FLAG_LOW_DELAY`, in which case it uses `MPP_TIMEOUT_BLOCK`. Up to
  `H26X_ASYNC_FRAMES = 4` frames may be in flight (`rkmppenc.h`). Each call submits one frame then tries to get one packet.
- `WebRTCSink::EnsureEncoderInitialized` never sets `AV_CODEC_FLAG_LOW_DELAY` (`WebRTCSink.cpp:210-227`). With non-blocking
  polling the VEPU has not finished frame N (~3-8 ms for 1080p, unverified) when `avcodec_receive_packet` is called immediately
  after `avcodec_send_frame`, so frame N's packet is sent when frame N+1 arrives: **+1 frame (33 ms at 30 fps) of glass-to-glass
  latency**, potentially more under load (up to 4 queued). With libx264 zerolatency there is no such lag, so switching to rkmpp
  (F1) without this flag trades CPU for latency.
- Fix: `m_CodecContext->flags |= AV_CODEC_FLAG_LOW_DELAY;` for the WebRTC encoder (keep non-blocking for RecordSink, where
  throughput matters and latency does not - but RecordSink must then drain on close, which it already does).
- Corroboration: the fork's own wiki (https://github.com/nyanmisaka/ffmpeg-rockchip/wiki/Encoder) labels its
  `-flags +low_delay` examples "low latency, disable async encoding" for both `h264_rkmpp` and `mjpeg_rkmpp`.
- Confidence: high on the code path; the exact added latency needs measuring (inference from code, not a documented guarantee).
- Test: log `av_gettime_relative()` at send_frame and at the packet's arrival in `EncodeAndSend` (both with and without the flag);
  or in a browser, `chrome://webrtc-internals` total latency is not exposed, so film a stopwatch on a phone next to the preview.

### F9. h264_rkmpp defaults that matter for WebRTC (verified from rkmppenc.h / rkmppenc.c)
- Defaults: `rc_mode` auto -> **CBR** when no `rc_max_rate` (VBR only if `rc_max_rate > 0`, FIXQP if `qp_init >= 0`);
  CBR bounds are bps_target x 15/16 .. 17/16. **profile = High, CABAC on, 8x8dct on, level auto.** `qp_init` 26, `qp_max` 48,
  `qp_min` 0 if unset. `gop_size` -> `rc:gop`. `max_b_frames` is ignored (MPP H.264 encode has no B-frames, so fine).
  Intra refresh supported: options `intra_refresh=1`, `refresh_mode=row|col`, `refresh_num=N`.
- Forced IDR: any input frame with `pict_type == AV_PICTURE_TYPE_I` triggers `MPP_ENC_SET_IDR_FRAME` (H.264/HEVC). So F2's "keep
  the encoder, force an IDR on join" works with both libx264 and h264_rkmpp.
- Rate-control settings are applied once at init; no runtime reconfigure - a bitrate change still needs a rebuild (current
  behaviour in `SetSettings` is therefore correct for rkmpp).
- Input formats: NV12, YUV420P, **BGR24/RGB24/BGRA** etc. are all accepted on RK3588 (RGB inputs are "pre-RK3576 only", i.e. the
  RK3588 VEPU580 has an RGB->YUV front end). Non-DRM input frames are copied into a DRM_PRIME frame from an internal hwframe
  pool via `av_hwframe_transfer_data` (a CPU memcpy), then imported with `mpp_buffer_import`; DRM_PRIME input
  (`avctx->pix_fmt = AV_PIX_FMT_DRM_PRIME`, `sw_pix_fmt` NV12) is imported zero-copy (LINEAR or AFBC modifiers).
- Consequence for today's path: RGA writes NV12 to a scratch buffer -> 2 memcpys into the AVFrame -> rkmpp memcpys it again into
  a DRM buffer = **3 full NV12 copies** (3 x 3.1 MB at 1080p = ~9.3 MB/frame, ~280 MB/s at 30 fps; ~1-2 ms of A76 time per
  frame plus DDR bandwidth). See recommendation R4 for the zero-copy path.
- Profile/browser compatibility: High profile H.264 is decoded by all current browsers' WebRTC stacks, but libdatachannel's
  `addH264Codec` advertises a default fmtp (to check: `profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1`).
  Chrome negotiates on the fmtp; a High-profile bitstream under a constrained-baseline profile-level-id normally still decodes
  in Chrome/Firefox software decoders, but hardware decoders (Safari/iOS VideoToolbox, some Android MediaCodec) can reject it.
  Safer: make the SDP and the bitstream agree - either encode `profile=baseline` (forces CAVLC) or advertise a High-profile
  `profile-level-id` matching the encoder (see F10). Bitrate cost of Baseline vs High at equal quality is roughly 10-20%
  (CABAC + 8x8 transform; general figure, unverified on VEPU580).
- Low-latency tuning: CBR with a short `rc_buffer_size` (stats_time = rc_buffer_size / max_bps seconds) limits I-frame bursts;
  `intra_refresh=1` (rolling intra) removes the periodic IDR burst entirely - useful on the FRC field (~4 Mbit/s cap on the robot
  radio per FMS bandwidth rules, to cite) where a 2-s GOP IDR at 4 Mbit/s is a ~100-200 KB burst.

### F10. SDP advertises Constrained Baseline L3.1 but the stream can be High @ L4.x - MEDIUM
- libdatachannel `DEFAULT_H264_VIDEO_PROFILE = "profile-level-id=42e01f;packetization-mode=1;level-asymmetry-allowed=1"`
  (`include/rtc/description.hpp`, https://github.com/paullouisageneau/libdatachannel/blob/master/include/rtc/description.hpp);
  `WebRTCSink.cpp:60` calls `addH264Codec(96)` with that default. 42e01f = Constrained Baseline, level 3.1 (max 3600 macroblocks
  per frame = 1280x720). 1080p (8160 MBs) needs level 4.0; h264_rkmpp defaults to High profile with CABAC (F9). libx264
  ultrafast happens to produce CB-compatible tools (no CABAC/8x8/B) but its level is auto (4.0 at 1080p).
- Desktop Chrome/Firefox software decoders tolerate both mismatches in practice; hardware decoders on Safari/iOS/Android and
  Chrome's hardware decode path may not (unverified per platform). Driver Station laptops are Windows Chrome/Edge, where Chrome
  uses D3D11 hardware H.264 decode when available - mismatches there show up as fallback or decode errors (to test).
- Fix: either set `profile=baseline` (`av_opt_set(priv, "profile", "baseline")` -> CAVLC) and `level` to match, or pass a correct
  fmtp to `addH264Codec`, e.g. `profile-level-id=640028` (High L4.0) when using rkmpp at 1080p. Zero cost.

### F11. Browser feedback is negotiated but ignored: no PLI/FIR handling, no bandwidth adaptation - MEDIUM
- libdatachannel's `Video::addVideoCodec` adds `rtcp-fb` `nack`, `nack pli`, `ccm fir`, `goog-remb` by default (src/description.cpp).
  The media handler chain is packetizer -> `RtcpSrReporter` -> `RtcpNackResponder` (`WebRTCSink.cpp:64-69`); there is no
  `RtcpPliHandler` and no `RembHandler`. So when the browser loses a packet that the NACK store (default 512 packets,
  `rtcpnackresponder.hpp`) cannot repair, or its decoder errors, its PLI/FIR is dropped and the picture stays broken/frozen until
  the next periodic IDR (gop = 2 x fps = 2 s).
- No pacing: every packet of a frame is sent back-to-back by `sendFrame`; an IDR at 4 Mbit/s 1080p is ~60-150 KB = 50-120 RTP
  packets in one burst, the classic cause of loss on Wi-Fi/robot radio links. libdatachannel >= 0.21 has
  `PacingHandler(bitsPerSecond, sendInterval, maxQueueSize)` (`include/rtc/pacinghandler.hpp`).
- No bitrate adaptation: bitrate is fixed by settings. A `RembHandler` (callback with the browser's estimate) could drive an
  encoder reconfigure; with ffmpeg-rockchip that needs a rebuild (F9), with MPP used directly it is a live `MPP_ENC_SET_CFG`.
- Fix: add `RtcpPliHandler([this]{ m_ForceKeyframe = true; })` to each peer's chain and set `pict_type = AV_PICTURE_TYPE_I` on the
  next frame (this also replaces the encoder-rebuild-on-join in F2); add `PacingHandler(bitrate*1.5, 5 ms)`; optionally intra
  refresh to avoid IDR bursts altogether. Effort: small (tens of lines). Note `install-deps.sh:581` clones libdatachannel HEAD
  unpinned, so these classes are present, but the version should be pinned (cf. memory note on pinning).

### F12. MJPEG sink could use the VEPU121 JPEG encoders (mjpeg_rkmpp) - LOW/MEDIUM (fallback path only)
- ffmpeg-rockchip has `mjpeg_rkmpp` (`DEFINE_RKMPP_ENCODER(mjpeg, ...)` in rkmppenc.c): FIXQP, `qp_init` = JPEG quality
  1-99 (default 80, maps to `jpeg:q_factor`), accepts NV12/YUV420P and, on RK3588, BGR24/BGRA directly (RGB formats are
  "pre-RK3576 only" in `rkmpp_get_mpp_fmt_mjpeg`), full-range output, `MJPEG_ASYNC_FRAMES = 8` so `-flags +low_delay` is
  needed here too. RK3588 has 4-5 VEPU121 JPEG-only encoder cores (kernel thread "Enable JPEG encoding on rk3588",
  https://lore.kernel.org/all/3784322.QJadu78ljV@diego/T/). Throughput spec quoted by Forlinx: ~90 Mpixel/s
  (https://www.forlinx.net/article_view_447.html) - ~23 ms per 1080p frame if that is per core (unverified; may be aggregate).
  So hardware JPEG is not necessarily *faster* per frame than libjpeg-turbo on an A76 (~8-12 ms at 1080p q80, unverified on this
  board), but it is *off-CPU*.
- Since the MJPEG path is only a fallback and is already janitor-gated, priority is lazy encoding + dropping base64 (F7) first.
  The build already links libavcodec, so `mjpeg_rkmpp` is a ~60-line change behind the existing encoder-name probe pattern.
- Alternative: TurboJPEG `tj3Compress8`/`tjCompressFromYUVPlanes` directly from the camera's NV12 (needs a planar copy of UV) or
  from BGR with `TJSAMP_420` and `TJFLAG_FASTDCT`: typically 20-30% faster than `cv::imencode` (which uses libjpeg's API via
  libjpeg-turbo with default slow-DCT), unverified on this board.

### F13. H.265 for WebRTC: not worth it now; H.265 for recordings: worth an option - LOW
- Chrome 136+ ships WebRTC HEVC **hardware-decode only** (IETF AVTCORE slides, https://datatracker.ietf.org/meeting/125/materials/slides-125-avtcore-h265-profile-for-webrtc-00;
  MediaMTX discussion https://github.com/bluenviron/mediamtx/discussions/4396), Edge does not send/negotiate it
  (https://learn.microsoft.com/en-ca/answers/questions/5880331/), Firefox does not support it. Driver Station laptops vary.
  On a LAN preview at 2-4 Mbit/s H.264 is fine; H.265 would need offer-both-and-pick logic. Rejected for WebRTC.
- Recording: `hevc_rkmpp` at the same quality needs roughly 30-50% fewer bits than H.264 (general HEVC vs AVC figure, not
  measured on VEPU580), i.e. fewer SD/eMMC bytes per match. Cost: browsers can only play HEVC MP4 with hardware decode, so
  downloading a segment to a laptop may not play in a browser. Make it an option, not the default.

### F14. Recording I/O and crash safety - MEDIUM (data loss), LOW (CPU)
- Bytes: 8 Mbit/s default = 1 MB/s = 60 MB/min = ~150 MB per 2.5-min match; 300 s segments = 300 MB files. SD/eMMC sustain
  this easily; the risk is not throughput but write latency spikes blocking `av_interleaved_write_frame` on the sink thread
  (it is synchronous on the sink's processing thread, `RecordSink.cpp:265-272`). The sink thread only lags itself (it reads the
  latest frame), so a stall drops recorded frames rather than slowing detection. Acceptable; an async writer thread with a
  bounded queue would avoid dropped frames under SD stalls.
- Crash safety: plain MP4 writes the `moov` index only in `av_write_trailer`. FRC robots lose power at the end of every match
  (and on brownouts); an unfinalised segment is unplayable without repair (untrunc etc.). With `av_dict_set(&opts, "movflags",
  "frag_keyframe+empty_moov+default_base_moof", 0)` passed to `avformat_write_header`, every GOP (2 s) is a self-contained
  fragment: at most ~2 s lost on power cut, at negligible CPU cost, and the file still plays everywhere. Alternatively
  MPEG-TS / Matroska. **This is a correctness fix for robot use, recommended regardless of performance.**
- Sidecar: one `write()` per frame via `flush()` (`RecordSink.cpp:282-283`) - ~30 syscalls/s; harmless on page cache, but
  JSONL lines are 100-500 B, so flushing once per second (or per GOP) reduces syscalls 30x with the same crash window as fMP4.
- Segment rotation tears the encoder down (F6); with forced IDR via `pict_type` + per-segment PTS offset the encoder could stay up.

### F15. Raw-camera previews pay an NV12 -> BGR -> NV12 round trip - MEDIUM
- The MPP JPEG decoder produces NV12 and converts it to BGR on the CPU with `cv::cvtColor(COLOR_YUV2BGR_NV12)`
  (`MppJpegDecoder.cpp:162-183`), V4L2 NV12/YUYV cameras likewise (`V4l2CameraBackend.cpp:354-379`). When a WebRTC/Record sink is
  bound directly to a camera (no annotation needed), the sink then converts that BGR straight back to NV12 (RGA) or YUV420P
  (sws). At 1080p: NV12->BGR ~2-4 ms CPU + BGR->NV12 RGA ~1-2 ms + copies. Note the BGR is only produced because a colour consumer
  exists (`HasActiveColorFrameConsumer`), i.e. *because of* the preview.
- Fix: keep the decoder's NV12 in `Frame` (FrameFormat::NV12 already exists, `Frame.cpp:69`), add a cached `AsNv12()` view, and let
  encoder sinks take NV12 when the source is NV12 (zero conversions; with a DMA-BUF-backed buffer, zero copies into MPP). Annotated
  detector output stays BGR (drawing is done in BGR); see R-ideas for drawing overlays client-side instead.
- Overlaps with agent(s) covering decode/Frame format; flagged here because the payoff is on the encode side.

### F16. Timestamps are synthesised from a frame counter at the nominal fps, not from capture time - MEDIUM
- WebRTC: RTP time = `m_FrameCounter / m_Config.fps` (`WebRTCSink.cpp:307,314`). Record: `pts = m_FrameCounter++` with
  `time_base = 1/fps` (`RecordSink.cpp:132,261`). Sinks only see the *latest* result and skip frames when they lag; sources can be
  fps-limited (`ISource::SetFpsLimit`), detectors (CPU AprilTag 36 ms) run below 30 fps, and USB cameras deliver variable rates.
  Whenever the actual delivery rate differs from `fps`, (a) the browser jitter buffer sees RTP timestamps that disagree with arrival
  times and adapts its delay (adds latency or stutters), (b) recordings play back too fast/slow and the video time no longer
  matches the JSONL sidecar's `captureTimeUs` - which defeats the sidecar's purpose for match review.
- Fix: derive timestamps from `result.captureTimeUs` (already in every SourceResult): RTP via `sendFrame(sample,
  FrameInfo{ rtpTimestamp from (captureTimeUs - t0) * 90 / 1000 })`, MP4 via `time_base = {1, 1000000}` (or 1/90000) and
  `pts = captureTimeUs - segmentStartUs`. Zero CPU cost; fixes F2's rewind too.
- Confidence: high (code); visible effect depends on how far real fps deviates.

### F17. Zero-copy RGA -> MPP is possible with what is already built (ffmpeg-rockchip hwcontext) - design note
- ffmpeg-rockchip ships `AV_HWDEVICE_TYPE_RKMPP` (`libavutil/hwcontext_rkmpp.c`): DRM_PRIME frames from an MPP buffer group,
  **DMA32 by default** (below 4 GB, so RGA2 can address them), linear NV12 layout, fd exposed via `AVDRMFrameDescriptor`
  (`objects[0].fd`). Its `transfer_data_to` is a CPU `av_frame_copy` into the mapped buffer - which is what h264_rkmpp does
  internally for every non-DRM frame today (F9).
- Path: `av_hwdevice_ctx_create(RKMPP)` -> frames ctx {format DRM_PRIME, sw_format NV12, w, h, initial_pool_size 4-6} ->
  per frame `av_hwframe_get_buffer` -> RGA `importbuffer_fd(desc->objects[0].fd, size)` (cache the handle per fd) ->
  one `improcess` doing BGR888 (src: virtual addr, or an fd if the BGR came from a DMA-BUF) -> NV12 **with the resize** ->
  `avcodec_send_frame` with `codec_ctx->pix_fmt = DRM_PRIME`, `sw_pix_fmt = NV12`, `hw_frames_ctx` set -> `mpp_buffer_import`
  zero-copy. Removes the scratch buffer, 2 memcpys in RgaColorConverter, the encoder's internal copy and the CPU `cv::resize`.
- Gain at 1080p30: ~9 MB/frame of copies (~280 MB/s DDR traffic) and an estimated 2-4 ms CPU per frame (copies + resize at
  scaleDivisor > 1; `cv::resize INTER_AREA` 1080p->540p is ~1-2 ms on an A76, unverified), plus lower RGA driver overhead for the
  destination side. Effort: medium (1-2 days incl. fallback); risk: format/stride/alignment corner cases (RGA needs width
  stride 4-aligned for RGB888, F4), keep sws fallback.
- The cheaper middle step (no hwcontext): feed **BGR24 directly** to h264_rkmpp (supported input on RK3588, F9) and drop RGA:
  one 6 MB CPU copy into the encoder's DRM buffer, colour conversion done by the VEPU's pre-processor. Fewer moving parts than
  today, likely similar or lower CPU; colour matrix/range of the VEPU RGB->YUV path must be checked (BT.601 vs 709, limited range).

### F19. The default WebRTC bitrate equals the entire FRC field bandwidth cap - HIGH (on-field)
- WebRTCSink defaults: 4000 kbit/s, 30 fps, full resolution (`WebRTCSink.h:24-29`, `WebRTCSinkController.cs:20`,
  `ApiService.ts:508`). The FRC robot radio enforces a **4 Mbit/s total** limit at events, not per client (WPILib docs,
  https://docs.wpilib.org/en/2025/docs/zero-to-robot/step-3/openmesh.html; reduced from 7 to 4 Mbit/s in 2019,
  https://www.chiefdelphi.com/t/bandwidth-discrepency/336165). When the link is full, round-trip latency rises from ~5 to ~50 ms
  (same thread) - i.e. a single default preview with CBR (F9: rkmpp defaults to CBR at bps_target +/- 6%) can starve NT4 and
  the driver station, plus IDR bursts every 2 s (F11). The ICE port range 5801-5809 (`WebRTCSink.cpp:15-16`) shows on-field use
  is intended.
- Fix: a "field" preset: scaleDivisor 2-3 via RGA (640x360 / 960x540), 15-20 fps, 0.8-1.5 Mbit/s, VBR with `rc_max_rate`, intra
  refresh instead of periodic IDR; keep 4 Mbit/s+ for pit/LAN. Also count viewers: N viewers = N x bitrate on the radio link.
  Downscaling also cuts encode and conversion work ~4x at divisor 2.
- Confidence: high on the numbers; whether teams stream during matches is a product question.

### F18. No output-side timing in the stage profiler - LOW (enabler)
- `ISink` exposes `SinkProfile()` (`ISink.h:34`) but WebRTCSink/MjpegSink/RecordSink record no stages (none in the files read).
  Add `convert`, `encode` (send_frame -> packet), `send` and `jpeg` stages so recommendations F1/F4/F8/F17 can be measured with
  `GET /api/profile` like the detector numbers in PERFORMANCE_ANALYSIS.md.

## Ideas rejected (and why)
- **Sharing packetisation/SRTP across WebRTC viewers**: each peer has its own SRTP keys and SSRC state; per-viewer cost at
  4 Mbit/s is small (AES-128 with ARMv8 crypto extensions, <1% of a core). Already one encode per sink. Not worth it.
- **H.265 for the live WebRTC preview**: browser support is hardware-decode-only in Chrome, absent in Edge/Firefox (F13).
- **AV1**: RK3588 VPU decodes AV1 but has no AV1 encoder. Not applicable.
- **Using the ISP for scaling/colour**: the ISP only processes MIPI-CSI sensor input; LumenVision's cameras are USB UVC (MJPEG/YUYV),
  which bypass the ISP. Not applicable unless MIPI cameras are adopted.
- **Making MjpegSink share the WebRTC encoder's output**: different codecs; a JPEG must be encoded separately.
- **Sharing one H.264 encode between WebRTC and RecordSink on the same node**: possible (same codec), but they want different
  bitrate/GOP/latency trade-offs and the VEPU has large headroom (spec ~1080p@480 fps aggregate H.264/HEVC,
  https://jellyfin.org/docs/general/post-install/transcoding/hardware-acceleration/rockchip/). Only the CPU-side conversion is
  worth sharing (cache an NV12 view on the Frame, F15).

## Ranked recommendations (output side)

Ordered by expected gain per unit of effort. CPU figures are per 1080p30 stream on an A76 unless stated; "unverified" figures are
estimates to be confirmed with R0.

| # | Change | Where | Effort | Expected gain |
|---|---|---|---|---|
| R0 | Add output-side stages (`convert`, `encode`, `send`, `jpeg`, `mux`) to the sinks' `StageProfile` | WebRTCSink/MjpegSink/RecordSink | S | Enables measuring everything below via `GET /api/profile` (F18) |
| R1 | Stop the web UI forcing `encoderName=libx264`; let the server pick `h264_rkmpp` | `ApiService.ts:508` | XS (1 line) | Every UI-created preview currently encodes in software: ~15-30 ms/frame of A76 time (0.5-1+ core) -> ~1-2 ms. Largest single CPU win on the output side (F1) |
| R2 | `flags |= AV_CODEC_FLAG_LOW_DELAY` on the WebRTC encoder | `WebRTCSink.cpp:~225` | XS (1 line) | Removes >= 1 frame (33 ms at 30 fps, up to 4 frames under load) of glass-to-glass latency with h264_rkmpp (F8). Must ship together with R1 |
| R3 | Viewer-gated frame consumption: frame-consumer `isActive` = "has an open WebRTC track / MJPEG read in last N s / RecordSink running", not merely "toggled on" | `ISink.cpp:132`, WebRTCSink, MjpegSink | S-M | With no viewer: grey instead of colour decode (~2-3 ms), no detector annotate copy (~3-4 ms on the detector critical path), no sink-side BGR/resize. Paid today whenever a preview was left toggled on (F3) |
| R4 | Fragmented MP4 (`movflags=frag_keyframe+empty_moov+default_base_moof`) | `RecordSink.cpp:203` | XS | Power cut at match end loses <= 1 GOP (~2 s) instead of the whole segment (up to 300 s). Correctness, ~0 CPU (F14) |
| R5 | Timestamps from `captureTimeUs` (RTP and MP4 pts); keep the encoder alive and force IDR via `pict_type = I` on new viewer, on PLI/FIR (`RtcpPliHandler`) and on segment roll | WebRTCSink, RecordSink | S | Fixes RTP rewind for existing viewers when one joins, faster recovery from loss (PLI honoured), recordings that match the telemetry sidecar; removes encoder rebuild stalls (F2, F11, F16) |
| R6 | Advertise the profile/level actually sent (or encode `profile=baseline`, level 4.0/4.1) | `WebRTCSink.cpp:60,226` | XS | Avoids hardware-decoder rejections on some browsers/devices (F10) |
| R7 | "Field" preset: RGA downscale /2-/3, 15-20 fps, 0.8-1.5 Mbit/s VBR (`rc_max_rate`), `intra_refresh=1`; `PacingHandler` per peer | WebRTCSink + UI | S | Keeps the preview inside the 4 Mbit/s FRC radio cap with headroom for NT4; removes IDR bursts; ~4x less conversion/encode work (F19, F11) |
| R8 | RecordSink: NV12 via RGA (reuse RgaColorConverter) or BGR24 straight into h264_rkmpp, instead of CPU `sws_scale` BGR->YUV420P | `RecordSink.cpp:154,258` | S | ~4-8 ms/frame of A76 time per recording stream (unverified) (F5) |
| R9 | One RGA call for scale + BGR->NV12, writing into an RKMPP DRM_PRIME hwframe (fd import), encoder fed DRM_PRIME = zero copy | RgaColorConverter, WebRTCSink, RecordSink | M | Removes CPU `cv::resize` (1-2 ms), scratch buffer + 2 memcpys + encoder-internal copy (~9 MB/frame, ~280 MB/s DDR); RGA itself 1.8-2.1x faster with fd vs virtual addresses per librga FAQ. Est. 2-4 ms CPU/frame (F4, F17) |
| R10 | MJPEG: lazy encode on read (<= poll rate), raw bytes over SWIG + sequence number instead of base64 strings, event instead of 100 ms polling; then `mjpeg_rkmpp` (or TurboJPEG from YUV) | MjpegSink, MjpegStreamModule | S then M | At 30 fps source / 10 fps poll: ~3x fewer JPEG encodes (~200 ms CPU/s saved at 1080p), no base64/UTF-16 churn; HW JPEG takes the rest off-CPU. Fallback path only (F7, F12) |
| R11 | NV12 passthrough: keep the MPP decoder's NV12 in `Frame`, add cached `AsNv12()`, let encoder sinks consume it when no annotation is needed | Frame, MppJpegDecoder, sinks | M | Raw-camera previews/recordings skip NV12->BGR (CPU, 2-4 ms) and BGR->NV12 (RGA); one shared conversion when WebRTC + Record bind the same node (F15) |
| R12 | Record writer thread with bounded queue; sidecar flushed once per second | RecordSink | S | No dropped recorded frames during SD/eMMC write stalls; 30x fewer sidecar syscalls (F6, F14) |
| R13 | Option: `hevc_rkmpp` for recordings | RecordSink config/UI | S | ~30-50% fewer bytes for the same quality (general HEVC figure, unverified on VEPU580); browser playback of HEVC MP4 is hardware-dependent (F13) |
| R14 | Longer term: draw overlays in the browser from detection JSON (data channel or the existing status feed) so detectors never annotate frames and previews can be the zero-copy camera NV12 | detectors, WebRTCSink, web UI | L | Removes the annotate copy entirely (3-4 ms on the detector critical path whenever previewing) and makes R9/R11 apply to every preview |

Suggested order: R0 -> R1+R2 (ship together) -> R3 -> R4 -> R5/R6 -> R7 -> R8 -> R9/R11 -> R10 -> R12-R14.

## How to test on the Orange Pi (common to all)
- Which encoder: `journalctl -u <service> | grep "WebRTCSink constructed"`; `top -H` for x264 threads; MPP sessions in
  `/proc/mpp_service/sessions-summary` (vendor kernel) and VEPU load in `/proc/mpp_service/load` (path to verify per kernel).
- RGA load: `cat /sys/kernel/debug/rkrga/load` (vendor kernel) while previewing.
- Latency: film a phone stopwatch next to the browser preview (glass-to-glass), or stamp `captureTimeUs` into an SEI (`udu_sei`)
  and compare in a test page; compare with/without R2.
- Browser side: `chrome://webrtc-internals` -> inbound-rtp `framesDropped`, `freezeCount`, `pliCount`, `nackCount`,
  `jitterBufferDelay/jitterBufferEmittedCount`, decoder implementation (hardware vs software).
- CPU per stage: R0 + `GET /api/profile`; system-wide `mpstat -P ALL 1` to see A76 vs A55 usage.
- Recording crash test: start recording, `kill -9` the service (or cut power), check the segment plays (`ffprobe`).

## Open questions
- Exact VEPU580 1080p encode time per frame and whether two h264_rkmpp sessions spread over both RKVENC cores automatically
  (MPP's multi-core scheduling on RK3588) - measure with R0.
- Is `mjpeg_rkmpp`'s ~90 Mpixel/s (Forlinx spec) per VEPU121 core or aggregate? Determines whether HW JPEG is faster per frame
  than libjpeg-turbo, or only off-CPU.
- VEPU RGB->YUV front-end colour matrix/range for BGR24 input (affects the "feed BGR24 directly" shortcut in F17).
- Whether Chrome's hardware H.264 decode path on Windows Driver Station laptops rejects High-profile streams under a 42e01f offer
  (F10) - quick test with a laptop.
- Is libdatachannel pinned anywhere? `install-deps.sh:581` clones HEAD with `--depth 1`; `PacingHandler` needs >= 0.21.
