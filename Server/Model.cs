namespace Server
{
    // a stored YOLOv8/v11 model (ONNX or RKNN export), uploaded via ModelController and
    // referenced by id when creating an ObjectDetectionSink
    public class Model
    {
        public int Id { get; set; }
        public string Name { get; set; } = "";
        public string ModelPath { get; set; } = "";
        public string LabelsPath { get; set; } = "";
        public YoloVariant Variant { get; set; }
        public int InputSize { get; set; } = 640;
        public float ConfThreshold { get; set; } = 0.25f;
        public float NmsThreshold { get; set; } = 0.45f;

        // the backend this model runs on, derived at upload from the file extension (see ModelManager.AddModel)
        public ObjectDetectionProvider Provider { get; set; }
    }
}
