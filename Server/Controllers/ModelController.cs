
using Microsoft.AspNetCore.Mvc;
using Server.Web;

namespace Server.Controllers
{
    // Upload/list/delete for YOLOv8/v11 detection models (ONNX or RKNN). The variant is a form field;
    // the provider (ONNX vs RKNN) is inferred from the file (see ModelManager.AddModel).
    internal class ModelController : ControllerBase
    {
        // POST multipart/form-data: fields "name", "variant" (0=YOLOv8, 1=YOLOv11),
        // "inputSize", "confThreshold", "nmsThreshold"; files "model" (.onnx or .rknn, required)
        // and "labels" (one class name per line, optional)
        [HttpPost("model/upload")]
        public async Task<int> Upload()
        {
            var form = await Request.ReadFormAsync(HttpContext.RequestAborted);

            string name = form["name"].ToString();
            var variant = (YoloVariant)int.Parse(form["variant"].ToString());
            int inputSize = int.TryParse(form["inputSize"], out var s) ? s : 640;
            float confThreshold = float.TryParse(form["confThreshold"], out var c) ? c : 0.25f;
            float nmsThreshold = float.TryParse(form["nmsThreshold"], out var n) ? n : 0.45f;

            var modelFile = form.Files.GetFile("model");
            if (modelFile == null) throw ApiException.BadRequest("missing required 'model' file");
            var labelsFile = form.Files.GetFile("labels");

            using var modelStream = modelFile.OpenReadStream();
            using var labelsStream = labelsFile?.OpenReadStream();
            var model = ModelManager.Instance.AddModel(
                name, Path.GetFileName(modelFile.FileName), modelStream,
                labelsFile == null ? null : Path.GetFileName(labelsFile.FileName), labelsStream,
                variant, inputSize, confThreshold, nmsThreshold);

            return model.Id;
        }

        [HttpGet("model/getAll")]
        public Task<List<Model>> GetAll()
        {
            return Task.FromResult(ModelManager.Instance.GetAllModels());
        }

        [HttpDelete("model/delete")]
        public Task Delete([FromQuery] int id)
        {
            ModelManager.Instance.DeleteModel(id);
            return Task.CompletedTask;
        }
    }
}
