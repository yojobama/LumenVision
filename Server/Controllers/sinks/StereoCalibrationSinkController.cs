using System.Linq;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    // Mirrors CameraCalibrationSinkController, plus an explicit left/right two-source bind.
    internal class StereoCalibrationSinkController : ControllerBase
    {
        // POST: Create a stereo calibration sink (default 6x9 checkerboard, 25mm squares)
        [HttpPost("stereoCalibrationSink/create")]
        public Task<int> Create([FromQuery] string name)
        {
            int sinkId = SinkManager.Instance.AddStereoCalibrationSink(name);
            DB.Instance.Save();
            return Task.FromResult(sinkId);
        }

        // POST: create a stereo calibration sink with an explicit checkerboard configuration (ChArUco is unsupported).
        [HttpPost("stereoCalibrationSink/createWithBoard")]
        public Task<int> CreateWithBoard([FromQuery] string name, [FromQuery] CalibrationBoardType boardType,
            [FromQuery] int rows, [FromQuery] int cols, [FromQuery] double squareSizeMeters)
        {
            int sinkId = SinkManager.Instance.AddStereoCalibrationSinkWithBoard(name, boardType, rows, cols, (float)squareSizeMeters);
            return Task.FromResult(sinkId);
        }

        // PATCH: bind the explicit left/right camera sources for this stereo sink
        [HttpPatch("stereoCalibrationSink/{id}/bind")]
        public Task Bind(int id, [FromQuery] int leftSourceId, [FromQuery] int rightSourceId)
        {
            SinkManager.Instance.BindStereoSourcesToSink(id, leftSourceId, rightSourceId);
            return Task.CompletedTask;
        }

        // POST: save the most recently matched (both-eyes-found, within-skew) checkerboard pair
        [HttpPost("stereoCalibrationSink/{id}/saveDetection")]
        public Task<bool> SaveDetection(int id)
        {
            return Task.FromResult(SinkManager.Instance.SaveStereoCalibrationDetection(id));
        }

        // GET: how many pairs have been saved so far
        [HttpGet("stereoCalibrationSink/{id}/pairCount")]
        public Task<int> GetPairCount(int id)
        {
            return Task.FromResult(SinkManager.Instance.GetStereoCalibrationPairCount(id));
        }

        // DELETE: remove one saved pair by index
        [HttpDelete("stereoCalibrationSink/{id}/pair")]
        public Task<bool> RemovePair(int id, [FromQuery] int index)
        {
            return Task.FromResult(SinkManager.Instance.RemoveStereoCalibrationPair(id, index));
        }

        // DELETE: discard every saved pair
        [HttpDelete("stereoCalibrationSink/{id}/pairs")]
        public Task ClearPairs(int id)
        {
            SinkManager.Instance.ClearStereoCalibrationPairs(id);
            return Task.CompletedTask;
        }

        // POST: run cv::stereoCalibrate + cv::stereoRectify over all saved pairs; persists the result keyed by both
        // cameras' device paths and resolution. Gate real use on the result's epipolarRms < 0.5px.
        [HttpPost("stereoCalibrationSink/{id}/run")]
        public Task<StereoCalibrationResultDto> RunCalibration(int id)
        {
            return Task.FromResult(StereoCalibrationResultDto.From(SinkManager.Instance.RunStereoCalibration(id)));
        }

        // GET: retrieve the last calibration result computed by this sink
        [HttpGet("stereoCalibrationSink/{id}/result")]
        public Task<StereoCalibrationResultDto> GetResult(int id)
        {
            return Task.FromResult(StereoCalibrationResultDto.From(SinkManager.Instance.GetStereoCalibrationResult(id)));
        }

        // GET: every saved pair's detected corner points for one eye (for the calibration coverage heatmap)
        [HttpGet("stereoCalibrationSink/{id}/coverage")]
        public Task<CalibrationCoverageDto> GetCoverage(int id, [FromQuery] string eye)
        {
            int count = SinkManager.Instance.GetStereoCalibrationPairCount(id);
            var pairs = new double[count][];
            for (int i = 0; i < count; i++)
            {
                pairs[i] = ManagerWrapper.Instance.GetStereoCalibrationPairCorners(id, i, eye).ToArray();
            }
            return Task.FromResult(new CalibrationCoverageDto(
                ManagerWrapper.Instance.GetStereoCalibrationFrameWidth(id),
                ManagerWrapper.Instance.GetStereoCalibrationFrameHeight(id),
                pairs));
        }
    }
}
