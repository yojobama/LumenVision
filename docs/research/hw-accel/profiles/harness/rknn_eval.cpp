// Accuracy harness for a YOLO .rknn model on the board, independent of LumenVision: runs every image of a pre-letterboxed raw RGB file
// (N x H x W x 3 uint8, made by rknn_eval_prep.py) through the model and writes the detections in letterbox pixels, one per line:
// "image class score x1 y1 x2 y2". Decoding follows Ultralytics' validator: multi-label candidates above CONF, the 30000 best kept,
// class-wise NMS at IOU, at most 300 per image. Takes the multi-scale split head (box DFL / class / score sum per scale, the rknn_model_zoo
// export) and the fused single head ([1, 4 + classes, anchors]).
// Build on the board: g++ -O2 -std=c++17 rknn_eval.cpp -I<dir with rknn_api.h> -L/opt/lumenvision -lrknnrt -Wl,-rpath,/opt/lumenvision -o rknn_eval
// Usage: rknn_eval MODEL.rknn IMAGES.raw COUNT OUT.txt [CONF=0.001] [IOU=0.7]
#include "rknn_api.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

struct Candidate {
	float x1, y1, x2, y2, score;
	int cls;
};

static float Iou(const Candidate& a, const Candidate& b)
{
	const float iw = std::max(0.f, std::min(a.x2, b.x2) - std::max(a.x1, b.x1));
	const float ih = std::max(0.f, std::min(a.y2, b.y2) - std::max(a.y1, b.y1));
	const float inter = iw * ih;
	const float uni = (a.x2 - a.x1) * (a.y2 - a.y1) + (b.x2 - b.x1) * (b.y2 - b.y1) - inter;
	return uni > 0 ? inter / uni : 0.f;
}

