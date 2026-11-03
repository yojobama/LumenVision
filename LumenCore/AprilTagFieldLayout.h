#pragma once
#include <opencv2/opencv.hpp>
#include <map>
#include <memory>
#include <mutex>
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
//
// Safe to reload while another thread looks tags up (the web UI loads a layout while the detector runs): a load builds the new tag
// table on the side and swaps it in, and a lookup works on the table it picked up.
class AprilTagFieldLayout {
public:
	// Returns false (leaving the layout empty) on any parse failure; never throws. A tag whose quaternion is zero or not finite is skipped;
	// any other quaternion is normalised, as WPILib's Rotation3d does.
	bool LoadFromFile(const std::string& jsonPath);

	bool TryGetTagPose(int id, AprilTagFieldPose& outPose) const;
	size_t size() const { return Table()->size(); }
	bool empty() const { return Table()->empty(); }

private:
	using TagTable = std::map<int, AprilTagFieldPose>;
	std::shared_ptr<const TagTable> Table() const;

	mutable std::mutex m_Mutex;
	std::shared_ptr<const TagTable> m_Tags = std::make_shared<const TagTable>();
};
