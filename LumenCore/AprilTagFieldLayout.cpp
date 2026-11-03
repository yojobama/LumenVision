#include "AprilTagFieldLayout.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>

namespace {
	// Quaternion (w,x,y,z) to 3x3 rotation matrix, as in WPILib's Rotation3d(Quaternion), which normalises first: a layout file is hand-edited
	// and a quaternion that is a little off unit length would otherwise give a matrix that is not a rotation. False for a zero or non-finite one.
	bool QuaternionToRotationMatrix(double w, double x, double y, double z, cv::Matx33d& out)
	{
		const double norm = std::sqrt(w * w + x * x + y * y + z * z);
		if (!std::isfinite(norm) || norm < 1e-12) return false;
		w /= norm; x /= norm; y /= norm; z /= norm;
		out = cv::Matx33d(
			1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
			2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
			2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y)
		);
		return true;
	}
}

bool AprilTagFieldLayout::LoadFromFile(const std::string& jsonPath)
{
	// built on the side and swapped in at the end, so a lookup on another thread never sees a table being changed (or freed)
	auto tags = std::make_shared<TagTable>();
	auto publish = [&] {
		std::lock_guard<std::mutex> guard(m_Mutex);
		m_Tags = tags;
	};

	std::ifstream file(jsonPath);
	if (!file) { publish(); return false; }

	nlohmann::json root;
	try {
		file >> root;
	} catch (const nlohmann::json::exception&) {
		publish();
		return false;
	}

	if (!root.contains("tags") || !root["tags"].is_array()) { publish(); return false; }

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
			if (!QuaternionToRotationMatrix(
				quaternionJson.at("W").get<double>(),
				quaternionJson.at("X").get<double>(),
				quaternionJson.at("Y").get<double>(),
				quaternionJson.at("Z").get<double>(), fieldPose.rotation)) continue;
			(*tags)[id] = fieldPose;
		} catch (const nlohmann::json::exception&) {
			// Skip a malformed tag entry rather than rejecting the whole file.
			continue;
		}
	}

	const bool any = !tags->empty();
	publish();
	return any;
}

std::shared_ptr<const AprilTagFieldLayout::TagTable> AprilTagFieldLayout::Table() const
{
	std::lock_guard<std::mutex> guard(m_Mutex);
	return m_Tags;
}

bool AprilTagFieldLayout::TryGetTagPose(int id, AprilTagFieldPose& outPose) const
{
	const auto table = Table();
	auto it = table->find(id);
	if (it == table->end()) return false;
	outPose = it->second;
	return true;
}
