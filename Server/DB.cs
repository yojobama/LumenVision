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
            // detection nodes switched off, so they stay off after a restart (see SinkManager.SetSinkEnabled)
            public List<int>? DisabledNodes { get; set; }
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
                Sources = sources,
                DisabledNodes = SinkManager.Instance.GetDisabledNodeIds()
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

        // A configuration saved with per-camera pipeline profiles is rewritten once, keeping a copy of the original beside it.
        private string MigrateProfiles(string jsonData)
        {
            if (!NodeMigration.Needed(jsonData)) return jsonData;

            File.Copy(jsonPath, jsonPath + ".before-nodes", overwrite: true);
            NodeMigration.Result migrated = NodeMigration.Migrate(jsonData, () => Random.Shared.Next(1, int.MaxValue));
            foreach (NodeMigration.LayoutCopy copy in migrated.LayoutCopies)
            {
                try
                {
                    string target = Path.Combine(AppContext.BaseDirectory, "fieldLayouts", $"sink-{copy.NodeId}.json");
                    Directory.CreateDirectory(Path.GetDirectoryName(target)!);
                    File.Copy(copy.FromPath, target, overwrite: true);
                }
                catch (Exception ex)
                {
                    logger.EnterLog($"Field layout {copy.FromPath} not carried over to node {copy.NodeId}: {ex.Message}");
                }
            }
            foreach (string note in migrated.Notes) logger.EnterLog("Pipeline migration: " + note);
            File.WriteAllText(jsonPath, migrated.Json);
            logger.EnterLog("Pipeline profiles migrated to detector nodes; the original is kept as " + jsonPath + ".before-nodes");
            return migrated.Json;
        }

        private void LoadInternal()
        {
            if (File.Exists(jsonPath))
            {
                string jsonData = File.ReadAllText(jsonPath);
                try {
                    jsonData = MigrateProfiles(jsonData);
                    var dbData = JsonSerializer.Deserialize<DBData>(jsonData);

                    if (dbData != null)
                    {
                        // calibration is no longer a graph node; records of the old calibration sinks (types 3 and 6) are dropped
                        sinks = (dbData.Sinks ?? new List<Sink>())
                            .Where(s => s.Type != SinkType.CameraCalibrationSink && s.Type != SinkType.StereoCalibrationSink)
                            .ToList();
                        sources = dbData.Sources ?? new List<Source>();
                        SinkManager.Instance.RestoreDisabledNodes(dbData.DisabledNodes ?? new List<int>());
                    }

                    // crops last: each is cut from a source that has to exist (and be in its saved mode) by then
                    foreach (var source in sources.OrderBy(s => s.Type == SourceType.Crop ? 1 : 0).ToList())
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
                                SourceManager.Instance.InitializeCameraSource(source.CameraHardwareInfo, source.Name, source.Id);
                                break;
                            case SourceType.Crop:
                                try
                                {
                                    if (source.CropParentId.HasValue)
                                        SourceManager.Instance.InitializeCropSource(source.CropParentId.Value, source.Name, source.CropLayout, source.CropRegion, source.Id);
                                }
                                catch (Exception ex)
                                {
                                    Console.WriteLine($"Restoring crop source {source.Id} failed: {ex.Message}");
                                    logger.EnterLog($"Crop source {source.Id} ({source.Name}) not restored: {ex.Message}");
                                }
                                break;
                        }

                        // InitializeXxxSource builds its own Source object, so the saved fields on this loop's `source` must be copied across explicitly
                        Source restored = SourceManager.Instance.GetSourceById(source.Id);
                        if (restored != null)
                        {
                            if (source.FpsLimit.HasValue) SourceManager.Instance.SetFpsLimit(restored.Id, source.FpsLimit.Value);
                            restored.ControlValues = source.ControlValues;
                            restored.Transform = source.Transform;
                            restored.Mode = source.Mode;
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
                        if (sink.Type != SinkType.WebRTCSink && !SinkManager.Instance.IsNodeDisabled(sink.Id))
                        {
                            SinkManager.Instance.EnableSinkById(sink.Id);
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
                Sources = sources,
                DisabledNodes = SinkManager.Instance.GetDisabledNodeIds()
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
            SinkManager.Instance.DeleteOrphanPreviews();

            foreach (var sink in sinks)
            {
                if (SinkManager.Instance.GetSinkById(sink.Id) == null)
                {
                    SinkManager.Instance.AddSink(sink.Name, sink.Type.ToString());
                }
            }

            foreach (var source in sources)
            {
                // a crop that could not be rebuilt (its camera is gone) must not come back as a camera
                if (source.Type == SourceType.Crop) continue;
                if (SourceManager.Instance.GetSourceById(source.Id) == null)
                {
                    SourceManager.Instance.InitializeCameraSource(new CameraHardwareInfo { name = source.Name, path = "" });
                }
            }
        }
    }
}
