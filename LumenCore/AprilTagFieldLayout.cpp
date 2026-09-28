#include "AprilTagFieldLayout.h"
#include <nlohmann/json.hpp>
#include <fstream>

namespace {
	// Unit quaternion (w,x,y,z) to 3x3 rotation matrix, as in WPILib's Rotation3d(Quaternion).
	cv::Matx33d QuaternionToRotationMatrix(double w, double x, double y, double z)
	{
		return cv::Matx33d(
			1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
			2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
			2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)
		);
	}
}

bool AprilTagFieldLayout::LoadFromFile(const std::string& jsonPath)
{
	m_Tags.clear();

	std::ifstream file(jsonPath);
	if (!file) return false;

	nlohmann::json root;
	try {
		file >> root;
	} catch (const nlohmann::json::exception&) {
		return false;
	}

	if (!root.contains("tags") || !root["tags"].is_array()) return false;

	for (const auto& tagJson : root["tags"]) {
		if (!tagJson.contains("ID") || !tagJson.contains("pose")) continue;
		const auto& poseJson = tagJson["pose"];
		if (!poseJson.contains("translation") || !poseJson.contains("rotation")) continue;
		const auto& translationJson = poseJson["translation"];
		const auto& rotationJson = poseJson["rotation"];
		if (!rotationJson.contains("quaternion")) continue;
		const auto& quaternionJson = rotationJson["quaternion"];

		try {
			int id = tagJson["ID"].get<int>();
			AprilTagFieldPose fieldPose;
			fieldPose.translation = cv::Point3d(
				translationJson.at("x").get<double>(),
				translationJson.at("y").get<double>(),
				translationJson.at("z").get<double>());
			fieldPose.rotation = QuaternionToRotationMatrix(
				quaternionJson.at("W").get<double>(),
				quaternionJson.at("X").get<double>(),
				quaternionJson.at("Y").get<double>(),
				quaternionJson.at("Z").get<double>());
			m_Tags[id] = fieldPose;
		} catch (const nlohmann::json::exception&) {
			// Skip a malformed tag entry rather than rejecting the whole file.
			continue;
		}
	}

	return !m_Tags.empty();
}

bool AprilTagFieldLayout::TryGetTagPose(int id, AprilTagFieldPose& outPose) const
{
	auto it = m_Tags.find(id);
	if (it == m_Tags.end()) return false;
	outPose = it->second;
	return true;
}
