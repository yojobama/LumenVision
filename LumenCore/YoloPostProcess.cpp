#include "YoloPostProcess.h"
#include <opencv2/dnn.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace YoloPostProcess {

bool LooksLikeEndToEndOutput(long long a, long long b)
{
	const long long channels = std::min(a, b), rows = std::max(a, b);
	return channels == 6 && rows <= 1000;
}

cv::Mat Letterbox(const cv::Mat& bgrFrame, int targetWidth, int targetHeight, LetterboxInfo& outInfo)
{
	if (bgrFrame.cols <= 0 || bgrFrame.rows <= 0 || targetWidth <= 0 || targetHeight <= 0) {
		throw std::invalid_argument("YoloPostProcess::Letterbox: the frame and the model input must have a positive size");
	}
	outInfo.originalWidth = bgrFrame.cols;
	outInfo.originalHeight = bgrFrame.rows;

	float scale = std::min(
		static_cast<float>(targetWidth) / bgrFrame.cols,
		static_cast<float>(targetHeight) / bgrFrame.rows);
	outInfo.scale = scale;

	// at least one pixel each way: a very thin frame (1920x1) scales to a fraction of a pixel, and cv::resize throws for an empty target
	int unpaddedWidth = std::max(1, static_cast<int>(std::round(bgrFrame.cols * scale)));
	int unpaddedHeight = std::max(1, static_cast<int>(std::round(bgrFrame.rows * scale)));

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

cv::Rect LetterboxLayout(int srcWidth, int srcHeight, int targetWidth, int targetHeight, LetterboxInfo& outInfo)
{
	if (srcWidth <= 0 || srcHeight <= 0 || targetWidth <= 0 || targetHeight <= 0) {
		throw std::invalid_argument("YoloPostProcess::LetterboxLayout: the frame and the model input must have a positive size");
	}
	outInfo.originalWidth = srcWidth;
	outInfo.originalHeight = srcHeight;

	float scale = std::min(
		static_cast<float>(targetWidth) / srcWidth,
		static_cast<float>(targetHeight) / srcHeight);
	outInfo.scale = scale;

	int unpaddedWidth = std::max(1, static_cast<int>(std::round(srcWidth * scale)));
	int unpaddedHeight = std::max(1, static_cast<int>(std::round(srcHeight * scale)));
	outInfo.padLeft = (targetWidth - unpaddedWidth) / 2;
	outInfo.padTop = (targetHeight - unpaddedHeight) / 2;
	return cv::Rect(outInfo.padLeft, outInfo.padTop, unpaddedWidth, unpaddedHeight);
}

void LetterboxInto(const cv::Mat& bgrFrame, int targetWidth, int targetHeight, LetterboxInfo& outInfo, cv::Mat& canvas)
{
	const cv::Rect picture = LetterboxLayout(bgrFrame.cols, bgrFrame.rows, targetWidth, targetHeight, outInfo);
	const int unpaddedWidth = picture.width, unpaddedHeight = picture.height;

	// the border only needs painting when the canvas is new or the picture's place in it has moved: the previous picture is then
	// not guaranteed to cover the same pixels
	const bool newLayout = canvas.empty() || canvas.cols != targetWidth || canvas.rows != targetHeight ||
		canvas.type() != bgrFrame.type() || !canvas.isContinuous() || canvas.u == nullptr;
	static thread_local cv::Rect lastPicture; // per thread: each backend runs on its own sink thread
	if (newLayout || picture != lastPicture) {
		canvas.create(targetHeight, targetWidth, bgrFrame.type());
		canvas.setTo(cv::Scalar(114, 114, 114));
		lastPicture = picture;
	}
	cv::Mat target = canvas(picture);
	cv::resize(bgrFrame, target, cv::Size(unpaddedWidth, unpaddedHeight));
}

std::vector<float> ToChannelMajor(const float* anchorMajor, int numAnchors, int channels)
{
	std::vector<float> channelMajor(static_cast<size_t>(numAnchors) * channels);
	for (int anchor = 0; anchor < numAnchors; anchor++)
		for (int channel = 0; channel < channels; channel++)
			channelMajor[static_cast<size_t>(channel) * numAnchors + anchor] = anchorMajor[static_cast<size_t>(anchor) * channels + channel];
	return channelMajor;
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
	std::vector<cv::Rect2d> boxesForNms; // in letterboxed-frame pixel space, NMS only needs relative geometry
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

		// bestClassId stays -1 when no class beat a score of 0 (a cutoff of 0 or less lets such an anchor through, and -1 indexes the labels)
		if (bestClassId < 0 || bestScore < confThreshold) continue;

		float cx = outputData[0 * numAnchors + anchor];
		float cy = outputData[1 * numAnchors + anchor];
		float w = outputData[2 * numAnchors + anchor];
		float h = outputData[3 * numAnchors + anchor];

		boxesForNms.emplace_back(cx - w / 2.0f, cy - h / 2.0f, w, h);
		scores.push_back(bestScore);
		classIds.push_back(bestClassId);
	}

	return NmsAndBuildDetections(boxesForNms, scores, classIds, labels, letterbox, confThreshold, nmsThreshold);
}

