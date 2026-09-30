using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sources
{
    // Pipeline profiles (see PipelineProfile.cs). Activating one replaces the source's detection sink with a fresh
    // instance built from the profile; other sinks on the same source are untouched.
    internal class PipelineProfileController : ControllerBase
    {
        // POST: define an AprilTag profile on a source; activation uses the source camera's saved calibration at its
        // current resolution (none = pose without real-world scale/undistort). Returns the new profile's index.
        [HttpPost("source/profiles/apriltag")]
        public Task<int> CreateApriltagProfile([FromQuery] int sourceId, [FromQuery] string name,
            [FromQuery] double tagSize,
            [FromQuery] ApriltagBackendKind backend = ApriltagBackendKind.APRILTAG_BACKEND_CPU,
            [FromQuery] int frameWidth = 0, [FromQuery] int frameHeight = 0,
            [FromQuery] bool driverMode = false,
            [FromQuery] int? nthreads = null, [FromQuery] float? quadDecimate = null, [FromQuery] bool? refineEdges = null,
            [FromQuery] RefineEdgesMode? refineMode = null)
        {
            int index = SourceManager.Instance.AddApriltagProfile(sourceId, name, tagSize,
                backend, frameWidth, frameHeight, driverMode, nthreads, quadDecimate, refineEdges, refineMode);
            return Task.FromResult(index);
        }

        // POST: define an object-detection profile using a registered model. Returns the new profile's index.
        [HttpPost("source/profiles/objectDetection")]
        public Task<int> CreateObjectDetectionProfile([FromQuery] int sourceId, [FromQuery] string name,
            [FromQuery] int modelId)
        {
            int index = SourceManager.Instance.AddObjectDetectionProfile(sourceId, name, modelId);
            return Task.FromResult(index);
        }

        // POST: upload a WPILib AprilTagFieldLayout JSON body onto one profile; returns tags loaded, or -1 if invalid.
        [HttpPost("source/profiles/fieldLayout")]
        public async Task<int> SetProfileFieldLayout([FromQuery] int sourceId, [FromQuery] int index)
        {
            using var reader = new StreamReader(HttpContext.OpenRequestStream());
            string json = await reader.ReadToEndAsync();

            string layoutDir = Path.Combine(AppContext.BaseDirectory, "fieldLayouts");
            Directory.CreateDirectory(layoutDir);
            string path = Path.Combine(layoutDir, $"source-{sourceId}-profile-{index}.json");
            await System.IO.File.WriteAllTextAsync(path, json);

            SourceManager.Instance.SetProfileFieldLayout(sourceId, index, path);

            Source source = SourceManager.Instance.GetSourceById(sourceId);
            bool isActive = source != null && source.ActiveProfileIndex == index && source.ActiveDetectionSinkId.HasValue;
            return isActive
                ? ManagerWrapper.Instance.GetFieldLayoutTagCount(source.ActiveDetectionSinkId.Value)
                : 0;
        }

        // GET: every profile defined on a source.
        [HttpGet("source/profiles")]
        public Task<List<PipelineProfile>> GetProfiles([FromQuery] int sourceId)
        {
            return Task.FromResult(SourceManager.Instance.GetProfiles(sourceId));
        }

        // GET: the active profile's index, or -1 if none has been activated
        [HttpGet("source/profiles/active")]
        public Task<int> GetActiveProfile([FromQuery] int sourceId)
        {
            return Task.FromResult(SourceManager.Instance.GetActiveProfileIndex(sourceId));
        }

        // PATCH: switch which profile is running for a source.
        [HttpPatch("source/profiles/activate")]
        public Task Activate([FromQuery] int sourceId, [FromQuery] int index)
        {
            SourceManager.Instance.ActivateProfile(sourceId, index);
            return Task.CompletedTask;
        }

        // DELETE: remove a profile definition. Refuses to delete the currently active one -
        // activate a different profile first.
        [HttpDelete("source/profiles")]
        public Task DeleteProfile([FromQuery] int sourceId, [FromQuery] int index)
        {
            SourceManager.Instance.DeleteProfile(sourceId, index);
            return Task.CompletedTask;
        }
    }
}
