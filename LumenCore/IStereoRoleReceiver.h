#pragma once
#include <string>

// Implemented by any node whose two bound sources have a fixed left/right meaning (StereoCalibrator, StereoDepthNode).
// ISink::BindSource only records bind order, and swapped roles flip every disparity sign (all blocks invalid), so
// Manager::BindStereoSources also dynamic_casts to this interface to record explicit roles by source ID.
class IStereoRoleReceiver {
public:
	virtual ~IStereoRoleReceiver() = default;
	virtual void SetStereoRoles(const std::string& leftSourceId, const std::string& rightSourceId) = 0;
};
