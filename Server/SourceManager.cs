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
            source.FpsLimit = limit > 0 ? limit : null;
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
            source.ControlValues ??= new Dictionary<int, int>();
            source.ControlValues[controlId] = value;
            DB.Instance.Save();
            return true;
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
            source.Transform = transform.IsIdentity ? null : transform;
            DB.Instance.Save();
        }

        public FrameTransformDto GetCameraTransform(int sourceId)
        {
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            return source.Transform ?? default;
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
            int? threads = null, float? quadDecimate = null, bool? refineEdges = null, RefineEdgesMode? refineMode = null)
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
                RefineMode = refineMode
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
