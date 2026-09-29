using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Calibration sessions: interactive camera/stereo calibration that never becomes a graph node.
    internal class CalibrationController : ControllerBase
    {
        // POST: start a camera calibration session on a camera source (default 6x9 checkerboard, 25mm squares)
        [HttpPost("calibration/camera/start")]
        public Task<CalibrationSessionDto> StartCamera([FromQuery] int sourceId,
            [FromQuery] CalibrationBoardType boardType = CalibrationBoardType.BOARD_CHECKERBOARD,
            [FromQuery] int rows = 6, [FromQuery] int cols = 9, [FromQuery] double squareSizeMeters = 0.025,
            [FromQuery] double markerSizeMeters = 0.018, [FromQuery] int arucoDictionaryId = 10)
        {
            var session = CalibrationSessionManager.Instance.StartCamera(
                sourceId, boardType, rows, cols, (float)squareSizeMeters, (float)markerSizeMeters, arucoDictionaryId);
            return Task.FromResult(CalibrationSessionDto.From(session));
        }

        // POST: start a stereo calibration session (checkerboard only); left/right roles are explicit
        [HttpPost("calibration/stereo/start")]
        public Task<CalibrationSessionDto> StartStereo([FromQuery] int leftSourceId, [FromQuery] int rightSourceId,
            [FromQuery] CalibrationBoardType boardType = CalibrationBoardType.BOARD_CHECKERBOARD,
            [FromQuery] int rows = 6, [FromQuery] int cols = 9, [FromQuery] double squareSizeMeters = 0.025)
        {
            var session = CalibrationSessionManager.Instance.StartStereo(
                leftSourceId, rightSourceId, boardType, rows, cols, (float)squareSizeMeters);
            return Task.FromResult(CalibrationSessionDto.From(session));
        }

        // POST: start a stereo session on one side-by-side camera; its frame is split in half into left/right eyes
        [HttpPost("calibration/stereo/startSplit")]
        public Task<CalibrationSessionDto> StartStereoSplit([FromQuery] int sourceId,
            [FromQuery] CalibrationBoardType boardType = CalibrationBoardType.BOARD_CHECKERBOARD,
            [FromQuery] int rows = 6, [FromQuery] int cols = 9, [FromQuery] double squareSizeMeters = 0.025)
        {
            var session = CalibrationSessionManager.Instance.StartStereoSplit(
                sourceId, boardType, rows, cols, (float)squareSizeMeters);
            return Task.FromResult(CalibrationSessionDto.From(session));
        }

        // GET: every running session
        [HttpGet("calibration/sessions")]
        public Task<List<CalibrationSessionDto>> GetSessions()
        {
            return Task.FromResult(CalibrationSessionManager.Instance.GetAll().Select(CalibrationSessionDto.From).ToList());
        }

        // GET: one running session
        [HttpGet("calibration/{id}")]
        public Task<CalibrationSessionDto> GetSession(int id)
        {
            return Task.FromResult(CalibrationSessionDto.From(CalibrationSessionManager.Instance.Get(id)));
        }

        // POST: end a session and free its native calibrator and preview sink
        [HttpPost("calibration/{id}/stop")]
        public Task Stop(int id)
        {
            CalibrationSessionManager.Instance.Stop(id);
            return Task.CompletedTask;
        }

        // POST: save the board detected in the latest frame (camera) or the latest matched pair (stereo)
        [HttpPost("calibration/{id}/saveDetection")]
        public Task<bool> SaveDetection(int id)
        {
            var m = CalibrationSessionManager.Instance;
            return Task.FromResult(m.Get(id).Kind == CalibrationSessionKind.Camera ? m.SaveCameraDetection(id) : m.SaveStereoDetection(id));
        }

        // GET: number of saved snapshots (camera) or pairs (stereo)
        [HttpGet("calibration/{id}/count")]
        public Task<int> GetCount(int id)
        {
            var m = CalibrationSessionManager.Instance;
            return Task.FromResult(m.Get(id).Kind == CalibrationSessionKind.Camera ? m.GetCameraSnapshotCount(id) : m.GetStereoPairCount(id));
        }

        // DELETE: remove one saved snapshot/pair by index
        [HttpDelete("calibration/{id}/entry")]
        public Task<bool> RemoveEntry(int id, [FromQuery] int index)
        {
            var m = CalibrationSessionManager.Instance;
            return Task.FromResult(m.Get(id).Kind == CalibrationSessionKind.Camera ? m.RemoveCameraSnapshot(id, index) : m.RemoveStereoPair(id, index));
        }

        // DELETE: discard every saved snapshot/pair
        [HttpDelete("calibration/{id}/entries")]
        public Task ClearEntries(int id)
        {
            var m = CalibrationSessionManager.Instance;
            if (m.Get(id).Kind == CalibrationSessionKind.Camera) m.ClearCameraSnapshots(id); else m.ClearStereoPairs(id);
            return Task.CompletedTask;
        }

        // POST: run a camera session's calibration and persist the result by camera path and resolution
        [HttpPost("calibration/{id}/runCamera")]
        public Task<CameraCalibrationResultDto> RunCamera(int id)
        {
            return Task.FromResult(CameraCalibrationResultDto.From(CalibrationSessionManager.Instance.RunCamera(id)));
        }

        // POST: run a stereo session's calibration; gate real use on the result's EpipolarRms < 0.5px
        [HttpPost("calibration/{id}/runStereo")]
        public Task<StereoCalibrationResultDto> RunStereo(int id)
        {
            return Task.FromResult(StereoCalibrationResultDto.From(CalibrationSessionManager.Instance.RunStereo(id)));
        }

        // GET: the last camera calibration result of a session (does not run calibration)
        [HttpGet("calibration/{id}/cameraResult")]
        public Task<CameraCalibrationResultDto> GetCameraResult(int id)
        {
            return Task.FromResult(CameraCalibrationResultDto.From(CalibrationSessionManager.Instance.GetCameraResult(id)));
        }

        // GET: the last stereo calibration result of a session (does not run calibration)
        [HttpGet("calibration/{id}/stereoResult")]
        public Task<StereoCalibrationResultDto> GetStereoResult(int id)
        {
            return Task.FromResult(StereoCalibrationResultDto.From(CalibrationSessionManager.Instance.GetStereoResult(id)));
        }

        // GET: saved corner points for the coverage heatmap; eye ("left"/"right") applies to stereo sessions only
        [HttpGet("calibration/{id}/coverage")]
        public Task<CalibrationCoverageDto> GetCoverage(int id, [FromQuery] string? eye = null)
        {
            var m = CalibrationSessionManager.Instance;
            return Task.FromResult(m.Get(id).Kind == CalibrationSessionKind.Camera ? m.GetCameraCoverage(id) : m.GetStereoCoverage(id, eye ?? "left"));
        }

        // GET: the calibrator's latest per-frame status JSON (stereo: foundLeft, foundRight, skew)
        [HttpGet("calibration/{id}/status")]
        public Task<string> GetStatus(int id)
        {
            return Task.FromResult(CalibrationSessionManager.Instance.GetStatusJson(id));
        }

        // GET: every camera calibration saved to disk
        [HttpGet("calibration/saved")]
        public Task<List<StoredCalibrationDto>> GetSaved()
        {
            return Task.FromResult(CalibrationManager.Instance.GetAll().Select(StoredCalibrationDto.From).ToList());
        }

        // GET: every stereo calibration saved to disk
        [HttpGet("calibration/savedStereo")]
        public Task<List<StoredStereoCalibrationDto>> GetSavedStereo()
        {
            return Task.FromResult(StereoCalibrationManager.Instance.GetAll().Select(StoredStereoCalibrationDto.From).ToList());
        }
    }
}