std::vector<ObjectDetection> NmsAndBuildDetections(
	const std::vector<cv::Rect2d>& boxesForNms,
	const std::vector<float>& scores,
	const std::vector<int>& classIds,
	const std::vector<std::string>& labels,
	const LetterboxInfo& letterbox,
	float confThreshold,
	float nmsThreshold)
{
	// a box with a side or a score that is not a finite number (a corrupt output, or a size so large the arithmetic overflows) is no detection:
	// NMS would keep it and the clipping below turns it into NaN coordinates
	std::vector<cv::Rect2d> finiteBoxes;
	std::vector<float> finiteScores;
	std::vector<int> finiteClassIds;
	finiteBoxes.reserve(boxesForNms.size());
	for (size_t i = 0; i < boxesForNms.size(); i++) {
		const cv::Rect2d& box = boxesForNms[i];
		if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.width) || !std::isfinite(box.height) || !std::isfinite(scores[i])) continue;
		finiteBoxes.push_back(box);
		finiteScores.push_back(scores[i]);
		finiteClassIds.push_back(classIds[i]);
	}
	const std::vector<cv::Rect2d>& boxes = finiteBoxes;
	const std::vector<float>& boxScores = finiteScores;
	const std::vector<int>& boxClassIds = finiteClassIds;

	std::vector<int> keptIndices;
	cv::dnn::NMSBoxesBatched(boxes, boxScores, boxClassIds, confThreshold, nmsThreshold, keptIndices);

	std::vector<ObjectDetection> detections;
	detections.reserve(keptIndices.size());

	for (int idx : keptIndices) {
		const cv::Rect2d& letterboxedBox = boxes[idx];

		// map the box out of letterbox space back into the original frame's pixel coordinates
		double left = (letterboxedBox.x - letterbox.padLeft) / letterbox.scale;
		double top = (letterboxedBox.y - letterbox.padTop) / letterbox.scale;
		double right = (letterboxedBox.x + letterboxedBox.width - letterbox.padLeft) / letterbox.scale;
		double bottom = (letterboxedBox.y + letterboxedBox.height - letterbox.padTop) / letterbox.scale;

		// clip each edge to the original frame - a box near the letterbox padding can extend outside it after unscaling,
		// and clipping the origin alone would leave the far edge where it was
		const double frameWidth = letterbox.originalWidth, frameHeight = letterbox.originalHeight;
		double origLeft = std::clamp(left, 0.0, frameWidth);
		double origTop = std::clamp(top, 0.0, frameHeight);
		double origWidth = std::clamp(right, 0.0, frameWidth) - origLeft;
		double origHeight = std::clamp(bottom, 0.0, frameHeight) - origTop;
		if (!(origWidth > 0.0 && origHeight > 0.0) || !std::isfinite(origLeft) || !std::isfinite(origTop)) continue; // entirely in the padding, or not a number

		ObjectDetection detection;
		detection.SetBoundingBox(cv::Rect2d(origLeft, origTop, origWidth, origHeight));
		detection.SetConfidence(boxScores[idx]);
		detection.SetClassId(boxClassIds[idx]);
		detection.SetClassName(boxClassIds[idx] >= 0 && boxClassIds[idx] < static_cast<int>(labels.size()) ? labels[boxClassIds[idx]] : std::to_string(boxClassIds[idx]));
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

bool ClassScoresAreProbabilities(const std::vector<DflScaleOutput>& scales, int numClasses)
{
	// The rknn_model_zoo export applies the class sigmoid inside the graph; other recipes leave raw logits. Probabilities
	// stay within [0,1] while logits of a real model go well below 0 (background cells sit around -5 to -10), so the range tells them apart.
	for (const DflScaleOutput& scale : scales) {
		const size_t count = static_cast<size_t>(scale.gridH) * scale.gridW * numClasses;
		for (size_t i = 0; i < count; i++)
			if (scale.clsData[i] < -0.05f || scale.clsData[i] > 1.05f) return false; // a little slack for dequantisation noise
	}
	return true;
}

bool QuantisedClassScoresAreProbabilities(float scale, int zeroPoint)
{
	// the real values an int8 tensor can hold; a little slack as in ClassScoresAreProbabilities
	const float lowest = (-128 - zeroPoint) * scale;
	const float highest = (127 - zeroPoint) * scale;
	return lowest >= -0.05f && highest <= 1.05f;
}

std::vector<ObjectDetection> DecodeDflMultiScaleInt8AndNms(
	const std::vector<DflScaleOutputInt8>& scales,
	int regMax,
	int numClasses,
	const std::vector<std::string>& labels,
	const LetterboxInfo& letterbox,
	float confThreshold,
	float nmsThreshold,
	bool classScoresAreProbabilities)
{
	std::vector<cv::Rect2d> boxesForNms;
	std::vector<float> scores;
	std::vector<int> classIds;
	std::vector<float> expVals(static_cast<size_t>(std::max(regMax, 1)));
	std::vector<int8_t> bestRaw;
	std::vector<int> bestClass;

	for (const DflScaleOutputInt8& scale : scales) {
		const int gridW = scale.gridW, stride = scale.stride;
		const int cellCount = scale.gridH * scale.gridW;

		// best class per cell: one pass per class over contiguous cells (the tensor is [classes, cells])
		bestRaw.assign(static_cast<size_t>(cellCount), std::numeric_limits<int8_t>::min());
		bestClass.assign(static_cast<size_t>(cellCount), 0);
		for (int c = 0; c < numClasses; c++) {
			const int8_t* classRow = scale.clsData + static_cast<size_t>(c) * cellCount;
			for (int cell = 0; cell < cellCount; cell++) {
				if (classRow[cell] > bestRaw[cell]) {
					bestRaw[cell] = classRow[cell];
					bestClass[cell] = c;
				}
			}
		}

		// a cell can only pass if its raw class value reaches the threshold: probabilities directly, logits through the sigmoid's inverse
		const float realThreshold = classScoresAreProbabilities ? confThreshold : std::log(confThreshold / (1.0f - confThreshold));
		const float rawThresholdReal = realThreshold / scale.clsScale + scale.clsZeroPoint;
		const int rawThreshold = static_cast<int>(std::floor(std::min(std::max(rawThresholdReal, -129.0f), 128.0f)));

		for (int cell = 0; cell < cellCount; cell++) {
			if (static_cast<int>(bestRaw[cell]) < rawThreshold) continue;
			const float logit = (static_cast<int>(bestRaw[cell]) - scale.clsZeroPoint) * scale.clsScale;
			const float bestScore = classScoresAreProbabilities ? logit : Sigmoid(logit);
			if (bestScore < confThreshold) continue;

			float distance[4];
			for (int side = 0; side < 4; side++) {
				const int8_t* binLogits = scale.boxData + static_cast<size_t>(side) * regMax * cellCount;

				float maxLogit = -std::numeric_limits<float>::infinity();
				for (int b = 0; b < regMax; b++) {
					const float value = (static_cast<int>(binLogits[b * cellCount + cell]) - scale.boxZeroPoint) * scale.boxScale;
					expVals[b] = value;
					maxLogit = std::max(maxLogit, value);
				}
				float sumExp = 0.0f;
				for (int b = 0; b < regMax; b++) {
					expVals[b] = std::exp(expVals[b] - maxLogit);
					sumExp += expVals[b];
				}
				float expected = 0.0f;
				for (int b = 0; b < regMax; b++) expected += b * (expVals[b] / sumExp);
				distance[side] = expected;
			}

			const int gx = cell % gridW, gy = cell / gridW;
			const float cx = (gx + 0.5f) * stride;
			const float cy = (gy + 0.5f) * stride;
			const float x1 = cx - distance[0] * stride;
			const float y1 = cy - distance[1] * stride;
			const float x2 = cx + distance[2] * stride;
			const float y2 = cy + distance[3] * stride;

			boxesForNms.emplace_back(x1, y1, x2 - x1, y2 - y1);
			scores.push_back(bestScore);
			classIds.push_back(bestClass[cell]);
		}
	}

	return NmsAndBuildDetections(boxesForNms, scores, classIds, labels, letterbox, confThreshold, nmsThreshold);
}

std::vector<ObjectDetection> DecodeDflMultiScaleAndNms(
	const std::vector<DflScaleOutput>& scales,
	int regMax,
	int numClasses,
	const std::vector<std::string>& labels,
	const LetterboxInfo& letterbox,
	float confThreshold,
	float nmsThreshold,
	std::optional<bool> knownClassScoresAreProbabilities)
{
	std::vector<cv::Rect2d> boxesForNms;
	std::vector<float> scores;
	std::vector<int> classIds;

	const bool classScoresAreProbabilities = knownClassScoresAreProbabilities.has_value()
		? *knownClassScoresAreProbabilities
		: ClassScoresAreProbabilities(scales, numClasses);

	std::vector<float> expVals(static_cast<size_t>(std::max(regMax, 1)));

	for (const DflScaleOutput& scale : scales) {
		const int gridH = scale.gridH, gridW = scale.gridW, stride = scale.stride;
		const int cellCount = gridH * gridW;

		for (int gy = 0; gy < gridH; gy++) {
			for (int gx = 0; gx < gridW; gx++) {
				int cell = gy * gridW + gx;

				int bestClassId = -1;
				float bestLogit = -std::numeric_limits<float>::infinity(); // the best class's raw output (a probability or a logit)
				for (int c = 0; c < numClasses; c++) {
					float logit = scale.clsData[c * cellCount + cell];
					if (logit > bestLogit) {
						bestLogit = logit;
						bestClassId = c;
					}
				}
				float bestScore = classScoresAreProbabilities ? bestLogit : Sigmoid(bestLogit);
				if (bestClassId < 0 || bestScore < confThreshold) continue;

				// DFL: each of the 4 sides (left, top, right, bottom) is a softmax distribution over regMax bins;
				// distance = sum(bin_index * softmax(logits)[bin_index]), the expected value rather than an argmax.
				float distance[4];
				for (int side = 0; side < 4; side++) {
					const float* binLogits = scale.boxData + static_cast<size_t>(side) * regMax * cellCount;

					float maxLogit = -std::numeric_limits<float>::infinity();
					for (int b = 0; b < regMax; b++) maxLogit = std::max(maxLogit, binLogits[b * cellCount + cell]);

					float sumExp = 0.0f;
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

				boxesForNms.emplace_back(x1, y1, x2 - x1, y2 - y1);
				scores.push_back(bestScore);
				classIds.push_back(bestClassId);
			}
		}
	}

	return NmsAndBuildDetections(boxesForNms, scores, classIds, labels, letterbox, confThreshold, nmsThreshold);
}

}
