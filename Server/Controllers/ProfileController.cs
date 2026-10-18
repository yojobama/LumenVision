using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;

namespace Server.Controllers
{
    // Per-stage timings of each node (LumenCore's StageProfile); see Manager::GetNodeProfilesJson for the stage names.
    internal class ProfileController : ControllerBase
    {
        // GET: every node that has produced timings, with its name and kind
        [HttpGet("profile")]
        public Task<NodeProfileDto[]> GetAll()
        {
            return Task.FromResult(NodeProfileReader.Read(ManagerWrapper.Instance.GetNodeProfilesJson()));
        }

        // POST: forget the recorded timings, to measure a change from a clean start
        [HttpPost("profile/reset")]
        public Task Reset()
        {
            ManagerWrapper.Instance.ResetNodeProfiles();
            return Task.CompletedTask;
        }
    }

    public record struct StageStatDto(string Stage, long Count, double MeanMs, double P50Ms, double P95Ms, double MaxMs, double LastMs);

    public record struct NodeProfileDto(int Id, string Name, string Kind, string Type, StageStatDto[] Stages);

    // turns the native {"<id>": [{stage, count, meanMs, ...}]} into one entry per node, named from the source and sink lists
    public static class NodeProfileReader
    {
        public static NodeProfileDto[] Read(string json)
        {
            using JsonDocument document = JsonDocument.Parse(json);
            var nodes = new List<NodeProfileDto>();
            foreach (JsonProperty node in document.RootElement.EnumerateObject())
            {
                if (!int.TryParse(node.Name, out int id)) continue;
                StageStatDto[] stages = node.Value.EnumerateArray().Select(s => new StageStatDto(
                    s.GetProperty("stage").GetString() ?? "", s.GetProperty("count").GetInt64(),
                    s.GetProperty("meanMs").GetDouble(), s.GetProperty("p50Ms").GetDouble(), s.GetProperty("p95Ms").GetDouble(),
                    s.GetProperty("maxMs").GetDouble(), s.GetProperty("lastMs").GetDouble())).ToArray();
                if (stages.Length == 0) continue;

                // a dual-role detector is both a sink and a source: the sink record carries its type
                Sink? sink = SinkManager.Instance.GetSinkById(id);
                Source? source = SourceManager.Instance.GetSourceById(id);
                string name = sink?.Name ?? source?.Name ?? $"#{id}";
                string kind = sink != null ? "sink" : source != null ? "source" : "node";
                string type = sink != null ? sink.Type.ToString() : source != null ? source.Type.ToString() : "";
                nodes.Add(new NodeProfileDto(id, name, kind, type, stages));
            }
            return nodes.OrderBy(n => n.Kind).ThenBy(n => n.Name).ToArray();
        }
    }
}
