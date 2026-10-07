using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;

namespace Server
{
    public class DB
    {
        private class DBData
        {
            public List<Sink> Sinks { get; set; }
            public List<Source> Sources { get; set; }
        }

        public static DB Instance { get; } = new DB("data.json");

        private string jsonPath;
        private List<Sink> sinks;
        private List<Source> sources;
        private readonly Logger logger;

        // Save() is a no-op while Load() runs: a mid-load Save() would serialise the partly reconstructed live state
        // (no sinks yet) over data.json.
        private bool m_Loading = false;

        private DB(string jsonPath)
        {
            this.jsonPath = jsonPath;
            sinks = new List<Sink>();
            sources = new List<Source>();
            logger = new Logger("DBLog.txt");
        }

        public string GetJson()
        {
            var dbData = new DBData
            {
                Sinks = sinks,
                Sources = sources
            };
            return JsonSerializer.Serialize(dbData, new JsonSerializerOptions { WriteIndented = true });
        }

        public void Load()
        {
            logger.EnterLog("DB Load called");
            m_Loading = true;
            try
            {
                LoadInternal();
            }
            finally
            {
                m_Loading = false;
            }
        }

        private void LoadInternal()
        {
            if (File.Exists(jsonPath))
            {
                string jsonData = File.ReadAllText(jsonPath);
                try {
                    var dbData = JsonSerializer.Deserialize<DBData>(jsonData);

                    if (dbData != null)
                    {
                        // calibration is no longer a graph node; records of the old calibration sinks (types 3 and 6) are dropped
                        sinks = (dbData.Sinks ?? new List<Sink>())
                            .Where(s => s.Type != SinkType.CameraCalibrationSink && s.Type != SinkType.StereoCalibrationSink)
                            .ToList();
                        sources = dbData.Sources ?? new List<Source>();
                    }

                    foreach (var source in sources)
                    {
                        switch (source.Type)
                        {
                            case SourceType.ImageFile:
                                SourceManager.Instance.initializeImageFileSource(source.FilePath, source.Name, source.Id);
                                break;
                            case SourceType.VideoFile:
                                SourceManager.Instance.InitializeVideoFileSource(source.FilePath, source.Fps ?? 30, source.Name, source.Id);
                                break;
                            case SourceType.Camera:
                                SourceManager.Instance.InitializeCameraSource(source.CameraHardwareInfo, id: source.Id);
                                break;
                        }

                        // InitializeXxxSource builds its own Source object, so the pipeline-profile fields on this loop's `source` must be copied across explicitly
                        Source restored = SourceManager.Instance.GetSourceById(source.Id);
                        if (restored != null)
                        {
                            restored.Profiles = source.Profiles ?? new List<PipelineProfile>();
                            restored.ActiveProfileIndex = source.ActiveProfileIndex;
                            restored.ActiveDetectionSinkId = source.ActiveDetectionSinkId;
                            if (source.FpsLimit.HasValue) SourceManager.Instance.SetFpsLimit(restored.Id, source.FpsLimit.Value);
                            restored.ControlValues = source.ControlValues;
                            restored.Transform = source.Transform;
                            if (source.Type == SourceType.Camera) SourceManager.Instance.ApplyCameraControls(restored.Id);
                        }
                    }
                    foreach (var sink in sinks)
                    {
                        // an ApriltagSink saved with its configuration (tag size, requested backend, tuning) is restored as configured;
                        // records without it use the generic path's defaults - see RestoreApriltagSink
                        if (sink.Type == SinkType.ApriltagSink && sink.ApriltagTagSize.HasValue)
                            SinkManager.Instance.RestoreApriltagSink(sink);
                        else if (sink.Type == SinkType.ObjectDetectionSink)
                            SinkManager.Instance.RestoreObjectDetectionSink(sink);
                        else if (sink.Type == SinkType.NetworkTablesSink)
                        {
                            try { SinkManager.Instance.RestoreNetworkTablesSink(sink); }
                            catch (Exception ex) { logger.EnterLog($"NetworkTables sink {sink.Id} not restored: {ex.Message}"); }
                        }
                        else if (sink.Type == SinkType.MjpegSink)
                            SinkManager.Instance.RestoreMjpegSink(sink);
                        else if (sink.Type == SinkType.WebRTCSink)
                        {
                            try { SinkManager.Instance.RestoreWebRTCSink(sink); }
                            catch (Exception ex) { logger.EnterLog($"WebRTC sink {sink.Id} not restored: {ex.Message}"); }
                        }
                        else
                            SinkManager.Instance.AddSink(sink.Name, sink.Type.ToString(), sink.Id);
                    }

                    // AddSink's generic switch cannot carry RecordSink's config, so RestoreRecordSink reads the persisted Sink directly
                    foreach (var sink in sinks)
                    {
                        if (sink.Type == SinkType.RecordSink)
                        {
                            SinkManager.Instance.RestoreRecordSink(sink);
                        }
                    }

                    // AddSink only recreates the native node; the source->sink bindings are restored here (sources are created above)
                    foreach (var sink in sinks)
                    {
                        if (sink.Source != null && sink.Source2 != null)
                        {
                            // stereo sink (StereoDepthSink): Source is the left role, Source2 the right
                            SinkManager.Instance.BindStereoSourcesToSink(sink.Id, sink.Source.Id, sink.Source2.Id);
                        }
                        else if (sink.Source != null)
                        {
                            SinkManager.Instance.BindSourceToSink(sink.Id, sink.Source.Id);
                        }

                        if (sink.DepthSourceId.HasValue)
                        {
                            SinkManager.Instance.AttachDepthFusionSource(sink.Id, sink.DepthSourceId.Value);
                        }
                    }

                    // Start the sinks meant to run unattended after a restart. Excluded: WebRTCSink
                    // (its encoder runs on every frame even with no peer). StartSinkById also starts a sink's bound source; RecordSink is included so recordings resume.
                    foreach (var sink in sinks)
                    {
                        if (sink.Type != SinkType.WebRTCSink)
                        {
                            SinkManager.Instance.EnableSinkById(sink.Id);
                        }
                    }

                    // Recreate each source's profile detection sink via ActivateProfile so tag size, calibration, field layout and driver mode
                    // are applied (AddSink only restores defaults), using the same rebinding logic as a live profile switch.
                    foreach (var source in sources)
                    {
                        if (source.ActiveProfileIndex >= 0 && source.Profiles.Any(p => p.Index == source.ActiveProfileIndex))
                        {
                            try
                            {
                                SourceManager.Instance.ActivateProfile(source.Id, source.ActiveProfileIndex);
                            }
                            catch (Exception ex)
                            {
                                logger.EnterLog($"Failed to reactivate pipeline profile {source.ActiveProfileIndex} for source {source.Id}: {ex.Message}");
                            }
                        }
                    }

                    logger.EnterLog("DB loaded successfully from " + jsonPath);
                }
                catch (Exception ex)
                {
                    logger.EnterLog("Error loading DB: " + ex.Message);
                }
            }
            else
            {
                logger.EnterLog("DB file not found: " + jsonPath);
            }
        }

