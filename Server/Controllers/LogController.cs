using System;
using System.IO;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Read-only, bounded view of the server's diagnostic log (DBLog.txt, the Logger instance in DB.cs).
    internal class LogController : ControllerBase
    {
        // Matches DB.cs's `new Logger("DBLog.txt")`: a relative filename resolved against the
        // process working directory.
        private const string LogFilePath = "DBLog.txt";

        // GET: the last `lines` log entries, oldest first; empty (not an error) if the file doesn't exist yet.
        [HttpGet("log/tail")]
        public async Task<string[]> Tail([FromQuery] int lines = 200)
        {
            if (!System.IO.File.Exists(LogFilePath)) return Array.Empty<string>();

            string[] all = await System.IO.File.ReadAllLinesAsync(LogFilePath);
            return all.Length <= lines ? all : all[^lines..];
        }
    }
}
