using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sinks
{
    internal class SinkController : ControllerBase
    {
        // GET: Get a sink's active status by id;
        [HttpGet("sink/getStatus")]
        public Task<bool> GetStatus([FromQuery] int SinkID)
        {
            return Task.FromResult(SinkManager.Instance.IsSinkRunning(SinkID));
        }

        // UTF-8 without BOM: strict JSON parsers reject a BOM preamble.
        private static readonly Encoding Utf8NoBom = new UTF8Encoding(false);

        // Written as a raw string body since GetResult/GetAllResults already return complete JSON documents.
        // GET: the latest result JSON of a sink that is also a source; "{}" for a terminal sink or if nothing has been produced yet
        [HttpGet("sink/getResult")]
        public async Task GetResult([FromQuery] int SinkID)
        {
            string json = SinkManager.Instance.GetResult(SinkID);
            await HttpContext.SendStringAsync(json, "application/json", Utf8NoBom);
        }

        // GET: every sink's latest result, keyed by sink id, as one JSON object
        [HttpGet("sink/getAllResults")]
        public async Task GetAllResults()
        {
            string json = SinkManager.Instance.GetAllResults();
            await HttpContext.SendStringAsync(json, "application/json", Utf8NoBom);
        }

        // PATCH: Enable/Disable a sink;
        [HttpPatch("sink/toggle")]
        public Task Toggle([FromQuery] int SinkID, [FromQuery] bool Enabled)
        {
            if (!Enabled) SinkManager.Instance.DisableSinkById(SinkID);
            else SinkManager.Instance.EnableSinkById(SinkID);
            DB.Instance.Save();
            return Task.CompletedTask;
        }

        // PATCH: Change the name of a sink;
        [HttpPatch("sink/rename")]
        public Task Rename([FromQuery] int SinkID, [FromQuery] string NewName)
        {
            SinkManager.Instance.SetSinkName(SinkID, NewName);
            DB.Instance.Save();
            return Task.CompletedTask;
        }

        // PATCH: Bind a sink to a source;
        [HttpPatch("sink/bind")]
        public Task Bind([FromQuery] int SinkID, [FromQuery] int SourceID)
        {
            SinkManager.Instance.BindSourceToSink(SinkID, SourceID);
            DB.Instance.Save();
            return Task.CompletedTask;
        }

        // sourceId is only required for multi-source sinks
        // PATCH: Unbind a sink from a source;
        [HttpPatch("sink/unbind")]
        public Task Unbind([FromQuery] int SinkID, [FromQuery] int? SourceID = null)
        {
            SinkManager.Instance.UnbindSourceFromSink(SinkID, SourceID);
            DB.Instance.Save();
            return Task.CompletedTask;
        }

        // DELETE: delete a certain sink;
        [HttpDelete("sink/delete")]
        public Task Delete([FromQuery] int SinkID)
        {
            try
            {
                SinkManager.Instance.DeleteSink(SinkID);
            }
            catch (Exception ex)
            {
                Console.WriteLine(ex.ToString());
                return Task.FromException(ex);
            }
            return Task.CompletedTask;
        }

        // GET: get all sinks;
        [HttpGet("sink/getAll")]
        public Task<List<Sink>> GetAll()
        {
            List<Sink> sinks = new List<Sink>();
            foreach (var sink in SinkManager.Instance.getAllSinkIds())
            {
                sinks.Add(SinkManager.Instance.GetSinkById(sink));
            }
            return Task.FromResult(sinks);
        }

        // POST: copy a detector node (ApriltagSink or ObjectDetectionSink) as "<name> copy"; with copyBindings it is bound to the same source.
        // Returns the new sink's id.
        [HttpPost("sink/duplicate")]
        public Task<int> Duplicate([FromQuery] int SinkID, [FromQuery] bool copyBindings = false)
        {
            return Task.FromResult(SinkManager.Instance.DuplicateSink(SinkID, copyBindings));
        }

        // PATCH: toggle driver mode on a detection sink (video still streams, detection/NT4 publishing is skipped).
        // Throws if the sink doesn't support it.
        [HttpPatch("sink/driverMode")]
        public Task SetDriverMode([FromQuery] int SinkID, [FromQuery] bool Enabled)
        {
            ManagerWrapper.Instance.SetDriverMode(SinkID, Enabled);
            return Task.CompletedTask;
        }

        // GET: whether a detection sink currently has driver mode enabled
        [HttpGet("sink/driverMode")]
        public Task<bool> GetDriverMode([FromQuery] int SinkID)
        {
            return Task.FromResult(ManagerWrapper.Instance.GetDriverMode(SinkID));
        }
    }
}
