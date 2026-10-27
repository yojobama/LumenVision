// NPU profiling harness for an .rknn model, independent of LumenVision: per-run NPU time (RKNN_QUERY_PERF_RUN), the per-layer report
// (RKNN_QUERY_PERF_DETAIL), and the wall time of a whole inference (input, run, outputs) for a chosen core mask and input path.
// Build on the board: g++ -O2 -std=c++17 rknn_perf.cpp -I<dir with rknn_api.h> -L/opt/lumenvision -lrknnrt -Wl,-rpath,/opt/lumenvision -o rknn_perf
// Usage: rknn_perf MODEL.rknn ITERATIONS CORES(auto|0|01|012) INPUT(set|mem) [DETAIL_OUT.txt]
#include "rknn_api.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static rknn_core_mask ParseCores(const std::string& s)
{
	if (s == "0") return RKNN_NPU_CORE_0;
	if (s == "01") return RKNN_NPU_CORE_0_1;
	if (s == "012") return RKNN_NPU_CORE_0_1_2;
	return RKNN_NPU_CORE_AUTO;
}

int main(int argc, char** argv)
{
	if (argc < 5) {
		std::fprintf(stderr, "usage: rknn_perf MODEL.rknn ITERATIONS CORES(auto|0|01|012) INPUT(set|mem) [DETAIL_OUT.txt]\n");
		return 2;
	}
	const std::string modelPath = argv[1];
	const int iterations = std::atoi(argv[2]);
	const std::string cores = argv[3], inputMode = argv[4];
	const char* detailOut = argc > 5 ? argv[5] : nullptr;

	std::ifstream file(modelPath, std::ios::binary | std::ios::ate);
	if (!file) { std::fprintf(stderr, "cannot read %s\n", modelPath.c_str()); return 1; }
	std::vector<char> model(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	file.read(model.data(), static_cast<std::streamsize>(model.size()));

	rknn_context ctx = 0;
	const uint32_t flags = detailOut ? RKNN_FLAG_COLLECT_PERF_MASK : 0;
	if (rknn_init(&ctx, model.data(), static_cast<uint32_t>(model.size()), flags, nullptr) != RKNN_SUCC) { std::fprintf(stderr, "rknn_init failed\n"); return 1; }
	if (rknn_set_core_mask(ctx, ParseCores(cores)) != RKNN_SUCC) std::fprintf(stderr, "rknn_set_core_mask(%s) failed; running on AUTO\n", cores.c_str());

	rknn_input_output_num io{};
	rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io, sizeof(io));
	rknn_tensor_attr in{};
	in.index = 0;
	rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &in, sizeof(in));
	const int h = in.fmt == RKNN_TENSOR_NHWC ? in.dims[1] : in.dims[2];
	const int w = in.fmt == RKNN_TENSOR_NHWC ? in.dims[2] : in.dims[3];
	const size_t inputBytes = static_cast<size_t>(w) * h * 3;
	std::vector<uint8_t> image(inputBytes);
	for (size_t i = 0; i < inputBytes; i++) image[i] = static_cast<uint8_t>((i * 2654435761u) >> 24); // a busy pattern, not zeros

	// zero-copy input: the runtime's own buffer, written once; the "set" path copies the image in on every run
	rknn_tensor_mem* mem = nullptr;
	if (inputMode == "mem") {
		rknn_tensor_attr native{};
		native.index = 0;
		rknn_query(ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &native, sizeof(native));
		native.type = RKNN_TENSOR_UINT8;
		native.fmt = RKNN_TENSOR_NHWC;
		mem = rknn_create_mem(ctx, native.size_with_stride);
		if (!mem || rknn_set_io_mem(ctx, mem, &native) != RKNN_SUCC) { std::fprintf(stderr, "zero-copy input not available\n"); return 1; }
		const int stride = native.w_stride > 0 ? native.w_stride : w;
		for (int y = 0; y < h; y++)
			std::memcpy(static_cast<uint8_t*>(mem->virt_addr) + static_cast<size_t>(y) * stride * 3, image.data() + static_cast<size_t>(y) * w * 3, static_cast<size_t>(w) * 3);
		rknn_mem_sync(ctx, mem, RKNN_MEMORY_SYNC_TO_DEVICE);
	}

	auto infer = [&](double& npuUs) {
		if (!mem) {
			rknn_input input{};
			input.index = 0;
			input.type = RKNN_TENSOR_UINT8;
			input.fmt = RKNN_TENSOR_NHWC;
			input.size = static_cast<uint32_t>(inputBytes);
			input.buf = image.data();
			rknn_inputs_set(ctx, 1, &input);
		}
		if (rknn_run(ctx, nullptr) != RKNN_SUCC) { std::fprintf(stderr, "rknn_run failed\n"); std::exit(1); } // else failed calls get timed
		rknn_perf_run run{};
		if (rknn_query(ctx, RKNN_QUERY_PERF_RUN, &run, sizeof(run)) == RKNN_SUCC) npuUs = static_cast<double>(run.run_duration);
		std::vector<rknn_output> outputs(io.n_output);
		for (auto& o : outputs) o.want_float = 0;
		rknn_outputs_get(ctx, io.n_output, outputs.data(), nullptr);
		rknn_outputs_release(ctx, io.n_output, outputs.data());
	};

	double npuUs = 0;
	for (int i = 0; i < 10; i++) infer(npuUs); // warm-up
	double npuSum = 0;
	const auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < iterations; i++) { infer(npuUs); npuSum += npuUs; }
	const double wallMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / iterations;
	std::printf("model=%s input=%dx%d cores=%s input_path=%s iterations=%d wall_ms=%.2f npu_run_ms=%.2f\n",
		modelPath.substr(modelPath.find_last_of('/') + 1).c_str(), w, h, cores.c_str(), inputMode.c_str(), iterations, wallMs, npuSum / iterations / 1000.0);

	if (detailOut) {
		rknn_perf_detail detail{};
		if (rknn_query(ctx, RKNN_QUERY_PERF_DETAIL, &detail, sizeof(detail)) == RKNN_SUCC && detail.perf_data) {
			std::ofstream(detailOut) << std::string(detail.perf_data, detail.data_len);
		}
	}
	if (mem) rknn_destroy_mem(ctx, mem);
	rknn_destroy(ctx);
	return 0;
}
