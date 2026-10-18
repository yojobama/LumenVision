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
            if (string.IsNullOrWhiteSpace(newName)) throw Server.Web.ApiException.BadRequest("a name is required");
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            source.Name = newName.Trim();
            DB.Instance.Save();
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
                StopSource(source);
            }
        }
        public void EnableAllSources()
        {
            // cameras and files first: a crop needs its parent running to have frames to cut
            foreach (var source in sources.OrderBy(s => s.Type == SourceType.Crop ? 1 : 0))
            {
                StartSource(source);
            }
        }

        // A crop is a native sink of its parent that republishes part of each frame, so starting it means starting the parent and then the
        // crop as a sink; every other source is started as a source.
        private bool StartSource(Source source)
        {
            if (source.Type != SourceType.Crop) return ManagerWrapper.Instance.StartSourceById(source.Id);
            if (source.CropParentId is int parentId && GetSourceById(parentId) is Source parent) StartSource(parent);
            return ManagerWrapper.Instance.StartSinkById(source.Id);
        }

        private void StopSource(Source source)
        {
            if (source.Type == SourceType.Crop) ManagerWrapper.Instance.StopSinkById(source.Id);
            else ManagerWrapper.Instance.StopSourceById(source.Id);
        }

        public void DisableSourceById(int id)
        {
            foreach (var source in sources)
            {
                if (source.Id == id)
                {
                    StopSource(source);
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
                    if (!StartSource(source)) throw new Exception("unable to start source");
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

        // The region a standard split keeps of a parent frame of this size.
        public static CropRegionDto RegionFor(string layout, int parentWidth, int parentHeight) => layout switch
        {
            "left" => new CropRegionDto(0, 0, parentWidth / 2, parentHeight),
            "right" => new CropRegionDto(parentWidth / 2, 0, parentWidth - parentWidth / 2, parentHeight),
            "top" => new CropRegionDto(0, 0, parentWidth, parentHeight / 2),
            "bottom" => new CropRegionDto(0, parentHeight / 2, parentWidth, parentHeight - parentHeight / 2),
            _ => throw Server.Web.ApiException.BadRequest($"unknown split '{layout}' (left, right, top or bottom)"),
        };

        // Publishes part of a source's frame as a source of its own: `layout` cuts a half and keeps re-cutting it, else `region` is the fixed rectangle.
        public int InitializeCropSource(int parentId, string name, string? layout = null, CropRegionDto? region = null, int? id = null)
        {
            Source parent = GetSourceById(parentId) ?? throw Server.Web.ApiException.NotFound($"no source with id {parentId}");
            if (parent.Type == SourceType.SinkOutput) throw Server.Web.ApiException.BadRequest("a detector's output cannot be cropped; crop the camera it reads");
            if (string.IsNullOrWhiteSpace(name)) throw Server.Web.ApiException.BadRequest("a name is required");

            CropRegionDto kept;
            if (region.HasValue)
            {
                // a given region wins (a saved crop being restored, whose camera has not been opened yet and so has no mode to cut from);
                // a layout given with it is kept, so the crop is re-cut when the camera's mode is known
                kept = region.Value;
            }
            else if (layout != null)
            {
                if (parent.Type != SourceType.Camera) throw Server.Web.ApiException.BadRequest("only a camera can be split in halves; give the region to crop instead");
                CameraMode mode = ManagerWrapper.Instance.GetCameraCurrentMode(parentId);
                kept = RegionFor(layout, mode.width, mode.height);
            }
            else
            {
                throw Server.Web.ApiException.BadRequest("give a layout or a region to crop");
            }
            if (kept.Width <= 0 || kept.Height <= 0 || kept.X < 0 || kept.Y < 0)
                throw Server.Web.ApiException.BadRequest("the crop must have a positive size and start inside the frame");

            int sourceId = id.HasValue
                ? ManagerWrapper.Instance.CreateRoiSource(parentId, kept.X, kept.Y, kept.Width, kept.Height, id.Value)
                : ManagerWrapper.Instance.CreateRoiSource(parentId, kept.X, kept.Y, kept.Width, kept.Height);
            sources.Add(new Source(sourceId, name, SourceType.Crop) { CropParentId = parentId, CropRegion = kept, CropLayout = layout });
            DB.Instance.Save();
            if (layout != null) StartCropReconciler();
            return sourceId;
        }

        // A camera's mode is only known once it is open, which after a restart is a moment after its splits are restored, so a slow check
        // re-cuts every split from its camera's real mode (and heals one cut before the camera changed under it).
        private System.Threading.Timer? cropReconciler;
        private void StartCropReconciler()
        {
            cropReconciler ??= new System.Threading.Timer(_ =>
            {
                try
                {
                    foreach (int parentId in sources.Where(s => s.Type == SourceType.Crop && s.CropLayout != null && s.CropParentId.HasValue)
                        .Select(s => s.CropParentId!.Value).Distinct().ToList())
                        RefitCrops(parentId, saveIfChanged: true);
                }
                catch (Exception)
                {
                    // the source list changed under the enumeration: the next tick tries again
                }
            }, null, TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(3));
        }

        // The two halves of a side-by-side (or top-and-bottom) camera as two crop sources, named after the camera: [first, second] = [left, right]
        // or [top, bottom]. A stereo pair binds them as its left and right eyes.
        public int[] SplitSource(int parentId, bool topBottom = false)
        {
            Source parent = GetSourceById(parentId) ?? throw Server.Web.ApiException.NotFound($"no source with id {parentId}");
            string first = topBottom ? "top" : "left", second = topBottom ? "bottom" : "right";
            int firstId = InitializeCropSource(parentId, $"{parent.Name} {first}", first);
            try
            {
                return new[] { firstId, InitializeCropSource(parentId, $"{parent.Name} {second}", second) };
            }
            catch
            {
                DeleteSource(firstId);
                throw;
            }
        }

        // A camera changed resolution: its split crops are cut again from the new frame size, so left stays the left half. Custom regions stay as given.
        public void RefitCrops(int parentId, bool saveIfChanged = false)
        {
            bool changed = false;
            foreach (Source crop in sources.Where(s => s.Type == SourceType.Crop && s.CropParentId == parentId && s.CropLayout != null).ToList())
            {
                try
                {
                    CameraMode mode = ManagerWrapper.Instance.GetCameraCurrentMode(parentId);
                    if (mode.width <= 0 || mode.height <= 0) continue; // the camera is not open yet: nothing to cut from
                    CropRegionDto region = RegionFor(crop.CropLayout!, mode.width, mode.height);
                    if (crop.CropRegion == region) continue;
                    if (ManagerWrapper.Instance.SetRoiSourceRect(crop.Id, region.X, region.Y, region.Width, region.Height))
                    {
                        crop.CropRegion = region;
                        changed = true;
                    }
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Refitting crop {crop.Id} to its camera failed: {ex.Message}");
                }
            }
            if (changed || !saveIfChanged) DB.Instance.Save();
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
                // remembered as overridden so switching pipelines or turning the override off puts the camera's own value back
                if (!overriddenControls.TryGetValue(sourceId, out var overridden)) overriddenControls[sourceId] = overridden = new HashSet<int>();
                overridden.Add(controlId);
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
            SinkManager.Instance.RefreshApriltagCalibrations(sourceId); // a calibration is carried across a transform
        }

        public FrameTransformDto GetCameraTransform(int sourceId)
        {
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            return ActiveOverrides(source)?.Transform ?? source.Transform ?? default;
        }

        // Requests a capture mode and, when the device accepts it, remembers it so the camera comes back in it after a restart. Returns whether
        // the request was accepted, not whether the device honours it exactly (re-read the current mode).
        public bool SetCameraMode(int sourceId, CameraModeDto mode)
        {
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            bool applied = ManagerWrapper.Instance.SetCameraMode(sourceId, mode.ToNative());
            if (applied)
            {
                source.Mode = mode;
                DB.Instance.Save();
                RefitCrops(sourceId);
            }
            SinkManager.Instance.RefreshApriltagCalibrations(sourceId); // the saved calibration is per resolution
            return applied;
        }

        // reapplies the remembered mode, transform and control values; a control the device no longer has is skipped
        public void ApplyCameraControls(int sourceId)
        {
            Source? source = GetSourceById(sourceId);
            if (source?.Mode is CameraModeDto mode)
            {
                // first: the transform and controls are read against the mode the camera ends up in
                try
                {
                    if (!ManagerWrapper.Instance.SetCameraMode(sourceId, mode.ToNative()))
                        Console.WriteLine($"The camera on source {sourceId} did not accept its saved mode {mode.Width}x{mode.Height}");
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Restoring the capture mode on source {sourceId} failed: {ex.Message}");
                }
            }
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
            // the crops of a source go with it: nothing would feed them
            foreach (int cropId in sources.Where(s => s.Type == SourceType.Crop && s.CropParentId == sourceId).Select(s => s.Id).ToList())
                DeleteSource(cropId);

            SinkManager.Instance.DeletePreviewsOf(sourceId);
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
            // a crop runs as a native sink of its parent
            if (GetSourceById(sourceId)?.Type == SourceType.Crop) return ManagerWrapper.Instance.IsSinkActive(sourceId);
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

        public int AddObjectDetectionProfile(int sourceId, string name, int modelId, float? confThreshold = null, float? nmsThreshold = null)
        {
            Source source = GetSourceById(sourceId) ?? throw new ArgumentException($"no source with id {sourceId}");
            int index = source.Profiles.Count == 0 ? 0 : source.Profiles.Max(p => p.Index) + 1;
            source.Profiles.Add(new PipelineProfile
            {
                Index = index,
                Name = name,
                Kind = DetectionSinkKind.ObjectDetectionSink,
                ModelId = modelId,
                ConfThreshold = confThreshold,
                NmsThreshold = nmsThreshold
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
