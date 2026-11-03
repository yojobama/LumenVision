# Builds small .rknn models with layouts RknnDetectionBackend does not expect (run in WSL with rknn-toolkit2 2.5.0).
import sys, os, numpy as np, onnx, cv2
from onnx import helper, TensorProto, numpy_helper
out_dir, bus = sys.argv[1], sys.argv[2]
work = os.path.join(out_dir, "_work"); os.makedirs(work, exist_ok=True)
rng = np.random.default_rng(0)

def init(name, arr): return numpy_helper.from_array(arr.astype(np.float32), name)

def yolov5_shape(size):
    # rknn_model_zoo yolov5 head: three raw outputs [1,255,S/8,S/8], [1,255,S/16,S/16], [1,255,S/32,S/32]
    w0 = rng.normal(0, 0.05, (255, 3, 8, 8)); w1 = rng.normal(0, 0.3, (255, 1, 2, 2)); w2 = rng.normal(0, 0.3, (255, 1, 2, 2))
    nodes = [helper.make_node("Conv", ["images", "w0"], ["o0"], kernel_shape=[8, 8], strides=[8, 8]),
             helper.make_node("Conv", ["o0", "w1"], ["o1"], kernel_shape=[2, 2], strides=[2, 2], group=255),
             helper.make_node("Conv", ["o1", "w2"], ["o2"], kernel_shape=[2, 2], strides=[2, 2], group=255)]
    g = helper.make_graph(nodes, "yolov5shape", [helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, size, size])],
        [helper.make_tensor_value_info("o0", TensorProto.FLOAT, [1, 255, size // 8, size // 8]),
         helper.make_tensor_value_info("o1", TensorProto.FLOAT, [1, 255, size // 16, size // 16]),
         helper.make_tensor_value_info("o2", TensorProto.FLOAT, [1, 255, size // 32, size // 32])],
        [init("w0", w0), init("w1", w1), init("w2", w2)])
    return g

def vector_input_fused():
    # not an image model: a [1,16] input, one fused-looking [1,84,8] output
    w = rng.normal(0, 0.1, (16, 84 * 8))
    nodes = [helper.make_node("MatMul", ["x", "w"], ["m"]),
             helper.make_node("Reshape", ["m", "shape"], ["out"])]
    g = helper.make_graph(nodes, "vecfused", [helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 16])],
        [helper.make_tensor_value_info("out", TensorProto.FLOAT, [1, 84, 8])],
        [init("w", w), numpy_helper.from_array(np.array([1, 84, 8], dtype=np.int64), "shape")])
    return g

def save(g, name):
    m = helper.make_model(g, opset_imports=[helper.make_opsetid("", 12)]); m.ir_version = 7
    onnx.checker.check_model(m); p = os.path.join(work, name + ".onnx"); onnx.save(m, p); return p

from rknn.api import RKNN
def convert(onnx_path, out, quant, size, image=True):
    r = RKNN(verbose=False)
    if image: r.config(mean_values=[[0, 0, 0]], std_values=[[255, 255, 255]], target_platform="rk3588")
    else: r.config(target_platform="rk3588")
    assert r.load_onnx(model=onnx_path) == 0
    ds = None
    if quant:
        img = cv2.resize(cv2.imread(bus), (size, size)); ip = os.path.join(work, f"cal{size}.jpg"); cv2.imwrite(ip, img)
        ds = os.path.join(work, f"ds{size}.txt"); open(ds, "w").write(ip + "\n")
    assert r.build(do_quantization=quant, dataset=ds) == 0
    assert r.export_rknn(os.path.join(out_dir, out)) == 0
    r.release()

convert(save(yolov5_shape(320), "v5"), "yolov5shape_320_i8.rknn", True, 320)
convert(save(yolov5_shape(320), "v5f"), "yolov5shape_320_fp16.rknn", False, 320)
convert(save(vector_input_fused(), "vec"), "vector_input_fused_fp16.rknn", False, 0, image=False)
print("done")
