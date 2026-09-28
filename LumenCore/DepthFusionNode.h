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

private:
	void Process(const std::vector<SourceResult>& results) override;

	std::shared_ptr<Logger> m_Logger;
	std::shared_ptr<StereoDepthNode> m_DepthNode;
};
