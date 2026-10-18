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

	// What a detector publishes, with range added, given a depth grid (rows x cols blocks of blockW x blockH pixels, metres; <= 0 = no depth).
	// Two shapes are understood, and the result keeps the shape it was given:
	//   - ObjectDetectionSink: an array of {"box":[x,y,w,h], ...}
	//   - ApriltagDetector: {"tags":[{"corners":[[x,y] x4], ...}], ...}; each tag's box is the bounding box of its corners
	// Each detection gains distanceMeters (median of the valid blocks under its box; 0 when none), xMeters/yMeters (its centre projected at that
	// range with the rectified focal length fx and principal point cx, cy) and depthValidFraction. Anything else is returned unchanged.
	// `boxes`, when given, receives each fused detection's box (x, y, w, h) and distance, for annotating a frame.
	struct FusedBox { double x, y, w, h, distanceMeters; };
	static nlohmann::json FuseDetections(const nlohmann::json& detections, const std::vector<float>& depth, int cols, int rows,
		int blockW, int blockH, double fx, double cx, double cy, std::vector<FusedBox>* boxes = nullptr);

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::shared_ptr<Logger> m_Logger;
	std::shared_ptr<StereoDepthNode> m_DepthNode;
};
