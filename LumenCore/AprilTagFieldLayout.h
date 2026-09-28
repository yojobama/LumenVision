#pragma once
#include <opencv2/opencv.hpp>
#include <map>
#include <string>

// One tag's known pose in FIELD coordinates (WPILib's convention: +X downfield from the blue
// alliance wall, +Y toward the left wall as viewed from the blue alliance, +Z up).
struct AprilTagFieldPose {
	cv::Point3d translation;
	cv::Matx33d rotation;
};

// Loads WPILib's AprilTagFieldLayout JSON format directly. Only these fields are read; other
// metadata is ignored:
//   { "tags": [ { "ID": 1, "pose": { "translation": {"x":.., "y":.., "z":..},
//                                    "rotation": {"quaternion": {"W":..,"X":..,"Y":..,"Z":..}} }
//               }, ... ] }
//
// Used by ApriltagDetector's multi-tag PnP to solve one field-relative camera pose.
class AprilTagFieldLayout {
public:
	// Returns false (leaving the layout empty) on any parse failure; never throws.
	bool LoadFromFile(const std::string& jsonPath);

	bool TryGetTagPose(int id, AprilTagFieldPose& outPose) const;
	size_t size() const { return m_Tags.size(); }
	bool empty() const { return m_Tags.empty(); }

private:
	std::map<int, AprilTagFieldPose> m_Tags;
};
