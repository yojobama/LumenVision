#include "DepthFusionNode.h"
#include <algorithm>
#include <cmath>

DepthFusionNode::DepthFusionNode(std::shared_ptr<Logger> logger, std::string id)
	: ISink(logger, 1, true, false, id), ISource(logger, id), m_Logger(logger)
{
	m_DoNotLoadCaptureThread = true;
}

void DepthFusionNode::SetStereoDepthNode(std::shared_ptr<StereoDepthNode> depthNode)
{
	m_DepthNode = depthNode;
}

nlohmann::json DepthFusionNode::FuseDetections(const nlohmann::json& detections, const std::vector<float>& depth, int cols, int rows,
	int blockW, int blockH, double fx, double cx, double cy, std::vector<FusedBox>* boxes)
{
	// adds range to one detection whose box is (bx, by, bw, bh)
	auto fuseOne = [&](nlohmann::json detection, double bx, double by, double bw, double bh) {
		int blockX0 = std::max(0, (int)(bx / blockW));
		int blockY0 = std::max(0, (int)(by / blockH));
		int blockX1 = std::min(cols, (int)std::ceil((bx + bw) / blockW));
		int blockY1 = std::min(rows, (int)std::ceil((by + bh) / blockH));

		std::vector<float> validDepths;
		for (int by2 = blockY0; by2 < blockY1; by2++) {
			for (int bx2 = blockX0; bx2 < blockX1; bx2++) {
				float d = depth[(size_t)by2 * cols + bx2];
				if (d > 0.0f) validDepths.push_back(d);
			}
		}

		// median, not mean, so one background block in the box doesn't skew the result
		double distanceMeters = 0.0;
		const int blockCount = (blockX1 - blockX0) * (blockY1 - blockY0);
		double validFraction = validDepths.empty() || blockCount <= 0 ? 0.0 : (double)validDepths.size() / blockCount;
		if (!validDepths.empty()) {
			std::nth_element(validDepths.begin(), validDepths.begin() + validDepths.size() / 2, validDepths.end());
			distanceMeters = validDepths[validDepths.size() / 2];
		}

		// pixel-centre pinhole projection assuming fy=fx (true after stereoRectify); equivalent to the
		// Q-matrix reprojection for a point on the rectified left image plane
		double u = bx + bw / 2.0, v = by + bh / 2.0;
		double xMeters = (fx > 0 && distanceMeters > 0) ? (u - cx) * distanceMeters / fx : 0.0;
		double yMeters = (fx > 0 && distanceMeters > 0) ? (v - cy) * distanceMeters / fx : 0.0;

		detection["distanceMeters"] = distanceMeters;
		detection["xMeters"] = xMeters;
		detection["yMeters"] = yMeters;
		detection["depthValidFraction"] = validFraction;
		if (boxes) boxes->push_back({ bx, by, bw, bh, distanceMeters });
		return detection;
	};

	// ApriltagDetector's envelope: keep it whole (calibration, multiTag, ...) and add range to each tag
	if (detections.is_object() && detections.contains("tags") && detections["tags"].is_array()) {
		nlohmann::json output = detections;
		for (auto& tag : output["tags"]) {
			if (!tag.contains("corners") || !tag["corners"].is_array() || tag["corners"].empty()) continue;
			double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
			for (const auto& corner : tag["corners"]) {
				if (!corner.is_array() || corner.size() != 2) continue;
				minX = std::min(minX, corner[0].get<double>()); maxX = std::max(maxX, corner[0].get<double>());
				minY = std::min(minY, corner[1].get<double>()); maxY = std::max(maxY, corner[1].get<double>());
			}
			if (minX > maxX || minY > maxY) continue;
			tag = fuseOne(tag, minX, minY, maxX - minX, maxY - minY);
		}
		return output;
	}

	// ObjectDetectionSink: an array of boxes
	if (!detections.is_array()) return detections;
	std::vector<nlohmann::json> fused;
	for (const auto& detection : detections) {
		if (!detection.contains("box") || detection["box"].size() != 4) continue;
		fused.push_back(fuseOne(detection, detection["box"][0], detection["box"][1], detection["box"][2], detection["box"][3]));
	}
	return nlohmann::json(fused);
}

void DepthFusionNode::Process(const std::vector<SourceResult>& results)
{
	if (results.empty() || !results[0].json.has_value()) return;
	if (!m_DepthNode) {
		if (m_Logger) m_Logger->EnterLog(LogLevel::Error, "DepthFusionNode: no StereoDepthNode attached - call SetStereoDepthNode first.");
		return;
	}

	std::vector<float> depth;
	int cols, rows, blockW, blockH;
	if (!m_DepthNode->GetLastDepthGrid(depth, cols, rows, blockW, blockH)) return;

	StereoCalibrationResult calibration = m_DepthNode->GetCalibration();
	// detections are in the cropped output frame, so the principal point moves by the crop's origin
	int cropX = 0, cropY = 0;
	m_DepthNode->GetCropOrigin(cropX, cropY);
	double fx = calibration.rectifiedFx, cx = calibration.rectifiedCx - cropX, cy = calibration.rectifiedCy - cropY;

	// annotate-on-demand: skips the clone and distance labels when nothing bound wants the frame
	// (see ApriltagDetector::Process).
	bool wantsFrame = HasActiveFrameConsumer();

	cv::Mat annotatedFrame;
	if (wantsFrame && results[0].frame.has_value() && !results[0].frame->empty()) annotatedFrame = results[0].frame->AsBgr().clone();

	std::vector<FusedBox> boxes;
	nlohmann::json fused = FuseDetections(results[0].json.value(), depth, cols, rows, blockW, blockH, fx, cx, cy, &boxes);

	if (!annotatedFrame.empty()) {
		for (const FusedBox& box : boxes) {
			if (box.distanceMeters <= 0.0) continue;
			std::string label = std::to_string(box.distanceMeters).substr(0, 4) + "m";
			cv::putText(annotatedFrame, label, cv::Point((int)box.x, (int)(box.y + box.h) + 15),
				cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0xff, 0xff), 1);
		}
	}

	SetLatestResult(SourceResult(fused, annotatedFrame));
}
