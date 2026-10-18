#pragma once
#include "ISource.h"
#include "ISink.h"
#include "StereoDepthNode.h"
#include <memory>

// ISource+ISink, maxSources=1: fuses a detector's (ObjectDetectionSink or ApriltagDetector) bounding boxes with a
// StereoDepthNode's block-grid depth to add range. Runs on each fresh detection against the current depth, read in-process
// via SetStereoDepthNode/GetLastDepthGrid(). The detector MUST be bound to the depth node's rectified-left output
// (STEREO_FRAME_RECTIFIED_LEFT) so its bbox pixels index the same grid; this is not verified.
class DepthFusionNode : public ISink, public ISource
{
public:
	DepthFusionNode(std::shared_ptr<Logger> logger, std::string id);

	void SetStereoDepthNode(std::shared_ptr<StereoDepthNode> depthNode);

	// Adds range to each detection of an object-detection array or an AprilTag {"tags":[...]} envelope, from a depth grid of blocks (metres, <= 0 = none).
	// Each gains distanceMeters, xMeters, yMeters and depthValidFraction; `boxes`, when given, receives the fused boxes and distances.
	struct FusedBox { double x, y, w, h, distanceMeters; };
	static nlohmann::json FuseDetections(const nlohmann::json& detections, const std::vector<float>& depth, int cols, int rows,
		int blockW, int blockH, double fx, double cx, double cy, std::vector<FusedBox>* boxes = nullptr);

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::shared_ptr<Logger> m_Logger;
	std::shared_ptr<StereoDepthNode> m_DepthNode;
};
