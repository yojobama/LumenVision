#pragma once
#include <array>

// Conversions between the frames libapriltag/OpenCV work in and the WPILib conventions robot code expects.
//
//   OpenCV camera frame:   X right, Y down, Z forward.
//   WPILib camera frame:   X forward, Y left, Z up.
//   libapriltag tag frame: X right, Y down, Z into the tag, as seen by a viewer facing it (so a tag squarely facing the camera
//                          has the identity rotation in the OpenCV camera frame).
//   WPILib tag frame:      X out of the tag's face, Z up, Y = Z cross X (the frame an AprilTagFieldLayout pose describes).
//
// A Pose3 is a rotation (row-major 3x3) and a translation; a "camera to tag" pose maps tag-frame points into the camera frame.
namespace frames {

struct Pose3 {
	std::array<double, 9> R{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	std::array<double, 3> t{ 0, 0, 0 };
};

// libapriltag's camera-to-tag pose (OpenCV camera frame, libapriltag tag frame) in WPILib frames: the camera frame becomes
// X forward / Y left / Z up and the tag frame the field-layout one, so it composes with a field-layout tag pose directly.
Pose3 AprilTagPoseToWpilib(const Pose3& cameraToTagOpenCv);

// A camera's pose in the field as solvePnP-derived (columns of R are the OpenCV camera axes expressed in field coordinates)
// re-expressed with the camera frame as X forward / Y left / Z up.
Pose3 OpenCvCameraInFieldToWpilib(const Pose3& cameraInFieldOpenCv);

// The four corners of a tag of the given side length in the WPILib tag frame, in libapriltag's detection corner order
// (bottom-left, bottom-right, top-right, top-left as seen from the front). A tag lies in its local Y-Z plane.
std::array<std::array<double, 3>, 4> WpilibTagCorners(double tagSize);

// unit quaternion (w, x, y, z) of a row-major rotation matrix
std::array<double, 4> RotationToQuaternion(const std::array<double, 9>& R);

// row-major rotation matrix of a quaternion (w, x, y, z), normalised first
std::array<double, 9> QuaternionToRotation(double w, double x, double y, double z);

}
