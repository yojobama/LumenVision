using System.Text.Json;

namespace Server
{
    // manages uploaded object-detection models under ./models, persisted to models.json
    public class ModelManager
    {
        public static ModelManager Instance { get; } = new ModelManager();

        private readonly string manifestPath = Path.Combine("models", "models.json");
        private readonly List<Model> models = new List<Model>();
        private int nextId = 1;

        private ModelManager()
        {
            Directory.CreateDirectory("models");
            Load();
        }

        private void Load()
        {
            if (!File.Exists(manifestPath)) return;
            try
            {
                var loaded = JsonSerializer.Deserialize<List<Model>>(File.ReadAllText(manifestPath));
                if (loaded != null)
                {
                    models.AddRange(loaded);
                    nextId = models.Count > 0 ? models.Max(m => m.Id) + 1 : 1;
                }
            }
            catch (Exception) { /* corrupt manifest - start empty rather than crash the server */ }
        }

        private void Save()
        {
            File.WriteAllText(manifestPath, JsonSerializer.Serialize(models, new JsonSerializerOptions { WriteIndented = true }));
        }

        public Model AddModel(string name, string modelFileName, Stream modelData, string? labelsFileName, Stream? labelsData,
            YoloVariant variant, int inputSize, float confThreshold, float nmsThreshold)
        {
            int id = nextId++;
            string modelPath = Path.Combine("models", $"{id}_{modelFileName}");
            using (var output = File.Create(modelPath)) modelData.CopyTo(output);

            string labelsPath = "";
            if (labelsData != null && labelsFileName != null)
            {
                labelsPath = Path.Combine("models", $"{id}_{labelsFileName}");
                using var output = File.Create(labelsPath);
                labelsData.CopyTo(output);
            }

            // .rknn is the only format RknnDetectionBackend reads; everything else (.onnx) goes to ONNX Runtime.
            // Sink creation reads the provider back from the model.
            ObjectDetectionProvider provider = Path.GetExtension(modelFileName).Equals(".rknn", StringComparison.OrdinalIgnoreCase)
                ? ObjectDetectionProvider.RKNN
                : ObjectDetectionProvider.ONNX;

            var model = new Model
            {
                Id = id,
                Name = name,
                ModelPath = modelPath,
                LabelsPath = labelsPath,
                Variant = variant,
                InputSize = inputSize,
                ConfThreshold = confThreshold,
                NmsThreshold = nmsThreshold,
                Provider = provider,
            };
            models.Add(model);
            Save();
            return model;
        }

        public Model? GetModel(int id) => models.FirstOrDefault(m => m.Id == id);
        public List<Model> GetAllModels() => models;

        // Renames a model and/or changes the confidence and NMS cutoffs new detectors start with (each null = unchanged). The input size is part of the
        // network and cannot change after upload. Detectors already running on the model are retuned by SinkManager.ApplyModelThresholds.
        public Model UpdateModel(int id, string? name, float? confThreshold, float? nmsThreshold)
        {
            var model = GetModel(id) ?? throw Server.Web.ApiException.NotFound($"no model with id {id}");
            if (confThreshold is < 0.01f or > 1.0f) throw Server.Web.ApiException.BadRequest("confThreshold must be 0.01 to 1");
            if (nmsThreshold is < 0.01f or > 1.0f) throw Server.Web.ApiException.BadRequest("nmsThreshold must be 0.01 to 1");
            if (name != null && string.IsNullOrWhiteSpace(name)) throw Server.Web.ApiException.BadRequest("the name must not be empty");

            if (name != null) model.Name = name.Trim();
            if (confThreshold.HasValue) model.ConfThreshold = confThreshold.Value;
            if (nmsThreshold.HasValue) model.NmsThreshold = nmsThreshold.Value;
            Save();
            return model;
        }

        public void DeleteModel(int id)
        {
            var model = GetModel(id);
            if (model == null) return;
            try { if (File.Exists(model.ModelPath)) File.Delete(model.ModelPath); } catch (Exception) { }
            try { if (!string.IsNullOrEmpty(model.LabelsPath) && File.Exists(model.LabelsPath)) File.Delete(model.LabelsPath); } catch (Exception) { }
            models.Remove(model);
            Save();
        }
    }
}
