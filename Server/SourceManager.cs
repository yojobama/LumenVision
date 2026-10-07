using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml.Linq;

namespace Server
{
    public class SourceManager
    {
        public static SourceManager Instance { get; } = new SourceManager();

        private List<Source> sources;

        private SourceManager()
        {
            sources = new List<Source>();
        }

        public void ChangeSourceName(int sourceId, string newName)
        {
            Source source = GetSourceById(sourceId);
            if (source != null)
            {
                source.Name = newName;
                DB.Instance.Save();
            }
        }

        public int[] GetAllSourceIds()
        {
            List<int> ids = new List<int>();
            foreach (var source in sources)
            {
                ids.Add(source.Id);
            }
            return ids.ToArray();
        }

        public void DisableAllSources()
        {
            foreach (var source in sources)
            {
                ManagerWrapper.Instance.StopSourceById(source.Id);
            }
        }
        public void EnableAllSources()
        {
            foreach (var source in sources)
            {
                ManagerWrapper.Instance.StartSourceById(source.Id);
            }
        }

        public void DisableSourceById(int id)
        {
            foreach (var source in sources)
            {
                if (source.Id == id)
                {
                    ManagerWrapper.Instance.StopSourceById(source.Id);
                    return;
                }
            }
        }
        public void EnableSourceById(int id)
        {
            foreach (var source in sources)
            {
                if (source.Id == id)
                {
                    if (!ManagerWrapper.Instance.StartSourceById(source.Id)) throw new Exception("unable to start source");
                    return;
                }
            }
        }

        public Source GetSourceById(int id)
        {
            foreach (var source in sources)
            {
                if (source.Id == id)
                {
                    return source;
                }
            }
            return null;
        }

        public int InitializeCameraSource(CameraHardwareInfo cameraHardwareInfo, string name = "default", int? id = null)
        {
            int sourceId = -1;

            if (id.HasValue)
                sourceId = ManagerWrapper.Instance.CreateCameraSource(cameraHardwareInfo, id.Value);
            else
                sourceId = ManagerWrapper.Instance.CreateCameraSource(cameraHardwareInfo);

            sources.Add(new Source(sourceId, name, SourceType.Camera, cameraHardwareInfo: cameraHardwareInfo));
            DB.Instance.Save();
            return sourceId;
        }

        public int InitializeVideoFileSource(string filePath, int fps = 30, string name = "default", int? id = null)
        {
            int sourceId = -1;

            if (id.HasValue)
                sourceId = ManagerWrapper.Instance.CreateVideoFileSource(filePath, fps, id.Value);
            else
                sourceId = ManagerWrapper.Instance.CreateVideoFileSource(filePath, fps);
            
            sources.Add(new Source(sourceId, name, SourceType.VideoFile, filePath, fps));
            DB.Instance.Save();
            return sourceId;
        }
        
        public int initializeImageFileSource(string filePath, string name = "default", int? id = null)
        {
            int sourceId = -1;
            
            if (id.HasValue)
                sourceId = ManagerWrapper.Instance.CreateImageFileSource(filePath, id.Value);
            else
                sourceId = ManagerWrapper.Instance.CreateImageFileSource(filePath);
            
            sources.Add(new Source(sourceId, name, SourceType.ImageFile, filePath));
            DB.Instance.Save();
            return sourceId;
        }

        // caps how many results per second a source publishes (<= 0 removes the cap) and remembers it
        public void SetFpsLimit(int sourceId, int fps)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            int limit = fps > 0 ? fps : -1;
            ManagerWrapper.Instance.SetSourceFpsLimit(sourceId, limit);
            if (ActiveOverrides(source) is CameraOverrides overrides) overrides.FpsLimit = limit > 0 ? limit : null;
            else source.FpsLimit = limit > 0 ? limit : null;
            DB.Instance.Save();
        }

