#include "YoloPostProcess.h"
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <cmath>
#include <limits>

namespace YoloPostProcess {

cv::Mat Letterbox(const cv::Mat& bgrFrame, int targetWidth, int targetHeight, LetterboxInfo& outInfo)
{
	outInfo.originalWidth = bgrFrame.cols;
	outInfo.originalHeight = bgrFrame.rows;

	float scale = std::min(
		static_cast<float>(targetWidth) / bgrFrame.cols,
		static_cast<float>(targetHeight) / bgrFrame.rows);
	outInfo.scale = scale;

	int unpaddedWidth = static_cast<int>(std::round(bgrFrame.cols * scale));
	int unpaddedHeight = static_cast<int>(std::round(bgrFrame.rows * scale));

	cv::Mat resized;
	cv::resize(bgrFrame, resized, cv::Size(unpaddedWidth, unpaddedHeight));

	outInfo.padLeft = (targetWidth - unpaddedWidth) / 2;
	outInfo.padTop = (targetHeight - unpaddedHeight) / 2;
	int padRight = targetWidth - unpaddedWidth - outInfo.padLeft;
	int padBottom = targetHeight - unpaddedHeight - outInfo.padTop;

	cv::Mat padded;
	cv::copyMakeBorder(resized, padded, outInfo.padTop, padBottom, outInfo.padLeft, padRight,
		cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
	return padded;
}

std::vector<ObjectDetection> DecodeAndNms(
	const float* outputData,
	int numClasses,
	int numAnchors,
	const std::vector<std::string>& labels,
	const LetterboxInfo& letterbox,
	float confThreshold,
	float nmsThreshold)
{
	std::vector<cv::Rect> boxesForNms; // in letterboxed-frame pixel space, NMS only needs relative geometry
	std::vector<float> scores;
	std::vector<int> classIds;

	for (int anchor = 0; anchor < numAnchors; anchor++) {
		int bestClassId = -1;
		float bestScore = 0.0f;
		for (int c = 0; c < numClasses; c++) {
			float score = outputData[(4 + c) * numAnchors + anchor];
			if (score > bestScore) {
				bestScore = score;
				bestClassId = c;
			}
		}

		if (bestScore < confThreshold) continue;

		float cx = outputData[0 * numAnchors + anchor];
		float cy = outputData[1 * numAnchors + anchor];
		float w = outputData[2 * numAnchors + anchor];
		float h = outputData[3 * numAnchors + anchor];

		int left = static_cast<int>(std::round(cx - w / 2.0f));
		int top = static_cast<int>(std::round(cy - h / 2.0f));

		boxesForNms.emplace_back(left, top, static_cast<int>(std::round(w)), static_cast<int>(std::round(h)));
		scores.push_back(bestScore);
		classIds.push_back(bestClassId);
	}

	return NmsAndBuildDetections(boxesForNms, scores, classIds, labels, letterbox, confThreshold, nmsThreshold);
}

std::vector<ObjectDetection> NmsAndBuildDetections(
	const std::vector<cv::Rect>& boxesForNms,
	const std::vector<float>& scores,
	const std::vector<int>& classIds,
	const std::vector<std::string>& labels,
	const LetterboxInfo& letterbox,
	float confThreshold,
	float nmsThreshold)
{
	std::vector<int> keptIndices;
	cv::dnn::NMSBoxes(boxesForNms, scores, confThreshold, nmsThreshold, keptIndices);

	std::vector<ObjectDetection> detections;
	detections.reserve(keptIndices.size());

	for (int idx : keptIndices) {
		const cv::Rect& letterboxedBox = boxesForNms[idx];

		// map the box out of letterbox space back into the original frame's pixel coordinates
		double origLeft = (letterboxedBox.x - letterbox.padLeft) / letterbox.scale;
		double origTop = (letterboxedBox.y - letterbox.padTop) / letterbox.scale;
		double origWidth = letterboxedBox.width / letterbox.scale;
		double origHeight = letterboxedBox.height / letterbox.scale;

		// clamp to the original frame - a box near the letterbox padding can extend slightly
		// outside it after unscaling
		origLeft = std::clamp(origLeft, 0.0, static_cast<double>(letterbox.originalWidth));
		origTop = std::clamp(origTop, 0.0, static_cast<double>(letterbox.originalHeight));
		origWidth = std::min(origWidth, letterbox.originalWidth - origLeft);
		origHeight = std::min(origHeight, letterbox.originalHeight - origTop);

		ObjectDetection detection;
		detection.SetBoundingBox(cv::Rect2d(origLeft, origTop, origWidth, origHeight));
		detection.SetConfidence(scores[idx]);
		detection.SetClassId(classIds[idx]);
		detection.SetClassName(classIds[idx] < static_cast<int>(labels.size()) ? labels[classIds[idx]] : std::to_string(classIds[idx]));
		detections.push_back(detection);
	}

	return detections;
}

namespace {
	inline float Sigmoid(float x)
	{
		return 1.0f / (1.0f + std::exp(-x));
	}
}

std::vector<ObjectDetection> DecodeDflMultiScaleAndNms(
	const std::vector<DflScaleOutput>& scales,
	int regMax,
	int numClasses,
	const std::vector<std::string>& labels,
	const LetterboxInfo& letterbox,
	float confThreshold,
	float nmsThreshold)
{
	std::vector<cv::Rect> boxesForNms;
	std::vector<float> scores;
	std::vector<int> classIds;

	for (const DflScaleOutput& scale : scales) {
		const int gridH = scale.gridH, gridW = scale.gridW, stride = scale.stride;
		const int cellCount = gridH * gridW;

		for (int gy = 0; gy < gridH; gy++) {
			for (int gx = 0; gx < gridW; gx++) {
				int cell = gy * gridW + gx;

				int bestClassId = -1;
				float bestLogit = -std::numeric_limits<float>::infinity();
				for (int c = 0; c < numClasses; c++) {
					float logit = scale.clsData[c * cellCount + cell];
					if (logit > bestLogit) {
						bestLogit = logit;
						bestClassId = c;
					}
				}
				float bestScore = Sigmoid(bestLogit);
				if (bestScore < confThreshold) continue;

				// DFL: each of the 4 sides (left, top, right, bottom) is a softmax distribution over regMax bins;
				// distance = sum(bin_index * softmax(logits)[bin_index]), the expected value rather than an argmax.
				float distance[4];
				for (int side = 0; side < 4; side++) {
					const float* binLogits = scale.boxData + static_cast<size_t>(side) * regMax * cellCount;

					float maxLogit = -std::numeric_limits<float>::infinity();
					for (int b = 0; b < regMax; b++) maxLogit = std::max(maxLogit, binLogits[b * cellCount + cell]);

					float sumExp = 0.0f;
					std::vector<float> expVals(regMax);
					for (int b = 0; b < regMax; b++) {
						expVals[b] = std::exp(binLogits[b * cellCount + cell] - maxLogit);
						sumExp += expVals[b];
					}

					float expected = 0.0f;
					for (int b = 0; b < regMax; b++) expected += b * (expVals[b] / sumExp);
					distance[side] = expected;
				}

				float cx = (gx + 0.5f) * stride;
				float cy = (gy + 0.5f) * stride;
				float x1 = cx - distance[0] * stride;
				float y1 = cy - distance[1] * stride;
				float x2 = cx + distance[2] * stride;
				float y2 = cy + distance[3] * stride;

				boxesForNms.emplace_back(
					static_cast<int>(std::round(x1)), static_cast<int>(std::round(y1)),
					static_cast<int>(std::round(x2 - x1)), static_cast<int>(std::round(y2 - y1)));
				scores.push_back(bestScore);
				classIds.push_back(bestClassId);
			}
		}
	}

	return NmsAndBuildDetections(boxesForNms, scores, classIds, labels, letterbox, confThreshold, nmsThreshold);
}

}
