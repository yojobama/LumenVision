using System.Collections.Generic;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Save/restore/list whole-graph profiles (see GraphProfile.cs).
    internal class GraphProfileController : ControllerBase
    {
        // GET: every saved whole-graph profile name.
        [HttpGet("graphProfile/list")]
        public Task<List<string>> List()
        {
            return Task.FromResult(GraphProfile.Instance.ListProfiles());
        }

        // POST: snapshot the live graph (every source, sink and binding) under `name`,
        // overwriting any existing profile with that name.
        [HttpPost("graphProfile/saveCurrentAs")]
        public Task SaveCurrentAs([FromQuery] string name)
        {
            GraphProfile.Instance.SaveCurrentAs(name);
            return Task.CompletedTask;
        }

        // POST: tear down the live graph and rebuild it from the named profile;
        // irreversible unless the current graph was saved first.
        [HttpPost("graphProfile/activate")]
        public Task Activate([FromQuery] string name)
        {
            GraphProfile.Instance.Activate(name);
            return Task.CompletedTask;
        }
    }
}