        // Copies a file source (image or video) as "<name> copy" with the same path, fps limit and pipeline profiles. A physical camera
        // cannot be opened twice, so cameras are refused: use a pipeline profile (or a crop source) for a second view of one.
        public int DuplicateSource(int sourceId)
        {
            Source original = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            string name = original.Name + " copy";
            int newId;
            switch (original.Type)
            {
                case SourceType.VideoFile:
                    newId = InitializeVideoFileSource(original.FilePath!, original.Fps ?? 30, name);
                    break;
                case SourceType.ImageFile:
                    newId = initializeImageFileSource(original.FilePath!, name);
                    break;
                case SourceType.Camera:
                    throw Server.Web.ApiException.BadRequest("a camera can only be opened once; add another pipeline profile to it instead of copying the node");
                default:
                    throw Server.Web.ApiException.BadRequest($"{original.Type} nodes cannot be copied");
            }

            Source copy = GetSourceById(newId);
            if (original.FpsLimit.HasValue) SetFpsLimit(newId, original.FpsLimit.Value);
            copy.Profiles = original.Profiles.Select(p => p.Clone()).ToList();
            DB.Instance.Save();
            return newId;
        }

        // writes one camera control and, when the device accepted it, remembers the value for the next start
        public bool SetCameraControl(int sourceId, int controlId, int value)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            if (!ManagerWrapper.Instance.SetCameraControl(sourceId, controlId, value)) return false;
            // with the active pipeline overriding camera settings the edit belongs to that pipeline, not to the camera's own settings
            if (ActiveOverrides(source) is CameraOverrides overrides)
            {
                overrides.ControlValues[controlId] = value;
            }
            else
            {
                source.ControlValues ??= new Dictionary<int, int>();
                source.ControlValues[controlId] = value;
            }
            DB.Instance.Save();
            return true;
        }

        private static CameraOverrides? ActiveOverrides(Source source) =>
            source.Profiles.FirstOrDefault(p => p.Index == source.ActiveProfileIndex)?.CameraOverrides;

        // the camera controls each source currently has set by a pipeline's overrides (not persisted: ActivateProfile reapplies them at start)
        private readonly Dictionary<int, HashSet<int>> overriddenControls = new Dictionary<int, HashSet<int>>();

        // Puts a camera into the state a pipeline asks for: the camera's own settings with the pipeline's overrides on top. Controls a previous
        // pipeline overrode but this one does not go back to the camera's own value (or the device default when it never had one).
        private void ApplyCameraSettings(Source source, CameraOverrides? overrides)
        {
            if (source.Type != SourceType.Camera) return;

            HashSet<int> previous = overriddenControls.TryGetValue(source.Id, out var set) ? set : new HashSet<int>();
            HashSet<int> next = overrides?.ControlValues.Keys.ToHashSet() ?? new HashSet<int>();
            var restore = previous.Except(next).ToList();
            if (restore.Count > 0)
            {
                Dictionary<int, int> defaults = ManagerWrapper.Instance.GetCameraControls(source.Id).ToDictionary(c => c.id, c => c.defaultValue);
                foreach (int controlId in restore)
                {
                    int? own = source.ControlValues != null && source.ControlValues.TryGetValue(controlId, out int v) ? v : defaults.TryGetValue(controlId, out int d) ? d : null;
                    if (own.HasValue) TrySetControl(source.Id, controlId, own.Value);
                }
            }
            if (overrides != null)
            {
                foreach (var (controlId, value) in overrides.ControlValues) TrySetControl(source.Id, controlId, value);
            }
            overriddenControls[source.Id] = next;

            FrameTransformDto transform = overrides?.Transform ?? source.Transform ?? default;
            ManagerWrapper.Instance.SetCameraTransform(source.Id, transform.ToNative());
            ManagerWrapper.Instance.SetSourceFpsLimit(source.Id, overrides?.FpsLimit ?? source.FpsLimit ?? -1);
        }

        private static void TrySetControl(int sourceId, int controlId, int value)
        {
            try
            {
                ManagerWrapper.Instance.SetCameraControl(sourceId, controlId, value);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Applying camera control {controlId} on source {sourceId} failed: {ex.Message}");
            }
        }

        // Turns a pipeline's camera-setting overrides on or off. Turning them on starts from what the camera is doing now (the active pipeline) or
        // from the camera's own settings (any other), so the pipeline initially behaves as before; later edits to the camera while the pipeline
        // is active then belong to it. Off removes them and, for the active pipeline, puts the camera's own settings back.
        public void SetProfileCameraOverrides(int sourceId, int index, bool enabled)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            if (source.Type != SourceType.Camera) throw Server.Web.ApiException.BadRequest("only camera sources have camera settings");
            PipelineProfile profile = source.Profiles.FirstOrDefault(p => p.Index == index)
                ?? throw new ArgumentException($"source {sourceId} has no profile at index {index}");
            bool active = source.ActiveProfileIndex == index;

            if (enabled && profile.CameraOverrides == null)
            {
                var overrides = new CameraOverrides();
                if (active)
                {
                    foreach (var control in ManagerWrapper.Instance.GetCameraControls(sourceId))
                    {
                        // buttons have no value, and an inactive or read-only control cannot be set
                        if (control.kind == CameraControlKind.CAMERA_CONTROL_BUTTON || control.readOnly || control.inactive) continue;
                        overrides.ControlValues[control.id] = control.value;
                    }
                    overrides.Transform = FrameTransformDto.From(ManagerWrapper.Instance.GetCameraTransform(sourceId));
                }
                else
                {
                    if (source.ControlValues != null) overrides.ControlValues = new Dictionary<int, int>(source.ControlValues);
                    overrides.Transform = source.Transform;
                }
                overrides.FpsLimit = source.FpsLimit;
                profile.CameraOverrides = overrides;
            }
            else if (!enabled && profile.CameraOverrides != null)
            {
                profile.CameraOverrides = null;
                if (active) ApplyCameraSettings(source, null);
            }
            DB.Instance.Save();
        }

        // reshapes a camera's frames; the transform is remembered across restarts. Validates the rotation and crop.
        public void SetCameraTransform(int sourceId, FrameTransformDto transform)
        {
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            if (source.Type != SourceType.Camera) throw Server.Web.ApiException.BadRequest("only camera sources can be transformed");
            if (transform.Rotation % 90 != 0) throw Server.Web.ApiException.BadRequest("rotation must be a multiple of 90 degrees");
            if (transform.CropX < 0 || transform.CropY < 0 || transform.CropWidth < 0 || transform.CropHeight < 0)
                throw Server.Web.ApiException.BadRequest("the crop must not be negative");
            if ((transform.CropWidth == 0) != (transform.CropHeight == 0))
                throw Server.Web.ApiException.BadRequest("set both the crop width and height, or neither");

            transform = transform with { Rotation = ((transform.Rotation % 360) + 360) % 360 };
            ManagerWrapper.Instance.SetCameraTransform(sourceId, transform.ToNative());
            if (ActiveOverrides(source) is CameraOverrides overrides) overrides.Transform = transform;
            else source.Transform = transform.IsIdentity ? null : transform;
            DB.Instance.Save();
        }

        public FrameTransformDto GetCameraTransform(int sourceId)
        {
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            return ActiveOverrides(source)?.Transform ?? source.Transform ?? default;
        }

        // reapplies the remembered control values; a control the device no longer has is skipped
        public void ApplyCameraControls(int sourceId)
        {
            Source? source = GetSourceById(sourceId);
            if (source?.Transform is FrameTransformDto transform)
            {
                try
                {
                    ManagerWrapper.Instance.SetCameraTransform(sourceId, transform.ToNative());
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Restoring the frame transform on source {sourceId} failed: {ex.Message}");
                }
            }
            if (source?.ControlValues == null) return;
            foreach (var (controlId, value) in source.ControlValues)
            {
                try
                {
                    ManagerWrapper.Instance.SetCameraControl(sourceId, controlId, value);
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Restoring camera control {controlId} on source {sourceId} failed: {ex.Message}");
                }
            }
        }

        // deletes a source and unbinds it from any sinks referencing it
        public void DeleteSource(int sourceId)
        {
            ManagerWrapper.Instance.DeleteSource(sourceId);

            sources.RemoveAll(s => s.Id == sourceId);

            foreach (var sinkId in SinkManager.Instance.getAllSinkIds())
            {
                var sink = SinkManager.Instance.GetSinkById(sinkId);
                if (sink != null && sink.Source != null && sink.Source.Id == sourceId)
                {
                    sink.Source = null;
                }
            }

            DB.Instance.Save();
        }

        public bool IsSourceActive(int sourceId)
        {
            return ManagerWrapper.Instance.IsSourceActive(sourceId);
        }

        // Pipeline profiles (see PipelineProfile.cs). Index is assigned once and never reused after a delete,
        // so a robot program's stored index keeps meaning the same profile.
        public int AddApriltagProfile(int sourceId, string name, double tagSize,
            ApriltagBackendKind backend, int frameWidth, int frameHeight, bool driverMode,
            int? threads = null, float? quadDecimate = null, bool? refineEdges = null, RefineEdgesMode? refineMode = null,
            ApriltagAdvancedTuning? advanced = null)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            int index = source.Profiles.Count == 0 ? 0 : source.Profiles.Max(p => p.Index) + 1;
            source.Profiles.Add(new PipelineProfile
            {
                Index = index,
                Name = name,
                Kind = DetectionSinkKind.ApriltagSink,
                TagSize = tagSize,
                Backend = backend,
                FrameWidth = frameWidth,
                FrameHeight = frameHeight,
                DriverMode = driverMode,
                Threads = threads,
                QuadDecimate = quadDecimate,
                RefineEdges = refineEdges,
                RefineMode = refineMode,
                Advanced = advanced
            });
            DB.Instance.Save();
            return index;
        }

        public int AddObjectDetectionProfile(int sourceId, string name, int modelId)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            int index = source.Profiles.Count == 0 ? 0 : source.Profiles.Max(p => p.Index) + 1;
            source.Profiles.Add(new PipelineProfile
            {
                Index = index,
                Name = name,
                Kind = DetectionSinkKind.ObjectDetectionSink,
                ModelId = modelId
            });
            DB.Instance.Save();
            return index;
        }

        public void DeleteProfile(int sourceId, int index)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            if (source.ActiveProfileIndex == index)
                throw new InvalidOperationException("cannot delete the active profile - activate a different one first");
            source.Profiles.RemoveAll(p => p.Index == index);
            DB.Instance.Save();
        }

        public List<PipelineProfile> GetProfiles(int sourceId)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            return source.Profiles;
        }

        // writes a field layout JSON onto one profile (profile-scoped, see PipelineProfile.FieldLayoutPath);
        // applied to the live sink immediately if that profile is active.
        public void SetProfileFieldLayout(int sourceId, int index, string path)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            PipelineProfile profile = source.Profiles.FirstOrDefault(p => p.Index == index)
                ?? throw new ArgumentException($"source {sourceId} has no profile at index {index}");
            profile.FieldLayoutPath = path;
            DB.Instance.Save();

            if (source.ActiveProfileIndex == index && source.ActiveDetectionSinkId.HasValue)
                ManagerWrapper.Instance.LoadFieldLayout(source.ActiveDetectionSinkId.Value, path);
        }

        // Tears down the source's current detection sink and recreates it from the chosen profile at the same id (source.ActiveDetectionSinkId), so downstream bindings survive.
        // Manager::DeleteSink unbinds the other sinks natively, so their bindings are captured beforehand and re-established afterwards.
        public void ActivateProfile(int sourceId, int profileIndex)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            PipelineProfile profile = source.Profiles.FirstOrDefault(p => p.Index == profileIndex)
                ?? throw new ArgumentException($"source {sourceId} has no profile at index {profileIndex}");

            ApplyCameraSettings(source, profile.CameraOverrides);

            List<int> downstreamSinkIds = source.ActiveDetectionSinkId.HasValue
                ? SinkManager.Instance.GetSinksBoundToSource(source.ActiveDetectionSinkId.Value)
                : new List<int>();

            int? explicitId = source.ActiveDetectionSinkId;
            if (explicitId.HasValue)
                SinkManager.Instance.DeleteSink(explicitId.Value);

            string sinkName = $"{source.Name} - {profile.Name}";
            int sinkId = SinkManager.Instance.CreateOrReplaceDetectionSinkForProfile(sinkName, profile, explicitId, sourceId);

            SinkManager.Instance.BindSourceToSink(sinkId, sourceId);
            foreach (int downstreamId in downstreamSinkIds)
            {
                SinkManager.Instance.BindSourceToSink(downstreamId, sinkId);
            }
            SinkManager.Instance.EnableSinkById(sinkId);

            source.ActiveDetectionSinkId = sinkId;
            source.ActiveProfileIndex = profileIndex;
            DB.Instance.Save();
        }

        public int GetActiveProfileIndex(int sourceId)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            return source.ActiveProfileIndex;
        }
    }
}