static std::vector<Candidate> Nms(std::vector<Candidate>& c, float iou)
{
	if (c.size() > 30000) {
		std::nth_element(c.begin(), c.begin() + 30000, c.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
		c.resize(30000);
	}
	std::sort(c.begin(), c.end(), [](const Candidate& a, const Candidate& b) { return a.score > b.score; });
	std::vector<Candidate> kept;
	std::vector<char> removed(c.size(), 0);
	for (size_t i = 0; i < c.size() && kept.size() < 300; i++) {
		if (removed[i]) continue;
		kept.push_back(c[i]);
		for (size_t j = i + 1; j < c.size(); j++)
			if (!removed[j] && c[j].cls == c[i].cls && Iou(c[i], c[j]) > iou) removed[j] = 1;
	}
	return kept;
}

int main(int argc, char** argv)
{
	if (argc < 5) {
		std::fprintf(stderr, "usage: rknn_eval MODEL.rknn IMAGES.raw COUNT OUT.txt [CONF=0.001] [IOU=0.7]\n");
		return 2;
	}
	const std::string modelPath = argv[1], rawPath = argv[2];
	const int count = std::atoi(argv[3]);
	const float conf = argc > 5 ? static_cast<float>(std::atof(argv[5])) : 0.001f;
	const float iouThreshold = argc > 6 ? static_cast<float>(std::atof(argv[6])) : 0.7f;

	std::ifstream file(modelPath, std::ios::binary | std::ios::ate);
	if (!file) { std::fprintf(stderr, "cannot read %s\n", modelPath.c_str()); return 1; }
	std::vector<char> model(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	file.read(model.data(), static_cast<std::streamsize>(model.size()));

	rknn_context ctx = 0;
	if (rknn_init(&ctx, model.data(), static_cast<uint32_t>(model.size()), 0, nullptr) != RKNN_SUCC) { std::fprintf(stderr, "rknn_init failed\n"); return 1; }
	rknn_set_core_mask(ctx, RKNN_NPU_CORE_0);

	rknn_input_output_num io{};
	rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io));
	rknn_tensor_attr in{};
	in.index = 0;
	rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in, sizeof(in));
	const int h = in.fmt == RKNN_TENSOR_NHWC ? in.dims[1] : in.dims[2];
	const int w = in.fmt == RKNN_TENSOR_NHWC ? in.dims[2] : in.dims[3];
	std::vector<rknn_tensor_attr> outAttrs(io.n_output);
	for (uint32_t i = 0; i < io.n_output; i++) {
		outAttrs[i].index = i;
		rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &outAttrs[i], sizeof(outAttrs[i]));
	}
	const bool split = io.n_output % 3 == 0 && io.n_output >= 3;
	if (!split && io.n_output != 1) { std::fprintf(stderr, "unrecognised output layout (%u outputs)\n", io.n_output); return 1; }

	const size_t imageBytes = static_cast<size_t>(w) * h * 3;
	std::ifstream raw(rawPath, std::ios::binary);
	if (!raw) { std::fprintf(stderr, "cannot read %s\n", rawPath.c_str()); return 1; }
	std::vector<uint8_t> image(imageBytes);
	FILE* out = std::fopen(argv[4], "w");
	if (!out) { std::fprintf(stderr, "cannot write %s\n", argv[4]); return 1; }

	double runMs = 0;
	for (int n = 0; n < count; n++) {
		if (!raw.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(imageBytes))) { std::fprintf(stderr, "raw file ends at image %d\n", n); return 1; }
		rknn_input input{};
		input.index = 0;
		input.type = RKNN_TENSOR_UINT8;
		input.fmt = RKNN_TENSOR_NHWC;
		input.size = static_cast<uint32_t>(imageBytes);
		input.buf = image.data();
		rknn_inputs_set(ctx, 1, &input);
		const auto t0 = std::chrono::steady_clock::now();
		if (rknn_run(ctx, nullptr) != RKNN_SUCC) { std::fprintf(stderr, "rknn_run failed at image %d\n", n); return 1; }
		runMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::vector<rknn_output> outputs(io.n_output);
		for (auto& o : outputs) o.want_float = 1;
		rknn_outputs_get(ctx, io.n_output, outputs.data(), nullptr);

		std::vector<Candidate> candidates;
		if (split) {
			for (uint32_t s = 0; s < io.n_output; s += 3) {
				const rknn_tensor_attr& boxAttr = outAttrs[s];
				const int bins = static_cast<int>(boxAttr.dims[1]) / 4;
				const int classes = static_cast<int>(outAttrs[s + 1].dims[1]);
				const int gh = static_cast<int>(boxAttr.dims[2]), gw = static_cast<int>(boxAttr.dims[3]);
				const float stride = static_cast<float>(h) / gh;
				const float* box = static_cast<const float*>(outputs[s].buf);
				const float* cls = static_cast<const float*>(outputs[s + 1].buf);
				const float* sum = static_cast<const float*>(outputs[s + 2].buf);
				const int plane = gh * gw;
				for (int cell = 0; cell < plane; cell++) {
					if (sum[cell] < conf) continue;
					bool any = false;
					for (int c = 0; c < classes && !any; c++) any = cls[c * plane + cell] > conf;
					if (!any) continue;
					float dist[4];
					for (int side = 0; side < 4; side++) {
						float maxLogit = -1e30f;
						for (int b = 0; b < bins; b++) maxLogit = std::max(maxLogit, box[(side * bins + b) * plane + cell]);
						float denom = 0, numer = 0;
						for (int b = 0; b < bins; b++) {
							const float e = std::exp(box[(side * bins + b) * plane + cell] - maxLogit);
							denom += e;
							numer += e * b;
						}
						dist[side] = numer / denom;
					}
					const float cx = (cell % gw) + 0.5f, cy = (cell / gw) + 0.5f;
					for (int c = 0; c < classes; c++) {
						const float score = cls[c * plane + cell];
						if (score <= conf) continue;
						candidates.push_back({ (cx - dist[0]) * stride, (cy - dist[1]) * stride, (cx + dist[2]) * stride, (cy + dist[3]) * stride, score, c });
					}
				}
			}
		} else {
			const rknn_tensor_attr& a = outAttrs[0];
			const int channels = static_cast<int>(a.n_dims == 3 ? a.dims[1] : a.dims[0]);
			const int anchors = static_cast<int>(a.n_dims == 3 ? a.dims[2] : a.dims[1]);
			const float* p = static_cast<const float*>(outputs[0].buf);
			for (int k = 0; k < anchors; k++) {
				const float cx = p[k], cy = p[anchors + k], bw = p[2 * anchors + k], bh = p[3 * anchors + k];
				for (int c = 0; c < channels - 4; c++) {
					const float score = p[(4 + c) * anchors + k];
					if (score <= conf) continue;
					candidates.push_back({ cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2, score, c });
				}
			}
		}
		rknn_outputs_release(ctx, io.n_output, outputs.data());

		for (const Candidate& d : Nms(candidates, iouThreshold))
			std::fprintf(out, "%d %d %.5f %.2f %.2f %.2f %.2f\n", n, d.cls, d.score, d.x1, d.y1, d.x2, d.y2);
	}
	std::fclose(out);
	std::printf("model=%s images=%d mean_run_ms=%.2f layout=%s\n", modelPath.substr(modelPath.find_last_of('/') + 1).c_str(), count, runMs / count,
		split ? "split" : "fused");
	rknn_destroy(ctx);
	return 0;
}
