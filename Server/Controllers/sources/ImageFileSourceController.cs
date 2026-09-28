using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers.sources
{
    internal class ImageFileSourceController : ControllerBase 
    {
        // GET: All ImageFile sources;
        [HttpGet("imageFileSource/get")]
        public Task<Source[]> GetAll()
        {
            List<Source> sources = new List<Source>();

            foreach (var item in SourceManager.Instance.GetAllSourceIds())
            {
                Source source = SourceManager.Instance.GetSourceById(item);
                if (source.Type == SourceType.ImageFile)
                    sources.Add(source);
            }
            return Task.FromResult(sources.ToArray());
        }

        // POST: Create ImageFile Sources from provided files;
        [HttpPost("imageFileSource/create")]
        public async Task<int[]> Create()
        {
            var form = await Request.ReadFormAsync(HttpContext.RequestAborted);
            List<int> created = new List<int>();

            foreach (var file in form.Files)
            {
                if (file != null)
                {
                    // GetFileName: the client-supplied name is untrusted - never let it escape images/
                    string fileName = Path.GetFileName(file.FileName);

                    Directory.CreateDirectory("images");

                    string savedPath = Path.Combine("images", fileName);
                    using (var output = System.IO.File.Create(savedPath))
                    {
                        await file.CopyToAsync(output, HttpContext.RequestAborted);
                    }
                    // Native cv::imread must run after the FileStream is closed: on Windows its exclusive lock blocks a second reader.
                    created.Add(SourceManager.Instance.initializeImageFileSource(savedPath, Path.GetFileNameWithoutExtension(fileName)));
                }
            }
            return created.ToArray();
        }

        
    }
}
