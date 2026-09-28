// Standalone timing tool that calls the backend classes directly (bypassing Server/Manager) and
// prints a markdown table. Backends lacking their hardware or build flag are reported as skipped.

#include "CpuApriltagBackend.h"
#ifdef LUMEN_WITH_VULKAN_APRILTAG
#include "VkApriltagBackend.h"
#endif
#include "SgbmStereoBackend.h"
#ifdef LUMEN_WITH_CODEC_STEREO
#include "CodecStereoBackend.h"
#endif
#ifdef LUMEN_WITH_ONNX
#include "OnnxDetectionBackend.h"
#endif

#include <opencv2/opencv.hpp>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr int ITERATIONS = 50;

struct BenchResult {
	std::string name;
	double meanMs = 0.0;
	bool skipped = false;
	std::string note;
};

template<typename Fn>
BenchResult RunBench(std::string name, Fn&& fn) {
	fn(); // warm-up run, excluded from the average
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < ITERATIONS; i++) fn();
	auto end = std::chrono::steady_clock::now();
	double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
	return { std::move(name), totalMs / ITERATIONS, false, "" };
}

BenchResult Skipped(std::string name, std::string note) {
	return { std::move(name), 0.0, true, std::move(note) };
}

// Synthetic stereo pair: blocky noise cropped/shifted for a known disparity (as in
// test_sgbm_synthetic_disparity.cpp).
void MakeSyntheticStereoPair(int width, int height, int shift, cv::Mat& left, cv::Mat& right) {
	const int texelSize = 8;
	cv::Mat coarse((height + texelSize - 1) / texelSize, (width + shift + texelSize - 1) / texelSize, CV_8UC1);
	cv::randu(coarse, 0, 255);
	cv::Mat base;
	cv::resize(coarse, base, cv::Size(width + shift, height), 0, 0, cv::INTER_NEAREST);
	left = base(cv::Rect(shift, 0, width, height)).clone();
	right = base(cv::Rect(0, 0, width, height)).clone();
}

} // namespace

int main() {
	std::vector<BenchResult> results;

	// --- AprilTag detection ---
	cv::Mat gray = cv::imread(std::string(LUMEN_VKAPRILTAG_SAMPLE_DIR) + "/grayimage.pgm", cv::IMREAD_GRAYSCALE);
	if (gray.empty()) {
		results.push_back(Skipped("CpuApriltagBackend", "grayimage.pgm fixture not found"));
		results.push_back(Skipped("VkApriltagBackend", "grayimage.pgm fixture not found"));
	} else {
		CpuApriltagBackend cpuTag(1, 2.0f);
		results.push_back(RunBench("CpuApriltagBackend", [&] {
			zarray_t* d = cpuTag.Detect(gray);
			cpuTag.ReleaseResult(d);
		}));

#ifdef LUMEN_WITH_VULKAN_APRILTAG
		try {
			VkApriltagBackend vkTag(gray.cols, gray.rows);
			results.push_back(RunBench("VkApriltagBackend", [&] {
				zarray_t* d = vkTag.Detect(gray);
				vkTag.ReleaseResult(d);
			}));
		} catch (const std::exception& e) {
			results.push_back(Skipped("VkApriltagBackend", std::string("no Vulkan compute device: ") + e.what()));
		}
#else
		results.push_back(Skipped("VkApriltagBackend", "not compiled in this build (LUMEN_WITH_VULKAN_APRILTAG off)"));
#endif
	}

	// --- Stereo depth ---
	{
		cv::Mat left, right;
		MakeSyntheticStereoPair(640, 384, 32, left, right);

		SgbmStereoBackend sgbm(16, 16, 16, 32);
		results.push_back(RunBench("SgbmStereoBackend", [&] {
			std::vector<float> disparity; int cols = 0, rows = 0;
			sgbm.Compute(left, right, disparity, cols, rows);
		}));

#ifdef LUMEN_WITH_CODEC_STEREO
		CodecStereoBackend::Config lavcCfg;
		lavcCfg.kind = STEREO_BACKEND_CODEC_LAVC;
		lavcCfg.searchRangeX = 48;
		lavcCfg.searchRangeY = 16;
		try {
			CodecStereoBackend lavc(lavcCfg);
			results.push_back(RunBench("codec-stereo lavc_sw", [&] {
				std::vector<float> disparity; int cols = 0, rows = 0;
				lavc.Compute(left, right, disparity, cols, rows);
			}));
		} catch (const std::exception& e) {
			results.push_back(Skipped("codec-stereo lavc_sw", e.what()));
		}

		CodecStereoBackend::Config rkmppCfg = lavcCfg;
		rkmppCfg.kind = STEREO_BACKEND_CODEC_RKMPP_HWENC;
		try {
			CodecStereoBackend rkmpp(rkmppCfg);
			results.push_back(RunBench("codec-stereo rkmpp_hwenc", [&] {
				std::vector<float> disparity; int cols = 0, rows = 0;
				rkmpp.Compute(left, right, disparity, cols, rows);
			}));
		} catch (const std::exception& e) {
			results.push_back(Skipped("codec-stereo rkmpp_hwenc", std::string("no RK3588 VPU: ") + e.what()));
		}
#else
		results.push_back(Skipped("codec-stereo lavc_sw", "not compiled in this build (LUMEN_WITH_CODEC_STEREO off)"));
		results.push_back(Skipped("codec-stereo rkmpp_hwenc", "not compiled in this build (LUMEN_WITH_CODEC_STEREO off)"));
#endif
	}

	// --- Object detection ---
#ifdef LUMEN_WITH_ONNX
	{
		DetectionBackendConfig cfg;
		cfg.modelPath = std::string(LUMEN_TEST_DATA_DIR) + "/yolov8n.onnx";
		cfg.labelsPath = std::string(LUMEN_TEST_DATA_DIR) + "/coco_80_labels.txt";
		cfg.variant = YOLOv8;
		cv::Mat bus = cv::imread(std::string(LUMEN_TEST_DATA_DIR) + "/bus.jpg");

		OnnxDetectionBackend onnx;
		if (bus.empty() || !onnx.Load(cfg)) {
			results.push_back(Skipped("OnnxDetectionBackend", "failed to load yolov8n.onnx/bus.jpg fixtures"));
		} else {
			results.push_back(RunBench("OnnxDetectionBackend", [&] {
				onnx.Infer(bus);
			}));
		}
	}
#else
	results.push_back(Skipped("OnnxDetectionBackend", "not compiled in this build (LUMEN_WITH_ONNX off)"));
#endif

	// No RKNN invocation code.
	results.push_back(Skipped("RknnDetectionBackend", "not yet implemented in lumen-bench - needs writing and verifying together on the Pi (real NPU + RKNN model required)"));

	std::printf("| Backend | Mean time (ms, %d iterations) | Notes |\n", ITERATIONS);
	std::printf("|---|---|---|\n");
	for (const BenchResult& r : results) {
		if (r.skipped) {
			std::printf("| %s | - | skipped: %s |\n", r.name.c_str(), r.note.c_str());
		} else {
			std::printf("| %s | %.3f | |\n", r.name.c_str(), r.meanMs);
		}
	}
	return 0;
}
