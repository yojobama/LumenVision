using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sources
{
    internal class VideoFileSourceController : ControllerBase
    {
        // GET: All VideoFile sources;
        [HttpGet("videoFileSource/getAll")]
        public Task<Source[]> GetAll()
        {
            List<Source> sources = new List<Source>();

            foreach (var item in SourceManager.Instance.GetAllSourceIds())
            {
                Source source = SourceManager.Instance.GetSourceById(item);
                if (source.Type == SourceType.VideoFile)
                    sources.Add(source);
            }
            return Task.FromResult(sources.ToArray());
        }

        // POST: Create VideoFile Sources from all provided files with a default FPS of 30;
        [HttpPost("videoFileSource/create")]
        public async Task<int[]> Create()
        {
            var form = await Request.ReadFormAsync(HttpContext.RequestAborted);
            List<int> created = new List<int>();

            foreach(var file in form.Files)
            {
                if (file != null)
                {
                    // GetFileName: the client-supplied name is untrusted - never let it escape videos/
                    string fileName = Path.GetFileName(file.FileName);

                    Directory.CreateDirectory("videos");

                    string savedPath = Path.Combine("videos", fileName);
                    using (var output = System.IO.File.Create(savedPath))
                    {
                        await file.CopyToAsync(output, HttpContext.RequestAborted);
                    }
                    // Native code must not open the file until the upload's FileStream is closed (see ImageFileSourceController.Create).
                    created.Add(SourceManager.Instance.InitializeVideoFileSource(savedPath, 30, Path.GetFileNameWithoutExtension(fileName)));
                }
            }
            return created.ToArray();
        }

        // PATCH: Change VideoFile FPS;
        [HttpPatch("videoFileSource/changeFPS")]
        public Task ChangeFPS([FromQuery] int fps)
        {
            return Task.CompletedTask;
        }

    }
}
