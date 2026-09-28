using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    internal class CameraCalibrationSinkController : ControllerBase
    {
        // POST: Create a camera calibration sink (default 6x9 checkerboard, 25mm squares)
        [HttpPost("cameraCalibrationSink/create")]
        public Task<int> Create([FromQuery] string name)
        {
            int sinkId = SinkManager.Instance.AddSink(name, "cameracalibrationsink");
            DB.Instance.Save();
            return Task.FromResult(sinkId);
        }

        // POST: Create a camera calibration sink with an explicit board configuration
        [HttpPost("cameraCalibrationSink/createWithBoard")]
        public Task<int> CreateWithBoard([FromQuery] string name, [FromQuery] CalibrationBoardType boardType,
            [FromQuery] int rows, [FromQuery] int cols, [FromQuery] double squareSizeMeters,
            [FromQuery] double markerSizeMeters = 0.018, [FromQuery] int arucoDictionaryId = 10)
        {
            int sinkId = SinkManager.Instance.AddCameraCalibrationSinkWithBoard(
                name, boardType, rows, cols, (float)squareSizeMeters, (float)markerSizeMeters, arucoDictionaryId);
            return Task.FromResult(sinkId);
        }

        // POST: save the corners detected in the sink's latest frame for later calibration
        [HttpPost("cameraCalibrationSink/{id}/saveDetection")]
        public Task<bool> SaveDetection(int id)
        {
            return Task.FromResult(SinkManager.Instance.SaveCameraCalibrationBoardDetection(id));
        }

        // GET: how many snapshots have been saved so far
        [HttpGet("cameraCalibrationSink/{id}/snapshotCount")]
        public Task<int> GetSnapshotCount(int id)
        {
            return Task.FromResult(SinkManager.Instance.GetCameraCalibrationSnapshotCount(id));
        }

        // DELETE: remove one saved snapshot by index
        [HttpDelete("cameraCalibrationSink/{id}/snapshot")]
        public Task<bool> RemoveSnapshot(int id, [FromQuery] int index)
        {
            return Task.FromResult(SinkManager.Instance.RemoveCameraCalibrationSnapshot(id, index));
        }

        // DELETE: discard every saved snapshot
        [HttpDelete("cameraCalibrationSink/{id}/snapshots")]
        public Task ClearSnapshots(int id)
        {
            SinkManager.Instance.ClearCameraCalibrationSnapshots(id);
            return Task.CompletedTask;
        }

        // POST: run cv::calibrateCamera over all saved snapshots; persists the result keyed by the bound
        // camera's device path and resolution (if bound to a camera source).
        [HttpPost("cameraCalibrationSink/{id}/run")]
        public Task<CameraCalibrationResultDto> RunCalibration(int id)
        {
            return Task.FromResult(CameraCalibrationResultDto.From(SinkManager.Instance.RunCameraCalibration(id)));
        }

        // GET: Retrieve the last calibration result computed by a camera calibration sink
        // (does NOT run calibration - call POST .../run first)
        [HttpGet("cameraCalibrationSink/{id}/result")]
        public Task<CameraCalibrationResultDto> GetResult(int id)
        {
            return Task.FromResult(CameraCalibrationResultDto.From(SinkManager.Instance.GetCameraCalibrationResult(id)));
        }

        // GET: every calibration result ever saved to disk, across all cameras
        [HttpGet("cameraCalibrationSink/savedResults")]
        public Task<List<StoredCalibrationDto>> GetSavedResults()
        {
            return Task.FromResult(CalibrationManager.Instance.GetAll().Select(StoredCalibrationDto.From).ToList());
        }

        // GET: every saved snapshot's detected corner points (for the calibration coverage heatmap)
        [HttpGet("cameraCalibrationSink/{id}/coverage")]
        public Task<CalibrationCoverageDto> GetCoverage(int id)
        {
            int count = SinkManager.Instance.GetCameraCalibrationSnapshotCount(id);
            var snapshots = new double[count][];
            for (int i = 0; i < count; i++)
            {
                snapshots[i] = ManagerWrapper.Instance.GetCameraCalibrationSnapshotCorners(id, i).ToArray();
            }
            return Task.FromResult(new CalibrationCoverageDto(
                ManagerWrapper.Instance.GetCameraCalibrationFrameWidth(id),
                ManagerWrapper.Instance.GetCameraCalibrationFrameHeight(id),
                snapshots));
        }
    }
}
