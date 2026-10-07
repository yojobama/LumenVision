
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace Server
{
    public class SinkManager
    {
        public static SinkManager Instance { get; } = new SinkManager();

        private List<Sink> sinks;

        private SinkManager()
        {
            sinks = new List<Sink>();
        }

        // Returns the native per-sink result JSON.
        public string GetResult(int sinkId) => ManagerWrapper.Instance.GetSinkResult(sinkId);
        public string GetAllResults() => ManagerWrapper.Instance.GetAllSinkResults();

        public Sink GetSinkById(int id)
        {
            foreach (var sink in sinks)
            {
                if (sink.Id == id)
                {
                    return sink;
                }
            }

            return null;
        }

        public int[] getAllSinkIds()
        {
            List<int> ids = new List<int>();
            foreach (var sink in sinks)
            {
                ids.Add(sink.Id);
            }

            return ids.ToArray();
        }

        public List<Sink> GetAllSinks()
        {
            return sinks;
        }

        public void SetSinkName(int sinkId, string dstName)
        {
            foreach (var sink in sinks)
            {
                if (sink.Id == sinkId)
                {
                    sink.Name = dstName;
                    DB.Instance.Save();
                    break;
                }
            }
        }

        public void DeleteSink(int sinkId)
        {
            ManagerWrapper.Instance.DeleteSink(sinkId);
            sinks.RemoveAll(sink => sink.Id == sinkId);
            DB.Instance.Save();
        }

        public int AddSink(string name, string type, int? id = null)
        {
            // Normalise the type name so matching is case-insensitive
            type = (type ?? string.Empty).Trim().ToLowerInvariant();

            if (id.HasValue)
            {
                switch (type)
                {
                    // "apriltagsink" is what DB.Load() passes (lower-cased SinkType.ApriltagSink);
                    // "apriltag" and "apritlag" are accepted short forms.
                    case "apriltagsink":
                    case "apriltag":
                    case "apritlag": 
                        id = ManagerWrapper.Instance.CreateApriltagDetector(id.Value);
                        sinks.Add(new Sink(id.Value, name, SinkType.ApriltagSink));
                        break;
                    case "objectdetectionsink":
                    case "ObjectDetectionSink":
                        // Unreachable in practice: the no-model CreateObjectDetectionSink overload always throws.
                        id = ManagerWrapper.Instance.CreateObjectDetectionSink(ObjectDetectionProvider.ONNX, id.Value);
                        sinks.Add(new Sink(id.Value, name, SinkType.ObjectDetectionSink));
                        break;
                    case "depthfusionsink":
                        id = ManagerWrapper.Instance.CreateDepthFusionNode(id.Value);
                        sinks.Add(new Sink(id.Value, name, SinkType.DepthFusionSink));
                        break;
                    // StereoDepthSink is not restorable here: it needs a backend, calibration result and depth range
                    // this signature cannot carry, so DB.Load() re-creates it as a no-op (id stays unset).
                }
                return id.GetValueOrDefault(-1);
            }
            else
            {
                switch (type)
                {
                    case "apriltagsink":
                    case "apriltag":
                    case "apritlag":
                        id = ManagerWrapper.Instance.CreateApriltagDetector();
                        sinks.Add(new Sink(id.Value, name, SinkType.ApriltagSink));
                        break;
                    case "objectdetectionsink":
                        id = ManagerWrapper.Instance.CreateObjectDetectionSink(ObjectDetectionProvider.ONNX);
                        sinks.Add(new Sink(id.Value, name, SinkType.ObjectDetectionSink));
                        break;
                    case "depthfusionsink":
                        id = ManagerWrapper.Instance.CreateDepthFusionNode();
                        sinks.Add(new Sink(id.Value, name, SinkType.DepthFusionSink));
                        break;
                }

                DB.Instance.Save();
                return id.GetValueOrDefault(-1);
            }
        }

        // creates an ApriltagSink using the saved calibration of a camera source at its current resolution,
        // so the apriltag detections can be translated into real world tag locations
        public int AddApriltagSinkForCamera(string name, int sourceId, double tagSize)
        {
            CameraCalibrationResult calibration = CalibrationManager.Instance.GetForSource(sourceId)
                ?? throw Server.Web.ApiException.BadRequest($"no saved calibration for source {sourceId} at its current resolution");
            int id = ManagerWrapper.Instance.CreateApriltagDetector(calibration, tagSize);
            sinks.Add(NewApriltagSinkRecord(id, name, tagSize, ApriltagBackendKind.APRILTAG_BACKEND_CPU, 0, 0.0f, true));
            DB.Instance.Save();
            return id;
        }

        // creates an ApriltagSink with an explicit backend (CPU or Vulkan) and no calibration data.
        // frameWidth/frameHeight are a hint (0 is fine); nthreads/quadDecimate <= 0 mean the backend default (see ApriltagTuning).
        public int AddApriltagSinkWithBackend(string name, double tagSize, ApriltagBackendKind backend, int frameWidth, int frameHeight,
            int nthreads = 0, float quadDecimate = 0.0f, bool refineEdges = true, RefineEdgesMode refineMode = RefineEdgesMode.REFINE_EXACT,
            ApriltagAdvancedTuning? advanced = null)
        {
            int id = ManagerWrapper.Instance.CreateApriltagDetector(new CameraCalibrationResult(), tagSize, backend, frameWidth, frameHeight,
                MakeTuning(nthreads, quadDecimate, refineEdges, refineMode, advanced));
            sinks.Add(NewApriltagSinkRecord(id, name, tagSize, backend, nthreads, quadDecimate, refineEdges, refineMode, advanced));
            DB.Instance.Save();
            return id;
        }

        private static ApriltagTuning MakeTuning(int nthreads, float quadDecimate, bool refineEdges, RefineEdgesMode refineMode,
            ApriltagAdvancedTuning? advanced = null)
        {
            var tuning = new ApriltagTuning { nthreads = nthreads, quadDecimate = quadDecimate, refineEdges = refineEdges, refineMode = refineMode };
            advanced?.ApplyTo(tuning);
            return tuning;
        }

        // the persisted record for an ApriltagSink, carrying the requested configuration for RestoreApriltagSink
        private static Sink NewApriltagSinkRecord(int id, string name, double tagSize, ApriltagBackendKind backend,
            int nthreads, float quadDecimate, bool refineEdges, RefineEdgesMode refineMode = RefineEdgesMode.REFINE_EXACT,
            ApriltagAdvancedTuning? advanced = null) =>
            new Sink(id, name, SinkType.ApriltagSink)
            {
                ApriltagAdvanced = advanced == null || advanced.IsDefault ? null : advanced.Clone(),
                ApriltagTagSize = tagSize,
                ApriltagBackend = backend,
                ApriltagThreads = nthreads,
                ApriltagQuadDecimate = quadDecimate,
                ApriltagRefineEdges = refineEdges,
                ApriltagRefineMode = refineMode,
            };

        // DB.Load() restore path: recreates the ApriltagSink at the same id with its saved tag size, backend and tuning
        public int RestoreApriltagSink(Sink persisted)
        {
            double tagSize = persisted.ApriltagTagSize ?? 0.1651;
            ApriltagBackendKind backend = persisted.ApriltagBackend ?? ApriltagBackendKind.APRILTAG_BACKEND_CPU;
            int nthreads = persisted.ApriltagThreads ?? 0;
            float quadDecimate = persisted.ApriltagQuadDecimate ?? 0.0f;
            bool refineEdges = persisted.ApriltagRefineEdges ?? true;
            RefineEdgesMode refineMode = persisted.ApriltagRefineMode ?? RefineEdgesMode.REFINE_EXACT;
            int id = ManagerWrapper.Instance.CreateApriltagDetector(persisted.Id, new CameraCalibrationResult(), tagSize,
                backend, 0, 0, MakeTuning(nthreads, quadDecimate, refineEdges, refineMode, persisted.ApriltagAdvanced));
            sinks.Add(NewApriltagSinkRecord(id, persisted.Name, tagSize, backend, nthreads, quadDecimate, refineEdges, refineMode, persisted.ApriltagAdvanced));
            return id;
        }

        // reports the backend an ApriltagSink actually runs; may differ from the request if no usable Vulkan device was found
        public string GetApriltagBackendName(int sinkId)
        {
            return ManagerWrapper.Instance.GetApriltagDetectorBackendName(sinkId);
        }

        // Switches an existing ApriltagSink between CPU/Vulkan by rebuilding it at the same id and re-establishing its bindings.
        // nthreads/quadDecimate/refineEdges/refineMode default to the sink's current values; frame size is 0x0 so a Vulkan detector sizes itself from its first frame.
        public void SetApriltagBackend(int sinkId, ApriltagBackendKind backend, int? nthreads = null, float? quadDecimate = null,
            bool? refineEdges = null, RefineEdgesMode? refineMode = null, ApriltagAdvancedTuning? advanced = null)
        {
            Sink sink = GetSinkById(sinkId) ?? throw new ArgumentException($"no sink with id {sinkId}");
            if (sink.Type != SinkType.ApriltagSink)
                throw new ArgumentException($"sink {sinkId} is not an ApriltagSink");

            double tagSize = ManagerWrapper.Instance.GetApriltagDetectorTagSize(sinkId);
            CameraCalibrationResult calibration = ManagerWrapper.Instance.GetApriltagDetectorCalibration(sinkId);
            bool driverMode = ManagerWrapper.Instance.GetDriverMode(sinkId);
            bool wasRunning = IsSinkRunning(sinkId);
            int? upstreamSourceId = sink.Source?.Id;
            List<int> downstreamSinkIds = GetSinksBoundToSource(sinkId);
            string name = sink.Name;
            // prefer the persisted request over the live read-back (which reports resolved or CPU-fallback values)
            int effectiveThreads = nthreads ?? sink.ApriltagThreads ?? ManagerWrapper.Instance.GetApriltagDetectorThreads(sinkId);
            float effectiveQuadDecimate = quadDecimate ?? sink.ApriltagQuadDecimate ?? ManagerWrapper.Instance.GetApriltagDetectorQuadDecimate(sinkId);
            bool effectiveRefineEdges = refineEdges ?? sink.ApriltagRefineEdges ?? ManagerWrapper.Instance.GetApriltagDetectorRefineEdges(sinkId);
            RefineEdgesMode effectiveRefineMode = refineMode ?? sink.ApriltagRefineMode ?? ManagerWrapper.Instance.GetApriltagDetectorRefineMode(sinkId);

            // settings given now replace the saved ones member by member; the rest are carried over
            ApriltagAdvancedTuning? effectiveAdvanced = advanced != null ? advanced.MergedOver(sink.ApriltagAdvanced) : sink.ApriltagAdvanced;

            DeleteSink(sinkId);

            ManagerWrapper.Instance.CreateApriltagDetector(sinkId, calibration, tagSize, backend, 0, 0,
                MakeTuning(effectiveThreads, effectiveQuadDecimate, effectiveRefineEdges, effectiveRefineMode, effectiveAdvanced));
            sinks.Add(NewApriltagSinkRecord(sinkId, name, tagSize, backend, effectiveThreads, effectiveQuadDecimate, effectiveRefineEdges, effectiveRefineMode, effectiveAdvanced));
            if (driverMode) ManagerWrapper.Instance.SetDriverMode(sinkId, true);

            // a camera's active pipeline keeps what was tuned here, so activating another pipeline and coming back (or a restart) does not lose it
            foreach (int sourceId in SourceManager.Instance.GetAllSourceIds())
            {
                Source? owner = SourceManager.Instance.GetSourceById(sourceId);
                if (owner?.ActiveDetectionSinkId != sinkId) continue;
                PipelineProfile? active = owner.Profiles.FirstOrDefault(p => p.Index == owner.ActiveProfileIndex);
                if (active == null || active.Kind != DetectionSinkKind.ApriltagSink) continue;
                active.Backend = backend;
                active.Threads = effectiveThreads;
                active.QuadDecimate = effectiveQuadDecimate;
                active.RefineEdges = effectiveRefineEdges;
                active.RefineMode = effectiveRefineMode;
                active.Advanced = effectiveAdvanced == null || effectiveAdvanced.IsDefault ? null : effectiveAdvanced.Clone();
            }

            if (upstreamSourceId.HasValue) BindSourceToSink(sinkId, upstreamSourceId.Value);
            foreach (int downstreamId in downstreamSinkIds) BindSourceToSink(downstreamId, sinkId);
            if (wasRunning) EnableSinkById(sinkId);

            DB.Instance.Save();
        }

        // creates a WebRTCSink; bind it to a frame-producing node to stream that stage.
        // encoderName defaults to GetPreferredWebRTCEncoder().
        public int AddWebRTCSink(string name, int bitrateKbps = 4000, int fps = 30, string? encoderName = null)
        {
            int id = ManagerWrapper.Instance.CreateWebRTCSink(bitrateKbps, fps, encoderName ?? ManagerWrapper.Instance.GetPreferredWebRTCEncoder());
            sinks.Add(new Sink(id, name, SinkType.WebRTCSink));
            DB.Instance.Save();
            return id;
        }

        // creates an MjpegSink (fallback preview stream); jpegQuality uses cv::IMWRITE_JPEG_QUALITY's 0-100 scale
        public int AddMjpegSink(string name, int jpegQuality = 80)
        {
            int id = ManagerWrapper.Instance.CreateMjpegSink(jpegQuality);
            sinks.Add(new Sink(id, name, SinkType.MjpegSink));
            DB.Instance.Save();
            return id;
        }

        // Retunes a live MjpegSink and remembers the values. quality 1-100; scaleDivisor N streams 1/N of the width and height.
        public void SetMjpegSettings(int sinkId, int jpegQuality, int scaleDivisor)
        {
            Sink sink = RequireSink(sinkId, SinkType.MjpegSink);
            if (jpegQuality < 1 || jpegQuality > 100) throw Server.Web.ApiException.BadRequest("jpegQuality must be 1 to 100");
            if (scaleDivisor < 1 || scaleDivisor > 16) throw Server.Web.ApiException.BadRequest("scaleDivisor must be 1 to 16");
            ManagerWrapper.Instance.SetMjpegSinkSettings(sinkId, jpegQuality, scaleDivisor);
            sink.StreamJpegQuality = jpegQuality;
            sink.StreamScaleDivisor = scaleDivisor;
            DB.Instance.Save();
        }

        // Retunes a live WebRTCSink (its encoder restarts on the next frame) and remembers the values.
        public void SetWebRTCSettings(int sinkId, int bitrateKbps, int fps, int scaleDivisor)
        {
            Sink sink = RequireSink(sinkId, SinkType.WebRTCSink);
            if (bitrateKbps < 100 || bitrateKbps > 50000) throw Server.Web.ApiException.BadRequest("bitrateKbps must be 100 to 50000");
            if (fps < 1 || fps > 120) throw Server.Web.ApiException.BadRequest("fps must be 1 to 120");
            if (scaleDivisor < 1 || scaleDivisor > 16) throw Server.Web.ApiException.BadRequest("scaleDivisor must be 1 to 16");
            ManagerWrapper.Instance.SetWebRTCSinkSettings(sinkId, bitrateKbps, fps, scaleDivisor);
            sink.StreamBitrateKbps = bitrateKbps;
            sink.StreamFps = fps;
            sink.StreamScaleDivisor = scaleDivisor;
            DB.Instance.Save();
        }

        private Sink RequireSink(int sinkId, SinkType type)
        {
            Sink sink = GetSinkById(sinkId) ?? throw Server.Web.ApiException.NotFound($"no sink with id {sinkId}");
            if (sink.Type != type) throw Server.Web.ApiException.BadRequest($"sink {sinkId} is not a {type}");
            return sink;
        }

        // DB.Load() restore paths for the stream sinks: same id, saved tuning
        public int RestoreMjpegSink(Sink persisted)
        {
            int id = ManagerWrapper.Instance.CreateMjpegSink(persisted.Id, persisted.StreamJpegQuality ?? 80);
            sinks.Add(new Sink(id, persisted.Name, SinkType.MjpegSink)
            {
                StreamJpegQuality = persisted.StreamJpegQuality,
                StreamScaleDivisor = persisted.StreamScaleDivisor,
            });
            if (persisted.StreamScaleDivisor is int divisor && divisor > 1)
                ManagerWrapper.Instance.SetMjpegSinkSettings(id, persisted.StreamJpegQuality ?? 80, divisor);
            return id;
        }

        public int RestoreWebRTCSink(Sink persisted)
        {
            int bitrate = persisted.StreamBitrateKbps ?? 4000;
            int fps = persisted.StreamFps ?? 30;
            int id = ManagerWrapper.Instance.CreateWebRTCSink(persisted.Id, bitrate, fps, ManagerWrapper.Instance.GetPreferredWebRTCEncoder());
            sinks.Add(new Sink(id, persisted.Name, SinkType.WebRTCSink)
            {
                StreamBitrateKbps = persisted.StreamBitrateKbps,
                StreamFps = persisted.StreamFps,
                StreamScaleDivisor = persisted.StreamScaleDivisor,
            });
            if (persisted.StreamScaleDivisor is int divisor && divisor > 1)
                ManagerWrapper.Instance.SetWebRTCSinkSettings(id, bitrate, fps, divisor);
            return id;
        }

        public string GetMjpegFrameBase64(int sinkId) => ManagerWrapper.Instance.GetMjpegFrameBase64(sinkId);

        // creates a RecordSink; bind it to a frame-producing node to record segmented MP4 files plus a JSON-Lines telemetry sidecar.
        // dstFolder is relative to the working directory; defaults to a name-derived folder under "recordings/".
        public int AddRecordSink(string name, string? dstFolder = null, string? encoderName = null,
            int bitrateKbps = 8000, int fps = 30, int segmentSeconds = 300,
            long maxFolderSizeBytes = 0, int maxFileCount = 0)
        {
            string resolvedFolder = dstFolder ?? System.IO.Path.Combine("recordings", SanitizeFolderName(name));
            // GetPreferredWebRTCEncoder() returns the best H264 encoder this ffmpeg build has (h264_rkmpp or libx264); it is not WebRTC-specific.
            string resolvedEncoder = encoderName ?? ManagerWrapper.Instance.GetPreferredWebRTCEncoder();
            int id = ManagerWrapper.Instance.CreateRecordSink(resolvedFolder, resolvedEncoder, bitrateKbps, fps, segmentSeconds, maxFolderSizeBytes, maxFileCount);
            sinks.Add(new Sink(id, name, SinkType.RecordSink)
            {
                RecordDstFolder = resolvedFolder,
                RecordEncoderName = resolvedEncoder,
                RecordBitrateKbps = bitrateKbps,
                RecordSegmentSeconds = segmentSeconds,
                RecordMaxFolderSizeBytes = maxFolderSizeBytes,
                RecordMaxFileCount = maxFileCount,
            });
            DB.Instance.Save();
            return id;
        }

        // dstFolder is a filesystem path, so strip everything except alphanumerics, dash and underscore from the free-text name.
        private static string SanitizeFolderName(string name)
        {
            var sanitized = new System.Text.StringBuilder();
            foreach (char c in name)
            {
                sanitized.Append(char.IsLetterOrDigit(c) || c == '-' || c == '_' ? c : '_');
            }
            return sanitized.Length > 0 ? sanitized.ToString() : "sink";
        }

        // DB.Load() restore path: RecordSink's config (dstFolder/encoder/segment/retention) does not fit AddSink's signature
        private readonly object _recordAllLock = new();

        // Starts a RecordSink bound directly to every source (reusing an existing one, else creating "<source name>-match"), or stops all running RecordSinks.
        // Idempotent and serialised so the NT poller and REST calls cannot create duplicates. Returns the number of running RecordSinks.
        public int SetAllRecording(bool enabled)
        {
            lock (_recordAllLock)
            {
                if (!enabled)
                {
                    foreach (var sink in sinks.Where(s => s.Type == SinkType.RecordSink).ToList())
                    {
                        if (IsSinkRunning(sink.Id)) DisableSinkById(sink.Id);
                    }
                    DB.Instance.Save();
                    return 0;
                }

                int running = 0;
                foreach (int sourceId in SourceManager.Instance.GetAllSourceIds())
                {
                    Source source = SourceManager.Instance.GetSourceById(sourceId);
                    if (source == null) continue;
                    Sink? recordSink = sinks.FirstOrDefault(s => s.Type == SinkType.RecordSink && s.Source?.Id == sourceId);
                    int recordSinkId;
                    if (recordSink != null)
                    {
                        recordSinkId = recordSink.Id;
                    }
                    else
                    {
                        recordSinkId = AddRecordSink($"{source.Name}-match");
                        BindSourceToSink(recordSinkId, sourceId);
                    }
                    if (!IsSinkRunning(recordSinkId)) EnableSinkById(recordSinkId);
                    running++;
                }
                DB.Instance.Save();
                return running;
            }
        }

        public bool IsAnyRecording() =>
            sinks.Any(s => s.Type == SinkType.RecordSink && IsSinkRunning(s.Id));

        public int RestoreRecordSink(Sink persisted)
        {
            string dstFolder = persisted.RecordDstFolder ?? System.IO.Path.Combine("recordings", persisted.Id.ToString());
            int id = ManagerWrapper.Instance.CreateRecordSink(persisted.Id, dstFolder,
                persisted.RecordEncoderName ?? ManagerWrapper.Instance.GetPreferredWebRTCEncoder(), persisted.RecordBitrateKbps ?? 8000, 30,
                persisted.RecordSegmentSeconds ?? 300, persisted.RecordMaxFolderSizeBytes ?? 0,
                persisted.RecordMaxFileCount ?? 0);
            sinks.Add(new Sink(id, persisted.Name, SinkType.RecordSink)
            {
                RecordDstFolder = dstFolder,
                RecordEncoderName = persisted.RecordEncoderName,
                RecordBitrateKbps = persisted.RecordBitrateKbps,
                RecordSegmentSeconds = persisted.RecordSegmentSeconds,
                RecordMaxFolderSizeBytes = persisted.RecordMaxFolderSizeBytes,
                RecordMaxFileCount = persisted.RecordMaxFileCount,
            });
            return id;
        }

        // filenames only, newest first; resolved against this sink's RecordDstFolder, never a caller-supplied path
        public List<string> GetRecordSinkSegments(int sinkId) =>
            ManagerWrapper.Instance.GetRecordSinkSegments(sinkId).ToList();
        public bool DeleteRecordSinkSegment(int sinkId, string filename) =>
            ManagerWrapper.Instance.DeleteRecordSinkSegment(sinkId, filename);

        public string WebRTCCreateOffer(int sinkId) => ManagerWrapper.Instance.WebRTCCreateOffer(sinkId);
        public void WebRTCSetAnswer(int sinkId, string sdp) => ManagerWrapper.Instance.WebRTCSetAnswer(sinkId, sdp);
        public void WebRTCAddIceCandidate(int sinkId, string candidate, string mid) =>
            ManagerWrapper.Instance.WebRTCAddIceCandidate(sinkId, candidate, mid);
        public bool IsWebRTCSinkConnected(int sinkId) => ManagerWrapper.Instance.IsWebRTCSinkConnected(sinkId);
        public string GetWebRTCSinkStatus(int sinkId) => ManagerWrapper.Instance.GetWebRTCSinkStatus(sinkId);

        // creates an ObjectDetectionSink running an uploaded model; the provider is fixed by ModelManager.AddModel from the file extension
        public int AddObjectDetectionSink(string name, int modelId, float? confThreshold = null, float? nmsThreshold = null)
        {
            var model = ModelManager.Instance.GetModel(modelId);
            if (model == null) throw new ArgumentException($"no model with id {modelId}");

            int id = ManagerWrapper.Instance.CreateObjectDetectionSink(
                model.Provider, model.ModelPath, model.LabelsPath, model.Variant,
                confThreshold ?? model.ConfThreshold, nmsThreshold ?? model.NmsThreshold, model.InputSize);
            sinks.Add(new Sink(id, name, SinkType.ObjectDetectionSink)
            {
                ObjectDetectionModelId = modelId,
                ObjectDetectionConfThreshold = confThreshold,
                ObjectDetectionNmsThreshold = nmsThreshold,
            });
            DB.Instance.Save();
            return id;
        }

        // DB.Load() restore path: recreates the ObjectDetectionSink at the same id from its saved model; false when the record carries no
        // model (older data) or the model no longer exists, in which case the sink is dropped
        public bool RestoreObjectDetectionSink(Sink persisted)
        {
            if (!persisted.ObjectDetectionModelId.HasValue) return false;
            var model = ModelManager.Instance.GetModel(persisted.ObjectDetectionModelId.Value);
            if (model == null) return false;

            int id = ManagerWrapper.Instance.CreateObjectDetectionSink(persisted.Id, model.Provider, model.ModelPath, model.LabelsPath,
                model.Variant, persisted.ObjectDetectionConfThreshold ?? model.ConfThreshold, persisted.ObjectDetectionNmsThreshold ?? model.NmsThreshold,
                model.InputSize);
            sinks.Add(new Sink(id, persisted.Name, SinkType.ObjectDetectionSink)
            {
                ObjectDetectionModelId = model.Id,
                ObjectDetectionConfThreshold = persisted.ObjectDetectionConfThreshold,
                ObjectDetectionNmsThreshold = persisted.ObjectDetectionNmsThreshold,
            });
            return true;
        }

        // Retunes a running ObjectDetectionSink's model cutoffs (0.01-1) and remembers them on the sink, overriding its model's defaults.
        public void SetObjectDetectionThresholds(int sinkId, float confThreshold, float nmsThreshold)
        {
            Sink sink = RequireSink(sinkId, SinkType.ObjectDetectionSink);
            if (confThreshold < 0.01f || confThreshold > 1.0f) throw Server.Web.ApiException.BadRequest("confThreshold must be 0.01 to 1");
            if (nmsThreshold < 0.01f || nmsThreshold > 1.0f) throw Server.Web.ApiException.BadRequest("nmsThreshold must be 0.01 to 1");
            ManagerWrapper.Instance.SetObjectDetectionThresholds(sinkId, confThreshold, nmsThreshold);
            sink.ObjectDetectionConfThreshold = confThreshold;
            sink.ObjectDetectionNmsThreshold = nmsThreshold;
            DB.Instance.Save();
        }

        // After a model's default cutoffs changed: the detectors running on it follow, unless their own cutoffs were set on the sink.
        public void ApplyModelThresholds(Model model)
        {
            foreach (Sink sink in sinks.Where(s => s.Type == SinkType.ObjectDetectionSink && s.ObjectDetectionModelId == model.Id).ToList())
            {
                ManagerWrapper.Instance.SetObjectDetectionThresholds(sink.Id,
                    sink.ObjectDetectionConfThreshold ?? model.ConfThreshold, sink.ObjectDetectionNmsThreshold ?? model.NmsThreshold);
            }
        }

        public (float Conf, float Nms, int? ModelId) GetObjectDetectionThresholds(int sinkId)
        {
            Sink sink = RequireSink(sinkId, SinkType.ObjectDetectionSink);
            return (ManagerWrapper.Instance.GetObjectDetectionConfThreshold(sinkId), ManagerWrapper.Instance.GetObjectDetectionNmsThreshold(sinkId),
                sink.ObjectDetectionModelId);
        }

        // The field-layout file a detector was given: its own upload, or the active profile's layout of the camera it is the detector for.
        private string? FindFieldLayoutPath(int sinkId)
        {
            string own = System.IO.Path.Combine(AppContext.BaseDirectory, "fieldLayouts", $"sink-{sinkId}.json");
            if (System.IO.File.Exists(own)) return own;
            foreach (int sourceId in SourceManager.Instance.GetAllSourceIds())
            {
                Source? source = SourceManager.Instance.GetSourceById(sourceId);
                if (source?.ActiveDetectionSinkId != sinkId) continue;
                string? path = source.Profiles.FirstOrDefault(p => p.Index == source.ActiveProfileIndex)?.FieldLayoutPath;
                if (!string.IsNullOrEmpty(path) && System.IO.File.Exists(path)) return path;
            }
            return null;
        }

        // Copies a detector node: a new, stopped sink named "<name> copy" with the same configuration (tag size, backend, tuning, calibration,
        // field layout, driver mode, or the same model). With copyBindings it is bound to the same source as the original. Only the two
        // detector types are copyable; the stream/record/NT outputs hang off nodes as badges and the stereo nodes need their own calibration.
        public int DuplicateSink(int sinkId, bool copyBindings)
        {
            Sink original = GetSinkById(sinkId) ?? throw Server.Web.ApiException.NotFound($"no sink with id {sinkId}");
            string name = original.Name + " copy";
            int newId;

            switch (original.Type)
            {
                case SinkType.ApriltagSink:
                {
                    double tagSize = ManagerWrapper.Instance.GetApriltagDetectorTagSize(sinkId);
                    CameraCalibrationResult calibration = ManagerWrapper.Instance.GetApriltagDetectorCalibration(sinkId);
                    ApriltagBackendKind backend = original.ApriltagBackend ?? ApriltagBackendKind.APRILTAG_BACKEND_CPU;
                    int nthreads = original.ApriltagThreads ?? ManagerWrapper.Instance.GetApriltagDetectorThreads(sinkId);
                    float quadDecimate = original.ApriltagQuadDecimate ?? ManagerWrapper.Instance.GetApriltagDetectorQuadDecimate(sinkId);
                    bool refineEdges = original.ApriltagRefineEdges ?? ManagerWrapper.Instance.GetApriltagDetectorRefineEdges(sinkId);
                    RefineEdgesMode refineMode = original.ApriltagRefineMode ?? ManagerWrapper.Instance.GetApriltagDetectorRefineMode(sinkId);

                    newId = ManagerWrapper.Instance.CreateApriltagDetector(calibration, tagSize, backend, 0, 0,
                        MakeTuning(nthreads, quadDecimate, refineEdges, refineMode, original.ApriltagAdvanced));
                    sinks.Add(NewApriltagSinkRecord(newId, name, tagSize, backend, nthreads, quadDecimate, refineEdges, refineMode, original.ApriltagAdvanced));

                    string? layout = FindFieldLayoutPath(sinkId);
                    if (layout != null)
                    {
                        // a private copy, so editing one node's layout never changes the other's
                        string copy = System.IO.Path.Combine(AppContext.BaseDirectory, "fieldLayouts", $"sink-{newId}.json");
                        System.IO.Directory.CreateDirectory(System.IO.Path.GetDirectoryName(copy)!);
                        System.IO.File.Copy(layout, copy, true);
                        ManagerWrapper.Instance.LoadFieldLayout(newId, copy);
                    }
                    if (ManagerWrapper.Instance.GetDriverMode(sinkId)) ManagerWrapper.Instance.SetDriverMode(newId, true);
                    break;
                }
                case SinkType.ObjectDetectionSink:
                    if (!original.ObjectDetectionModelId.HasValue)
                        throw Server.Web.ApiException.BadRequest("this object detection node predates model tracking and cannot be copied; create it again");
                    newId = AddObjectDetectionSink(name, original.ObjectDetectionModelId.Value,
                        original.ObjectDetectionConfThreshold, original.ObjectDetectionNmsThreshold);
                    break;
                default:
                    throw Server.Web.ApiException.BadRequest($"{original.Type} nodes cannot be copied");
            }

            if (copyBindings && original.Source != null) BindSourceToSink(newId, original.Source.Id);
            DB.Instance.Save();
            return newId;
        }

        // which backend an existing ObjectDetectionSink runs (read-only)
        public string GetObjectDetectionSinkBackendName(int sinkId) => ManagerWrapper.Instance.GetObjectDetectionSinkBackendName(sinkId);

        // (re)creates the detection sink a PipelineProfile describes: construction-time settings at creation, the rest via setters.
        // explicitId creates it at that exact id (used by SourceManager.ActivateProfile).
        // An AprilTag profile gets the saved calibration of the source's camera at its current resolution, if there is one.
        public int CreateOrReplaceDetectionSinkForProfile(string name, PipelineProfile profile, int? explicitId, int sourceId)
        {
            int id;
            switch (profile.Kind)
            {
                case DetectionSinkKind.ApriltagSink:
                    CameraCalibrationResult calibration = CalibrationManager.Instance.GetForSource(sourceId)
                        ?? new CameraCalibrationResult();
                    double tagSize = profile.TagSize ?? 0.1651;
                    ApriltagBackendKind backend = profile.Backend ?? ApriltagBackendKind.APRILTAG_BACKEND_CPU;
                    int nthreads = profile.Threads ?? 0;
                    float quadDecimate = profile.QuadDecimate ?? 0.0f;
                    bool refineEdges = profile.RefineEdges ?? true;
                    RefineEdgesMode refineMode = profile.RefineMode ?? RefineEdgesMode.REFINE_EXACT;
                    ApriltagTuning tuning = MakeTuning(nthreads, quadDecimate, refineEdges, refineMode, profile.Advanced);
                    id = explicitId.HasValue
                        ? ManagerWrapper.Instance.CreateApriltagDetector(explicitId.Value, calibration, tagSize,
                            backend, profile.FrameWidth, profile.FrameHeight, tuning)
                        : ManagerWrapper.Instance.CreateApriltagDetector(calibration, tagSize,
                            backend, profile.FrameWidth, profile.FrameHeight, tuning);
                    sinks.Add(NewApriltagSinkRecord(id, name, tagSize, backend, nthreads, quadDecimate, refineEdges, refineMode, profile.Advanced));

                    if (!string.IsNullOrEmpty(profile.FieldLayoutPath))
                        ManagerWrapper.Instance.LoadFieldLayout(id, profile.FieldLayoutPath);
                    ManagerWrapper.Instance.SetDriverMode(id, profile.DriverMode);
                    break;

                case DetectionSinkKind.ObjectDetectionSink:
                    if (!profile.ModelId.HasValue)
                        throw new ArgumentException("ObjectDetectionSink profile has no ModelId set");
                    var model = ModelManager.Instance.GetModel(profile.ModelId.Value);
                    if (model == null) throw new ArgumentException($"no model with id {profile.ModelId.Value}");

                    float conf = profile.ConfThreshold ?? model.ConfThreshold;
                    float nms = profile.NmsThreshold ?? model.NmsThreshold;
                    id = explicitId.HasValue
                        ? ManagerWrapper.Instance.CreateObjectDetectionSink(explicitId.Value, model.Provider,
                            model.ModelPath, model.LabelsPath, model.Variant, conf, nms, model.InputSize)
                        : ManagerWrapper.Instance.CreateObjectDetectionSink(model.Provider,
                            model.ModelPath, model.LabelsPath, model.Variant, conf, nms, model.InputSize);
                    sinks.Add(new Sink(id, name, SinkType.ObjectDetectionSink)
                    {
                        ObjectDetectionModelId = model.Id,
                        ObjectDetectionConfThreshold = profile.ConfThreshold,
                        ObjectDetectionNmsThreshold = profile.NmsThreshold,
                    });
                    break;

                default:
                    throw new ArgumentException($"unknown pipeline profile kind {profile.Kind}");
            }

            DB.Instance.Save();
            return id;
        }

        // every sink bound (as its source) to sourceId; used by SourceManager.ActivateProfile to rebind downstream sinks
        public List<int> GetSinksBoundToSource(int sourceId)
        {
            return sinks.Where(s => s.Source != null && s.Source.Id == sourceId).Select(s => s.Id).ToList();
        }

        // creates a NetworkTablesSink that connects to a server via team number (e.g. 1234 ->
        // roboRIO mDNS/static IP resolution, exactly like a real driver station)
        public int AddNetworkTablesSinkForTeam(string name, int teamNumber, string rootTable, string clientIdentity)
        {
            int id = ManagerWrapper.Instance.CreateNetworkTablesSinkForTeam(teamNumber, rootTable, clientIdentity);
            sinks.Add(new Sink(id, name, SinkType.NetworkTablesSink));
            DB.Instance.Save();
            return id;
        }

        // creates a NetworkTablesSink that connects to an explicit server address - useful for
        // bench testing against a local NT4 server/Glass instance instead of a real robot
        public int AddNetworkTablesSinkForServer(string name, string serverAddress, int port, string rootTable, string clientIdentity)
        {
            int id = ManagerWrapper.Instance.CreateNetworkTablesSinkForServer(serverAddress, port, rootTable, clientIdentity);
            sinks.Add(new Sink(id, name, SinkType.NetworkTablesSink));
            DB.Instance.Save();
            return id;
        }

        // The native NetworkTablesSink for the device's saved NT settings (DeviceSettings), created under `id` when given.
        private int CreateNativeNetworkTablesSink(int? id)
        {
            NetworkTablesSettings nt = DeviceSettings.Instance.Data.NetworkTables;
            if (nt.Mode == "server")
            {
                return id.HasValue
                    ? ManagerWrapper.Instance.CreateNetworkTablesSinkForServer(id.Value, nt.ServerAddress ?? "", nt.Port, nt.RootTable, nt.ClientIdentity)
                    : ManagerWrapper.Instance.CreateNetworkTablesSinkForServer(nt.ServerAddress ?? "", nt.Port, nt.RootTable, nt.ClientIdentity);
            }
            if (nt.TeamNumber is not int team) throw Server.Web.ApiException.BadRequest("set a NetworkTables team number or server address in Settings first");
            return id.HasValue
                ? ManagerWrapper.Instance.CreateNetworkTablesSinkForTeam(id.Value, team, nt.RootTable, nt.ClientIdentity)
                : ManagerWrapper.Instance.CreateNetworkTablesSinkForTeam(team, nt.RootTable, nt.ClientIdentity);
        }

        // creates a NetworkTablesSink that connects the way the device's Settings say
        public int AddNetworkTablesSinkFromSettings(string name)
        {
            int id = CreateNativeNetworkTablesSink(null);
            sinks.Add(new Sink(id, name, SinkType.NetworkTablesSink));
            DB.Instance.Save();
            return id;
        }

        // DB.Load() restore path: the same id, connecting the way the saved Settings say
        public int RestoreNetworkTablesSink(Sink persisted)
        {
            int id = CreateNativeNetworkTablesSink(persisted.Id);
            sinks.Add(new Sink(id, persisted.Name, SinkType.NetworkTablesSink));
            return id;
        }

        // After the NT settings changed: rebuilds every NetworkTablesSink at its own id so they connect the new way, keeping each one's source binding
        // and whether it was running.
        public void ReapplyNetworkTablesSettings()
        {
            foreach (Sink sink in sinks.Where(s => s.Type == SinkType.NetworkTablesSink).ToList())
            {
                bool wasRunning = IsSinkRunning(sink.Id);
                int? sourceId = sink.Source?.Id;
                ManagerWrapper.Instance.DeleteSink(sink.Id);
                try
                {
                    CreateNativeNetworkTablesSink(sink.Id);
                }
                catch (Exception ex)
                {
                    // settings that cannot build a sink (no team number yet): drop the node's record instead of leaving a dead one
                    Console.WriteLine($"NetworkTablesSink {sink.Id} not rebuilt: {ex.Message}");
                    sinks.Remove(sink);
                    continue;
                }
                if (sourceId.HasValue) ManagerWrapper.Instance.BindSourceToSink(sourceId.Value, sink.Id);
                if (wasRunning) EnableSinkById(sink.Id);
                // the rebuilt sink has no camera aliases yet; the control service applied them to the old one
                NetworkTablesControlService.ForgetSink(sink.Id);
            }
            DB.Instance.Save();
        }

        public bool IsNetworkTablesSinkConnected(int sinkId)
        {
            return ManagerWrapper.Instance.IsNetworkTablesSinkConnected(sinkId);
        }

        public string GetNetworkTablesSinkStatus(int sinkId)
        {
            return ManagerWrapper.Instance.GetNetworkTablesSinkStatus(sinkId);
        }

        // --- Stereo depth ---

        // binds the explicit left/right roles of a stereo sink; BindSourceToSink is bind-order only, and swapping them flips the disparity sign
        public void BindStereoSourcesToSink(int sinkId, int leftSourceId, int rightSourceId)
        {
            var sink = sinks.FirstOrDefault(s => s.Id == sinkId);
            if (sink == null) throw new ArgumentException($"no sink with id {sinkId}");

            Source ResolveSource(int sourceId)
            {
                Source? s = SourceManager.Instance.GetSourceById(sourceId);
                if (s == null) {
                    // dual-role sink acting as its own source (see BindSourceToSink)
                    var sourceSink = sinks.FirstOrDefault(sk => sk.Id == sourceId && DualRoleSinkTypes.Contains(sk.Type));
                    if (sourceSink != null) s = new Source(sourceSink.Id, sourceSink.Name, SourceType.SinkOutput);
                }
                if (s == null) throw new Exception($"no source (or dual-role sink) with id {sourceId}");
                return s;
            }

            Source left = ResolveSource(leftSourceId);
            Source right = ResolveSource(rightSourceId);

            bool ok = ManagerWrapper.Instance.BindStereoSources(sinkId, leftSourceId, rightSourceId);
            if (!ok) throw new Exception($"BindStereoSources failed for sink {sinkId}");

            sink.Source = left;
            sink.Source2 = right;

            // only a SourceManager-tracked source has an enable/disable lifecycle to start here
            if (SourceManager.Instance.GetSourceById(leftSourceId) != null) SourceManager.Instance.EnableSourceById(leftSourceId);
            if (SourceManager.Instance.GetSourceById(rightSourceId) != null) SourceManager.Instance.EnableSourceById(rightSourceId);

            DB.Instance.Save();
        }

        // creates a StereoDepthNode with nothing bound; bind its left/right sources with BindStereoSourcesToSink.
        // `calibration` is a saved stereo calibration result.
        public int AddStereoDepthSink(string name, StereoDepthBackendKind backend, StereoCalibrationResult calibration,
            double minDepthMeters, double maxDepthMeters, int maxSkewUs, StereoFrameOutput frameOutput)
        {
            int id = ManagerWrapper.Instance.CreateStereoDepthNode(backend, calibration, minDepthMeters, maxDepthMeters, maxSkewUs, frameOutput);
            sinks.Add(new Sink(id, name, SinkType.StereoDepthSink));
            DB.Instance.Save();
            return id;
        }

        public string GetStereoDepthBackendName(int sinkId) => ManagerWrapper.Instance.GetStereoDepthBackendName(sinkId);
        public double GetStereoDepthValidFraction(int sinkId) => ManagerWrapper.Instance.GetStereoDepthValidFraction(sinkId);
        public double GetStereoDepthMedianDepthMeters(int sinkId) => ManagerWrapper.Instance.GetStereoDepthMedianDepthMeters(sinkId);

        // creates a DepthFusionNode; bind the detector with BindSourceToSink (it must consume the StereoDepthSink's rectified-left output),
        // then attach the depth source with AttachDepthFusionSource.
        public int AddDepthFusionSink(string name)
        {
            int id = ManagerWrapper.Instance.CreateDepthFusionNode();
            sinks.Add(new Sink(id, name, SinkType.DepthFusionSink));
            DB.Instance.Save();
            return id;
        }

        // attaches the StereoDepthSink whose depth grid a DepthFusionNode reads through a direct C++ reference (not serialised via SourceResult)
        public void AttachDepthFusionSource(int fusionSinkId, int stereoDepthSinkId)
        {
            bool ok = ManagerWrapper.Instance.SetDepthFusionDepthNode(fusionSinkId, stereoDepthSinkId);
            if (!ok) throw new Exception($"AttachDepthFusionSource failed for fusion sink {fusionSinkId} / depth sink {stereoDepthSinkId}");

            var sink = sinks.FirstOrDefault(s => s.Id == fusionSinkId);
            if (sink != null)
            {
                sink.DepthSourceId = stereoDepthSinkId;
                DB.Instance.Save();
            }
        }

        // stop sink by id
        public void DisableSinkById(int id)
        {
            Source source = null;
            foreach (var sink in sinks)
            {
                if (sink.Id == id)
                {
                    if (sink.Source != null)
                    {
                        source = sink.Source;
                    }

                    ManagerWrapper.Instance.StopSinkById(id);
                    break;
                }
            }

            if (source != null)
            {
                bool isSourceUsed = false;
                foreach (var s in sinks)
                {
                    if (s.Source != null && s.Source.Id == source.Id)
                    {
                        isSourceUsed = true;
                        break;
                    }
                }

                if (!isSourceUsed)
                {
                    SourceManager.Instance.DisableSourceById(source.Id);
                }
            }
        }

        // start sink by id
        public void EnableSinkById(int id)
        {
            foreach (var sink in sinks)
            {
                if (sink.Id == id)
                {
                    if (sink.Source != null)
                    {
                        SourceManager.Instance.EnableSourceById(sink.Source.Id);
                    }

                    ManagerWrapper.Instance.StartSinkById(id);
                    return;
                }
            }
        }

        public void EnableAllSinks()
        {
            foreach (var sink in sinks)
            {
                if (sink.Source != null)
                {
                    SourceManager.Instance.EnableSourceById(sink.Source.Id);
                }

                ManagerWrapper.Instance.StartSinkById(sink.Id);
            }
        }

        public void UnbindSourceFromSink(int sinkId, int? sourceId = null)
        {
            foreach (var sink in sinks)
            {
                if (sink.Id == sinkId)
                {
                    ManagerWrapper.Instance.UnbindSourceFromSink(sinkId);
                    sink.Source = null; // Unbind the source
                    DB.Instance.Save();
                    break;
                }
            }
        }

        // ApriltagSink and ObjectDetectionSink are dual-role (natively both sink and source), so their id is a valid bind target,
        // but SourceManager's C# source list does not track them, so the source lookup yields null.
        private static readonly HashSet<SinkType> DualRoleSinkTypes = new HashSet<SinkType> {
            SinkType.ApriltagSink, SinkType.ObjectDetectionSink, SinkType.StereoDepthSink, SinkType.DepthFusionSink
        };

        public void BindSourceToSink(int sinkId, int sourceId)
        {
            foreach (var sink in sinks)
            {
                if (sink.Id == sinkId)
                {
                    Source? source = SourceManager.Instance.GetSourceById(sourceId);
                    bool isRealSource = source != null;
                    if (source == null)
                    {
                        var sourceSink = sinks.FirstOrDefault(s => s.Id == sourceId && DualRoleSinkTypes.Contains(s.Type));
                        if (sourceSink != null) source = new Source(sourceSink.Id, sourceSink.Name, SourceType.SinkOutput);
                    }
                    if (source == null) throw new Exception($"no source (or dual-role sink) with id {sourceId}");

                    sink.Source = source;
                    ManagerWrapper.Instance.BindSourceToSink(sourceId, sinkId);
                    // Only a SourceManager-tracked source has an enable/disable lifecycle here; a dual-role sink is toggled via EnableSinkById.
                    if (isRealSource) SourceManager.Instance.EnableSourceById(source.Id);
                    DB.Instance.Save();
                    break;
                }
            }
        }

        public bool IsSinkRunning(int id)
        {
            return ManagerWrapper.Instance.IsSinkActive(id);
        }
    }
}