        // set by a factory reset: the files were just deleted and the server is about to restart, so nothing may write the live state back
        private volatile bool m_SavesSuspended = false;

        public void SuspendSaves() => m_SavesSuspended = true;

        public void Save()
        {
            // Save() is skipped during Load() (see m_Loading): live state is still partly reconstructed
            if (m_Loading || m_SavesSuspended) return;

            logger.EnterLog("DB Save called");
            List<Sink> sinks = new List<Sink>();
            List<Source> sources = new List<Source>();

            foreach (var sink in SinkManager.Instance.getAllSinkIds())
            {
                sinks.Add(SinkManager.Instance.GetSinkById(sink));
            }
            foreach (var source in SourceManager.Instance.GetAllSourceIds())
            {
                sources.Add(SourceManager.Instance.GetSourceById(source));
            }

            var dbData = new DBData
            {
                Sinks = sinks,
                Sources = sources
            };

            string jsonData = JsonSerializer.Serialize(dbData, new JsonSerializerOptions { WriteIndented = true });
            File.WriteAllText(jsonPath, jsonData);
            logger.EnterLog("DB saved successfully to " + jsonPath);
        }

        public void RemoveSink(Sink sink)
        {
            sinks.Remove(sink);
            Save();
        }

        public void RemoveSource(Source source)
        {
            sources.Remove(source);
            Save();
        }

        public List<Sink> GetSinks()
        {
            return sinks;
        }

        public List<Source> GetSources()
        {
            return sources;
        }

        public void Verify()
        {
            foreach (var sink in sinks)
            {
                if (SinkManager.Instance.GetSinkById(sink.Id) == null)
                {
                    SinkManager.Instance.AddSink(sink.Name, sink.Type.ToString());
                }
            }

            foreach (var source in sources)
            {
                if (SourceManager.Instance.GetSourceById(source.Id) == null)
                {
                    SourceManager.Instance.InitializeCameraSource(new CameraHardwareInfo { name = source.Name, path = "" });
                }
            }
        }
    }
}
