using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sources
{
    internal class CameraSourceController : ControllerBase
    {
        // POST: create camera sources from all connected cameras;
        [HttpPost("cameraSource/createAll")]
        public Task CreateAll()
        {
            CameraHardwareInfo[] cameraHardwareInfoArray = GetUnregisteredCameras();

            cameraHardwareInfoArray.ToList().ForEach(hardwareInfo =>
            {
                int sourceId = SourceManager.Instance.InitializeCameraSource(hardwareInfo);
            });

            return Task.CompletedTask;
        }

        // POST: Create a camera source from a specified camera;
        // Body is deserialised by hand with System.Text.Json rather than bound as a record struct.
        [HttpPost("cameraSource/create")]
        public async Task<int> Create([FromQuery] string name = "default")
        {
            string body = await HttpContext.GetRequestBodyAsStringAsync();
            CameraHardwareInfoDto hardwareInfo = System.Text.Json.JsonSerializer.Deserialize<CameraHardwareInfoDto>(body);
            int sourceId = SourceManager.Instance.InitializeCameraSource(hardwareInfo.ToNative(), name);
            return sourceId;
        }

        // GET: All connected cameras;
        [HttpGet("cameraSource/getRegistered")]
        public Task<Source[]> GetRegistered()
        {
            List<Source> sources = new List<Source>();

            foreach (var item in SourceManager.Instance.GetAllSourceIds())
            {
                Source source = SourceManager.Instance.GetSourceById(item);
                if (source.Type == SourceType.Camera)
                    sources.Add(source);
            }
            return Task.FromResult(sources.ToArray());
        }

        // GET: available cameras not yet turned into a source;
        [HttpGet("cameraSource/getNotRegistered")]
        public Task<CameraHardwareInfoDto[]> GetNotRegistered()
        {
            return Task.FromResult(GetUnregisteredCameras().Select(CameraHardwareInfoDto.From).ToArray());
        }

        // Enumerated cameras no camera source uses, compared by resolved device node (a saved /dev/videoN and a
        // by-path link can name the same physical camera).
        private static CameraHardwareInfo[] GetUnregisteredCameras()
        {
            HashSet<string> registered = SourceManager.Instance.GetAllSourceIds()
                .Select(id => SourceManager.Instance.GetSourceById(id))
                .Where(source => source?.Type == SourceType.Camera && source.CameraHardwareInfo != null)
                .Select(source => ResolveDevicePath(source!.CameraHardwareInfo!.path))
                .ToHashSet();
            return ManagerWrapper.Instance.EnumerateAvailableCameras()
                .Where(hardwareInfo => !registered.Contains(ResolveDevicePath(hardwareInfo.path)))
                .ToArray();
        }

        private static string ResolveDevicePath(string path)
        {
            try
            {
                return System.IO.File.ResolveLinkTarget(path, returnFinalTarget: true)?.FullName ?? path;
            }
            catch (Exception)
            {
                // not a filesystem path at all (Windows camera indices), or gone - compare as-is
                return path;
            }
        }

        // GET: every capture mode this camera advertises
        [HttpGet("cameraSource/{id}/modes")]
        public Task<CameraModeDto[]> GetModes(int id)
        {
            return Task.FromResult(ManagerWrapper.Instance.GetCameraModes(id).Select(CameraModeDto.From).ToArray());
        }

        // GET: the mode the device is actually running; check isNative after a /mode PATCH (V4L2 and Media Foundation may substitute).
        [HttpGet("cameraSource/{id}/currentMode")]
        public Task<CameraModeDto> GetCurrentMode(int id)
        {
            return Task.FromResult(CameraModeDto.From(ManagerWrapper.Instance.GetCameraCurrentMode(id)));
        }

        // GET: whether this camera has a saved calibration and whether it matches the current capture mode.
        [HttpGet("cameraSource/{id}/calibrationStatus")]
        public Task<CalibrationStatusDto> GetCalibrationStatus(int id)
        {
            return Task.FromResult(CalibrationStatusDto.From(CalibrationManager.Instance.GetCalibrationStatus(id)));
        }

        // PATCH: request a capture mode. Returns whether the API call succeeded, not whether the device honoured it
        // exactly; re-GET /currentMode.
        [HttpPatch("cameraSource/{id}/mode")]
        public async Task<bool> SetMode(int id)
        {
            string body = await HttpContext.GetRequestBodyAsStringAsync();
            CameraModeDto mode = System.Text.Json.JsonSerializer.Deserialize<CameraModeDto>(body);
            return ManagerWrapper.Instance.SetCameraMode(id, mode.ToNative());
        }

        // PATCH: exposure/gain control. Set autoExposure=false before exposureAbsolute for it to take effect on most UVC hardware.
        [HttpPatch("cameraSource/{id}/exposure")]
        public Task<bool> SetExposure(int id, [FromQuery] int exposureAbsolute)
        {
            return Task.FromResult(ManagerWrapper.Instance.SetCameraExposure(id, exposureAbsolute));
        }

        [HttpPatch("cameraSource/{id}/autoExposure")]
        public Task<bool> SetAutoExposure(int id, [FromQuery] bool enabled)
        {
            return Task.FromResult(ManagerWrapper.Instance.SetCameraAutoExposure(id, enabled));
        }

        [HttpPatch("cameraSource/{id}/gain")]
        public Task<bool> SetGain(int id, [FromQuery] int gain)
        {
            return Task.FromResult(ManagerWrapper.Instance.SetCameraGain(id, gain));
        }

        // GET: the device's range and current value for exposure/gain; units vary per camera (UVC 100us steps, MIPI sensor lines).
        [HttpGet("cameraSource/{id}/controls")]
        public Task<CameraControlsDto> GetControls(int id)
        {
            return Task.FromResult(new CameraControlsDto(
                CameraControlRangeDto.From(ManagerWrapper.Instance.GetCameraExposureRange(id)),
                CameraControlRangeDto.From(ManagerWrapper.Instance.GetCameraGainRange(id))));
        }

        // GET: every control the device exposes (brightness, contrast, white balance, ...) with range, menu entries and current value
        [HttpGet("cameraSource/{id}/controlList")]
        public Task<CameraControlDto[]> GetControlList(int id)
        {
            return Task.FromResult(ManagerWrapper.Instance.GetCameraControls(id).Select(CameraControlDto.From).ToArray());
        }

        // PATCH: write one control from controlList by its Id; remembered across restarts. Returns whether the device accepted it.
        [HttpPatch("cameraSource/{id}/control")]
        public Task<bool> SetControl(int id, [FromQuery] int controlId, [FromQuery] int value)
        {
            return Task.FromResult(SourceManager.Instance.SetCameraControl(id, controlId, value));
        }

        // GET: the crop/rotation/mirroring applied to this camera's frames (all defaults when none)
        [HttpGet("cameraSource/{id}/transform")]
        public Task<FrameTransformDto> GetTransform(int id)
        {
            return Task.FromResult(SourceManager.Instance.GetCameraTransform(id));
        }

        // PATCH: set the frame transform from the JSON body {Rotation, FlipHorizontal, FlipVertical, CropX, CropY, CropWidth, CropHeight};
        // saved calibrations follow it automatically. An all-default body removes the transform.
        [HttpPatch("cameraSource/{id}/transform")]
        public async Task SetTransform(int id)
        {
            string body = await HttpContext.GetRequestBodyAsStringAsync();
            FrameTransformDto transform = System.Text.Json.JsonSerializer.Deserialize<FrameTransformDto>(body);
            SourceManager.Instance.SetCameraTransform(id, transform);
        }

        // POST: publish a fixed crop of this camera's frame as an independent source (call twice on a side-by-side
        // camera for left/right).
        [HttpPost("cameraSource/{id}/roi")]
        public Task<int> CreateRoi(int id, [FromQuery] int x, [FromQuery] int y, [FromQuery] int width, [FromQuery] int height)
        {
            return Task.FromResult(ManagerWrapper.Instance.CreateRoiSource(id, x, y, width, height));
        }
    }
}
