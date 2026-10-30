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
            source.FpsLimit = limit > 0 ? limit : null;
            DB.Instance.Save();
        }

        // Copies a file source (image or video) as "<name> copy" with the same path and fps limit. A physical camera cannot be opened
        // twice, so cameras are refused: bind more detector nodes (or a crop source) to it for another view.
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
                    throw Server.Web.ApiException.BadRequest("a camera can only be opened once; bind another detector node to it instead of copying the node");
                default:
                    throw Server.Web.ApiException.BadRequest($"{original.Type} nodes cannot be copied");
            }

            Source copy = GetSourceById(newId);
            if (original.FpsLimit.HasValue) SetFpsLimit(newId, original.FpsLimit.Value);
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
            SinkManager.Instance.RefreshDetectorCalibrations(sourceId); // a calibration is carried across a transform
        }

        public FrameTransformDto GetCameraTransform(int sourceId)
        {
            Source source = GetSourceById(sourceId) ?? throw Server.Web.ApiException.NotFound($"no source with id {sourceId}");
            return source.Transform ?? default;
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
            SinkManager.Instance.RefreshDetectorCalibrations(sourceId); // the saved calibration is per resolution
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

    }
}
