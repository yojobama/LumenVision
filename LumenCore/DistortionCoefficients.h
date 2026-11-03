#pragma once

#include <cmath>
#include <vector>

// OpenCV's undistortPoints / solvePnP / calibration take 4, 5, 8, 12 or 14 distortion coefficients (k1 k2 p1 p2 [k3 [k4 k5 k6 [s1 s2 s3 s4 [tx ty]]]])
// and throw for any other count. A calibration that arrives in a request or a file can have any count (k1 and k2 alone is common). This brings
// one to a count OpenCV takes: the terms that are missing are zero, so it pads with zeros; terms beyond 14 do not exist and are dropped; a
// coefficient that is not a number counts as zero. An empty list stays empty (no distortion).
inline std::vector<double> NormalisedDistortionCoefficients(std::vector<double> coefficients)
{
	if (coefficients.empty()) return coefficients;
	for (double& coefficient : coefficients)
		if (!std::isfinite(coefficient)) coefficient = 0.0;
	static const size_t kAccepted[] = { 4, 5, 8, 12, 14 };
	size_t target = 14;
	for (size_t accepted : kAccepted) {
		if (coefficients.size() <= accepted) {
			target = accepted;
			break;
		}
	}
	coefficients.resize(target, 0.0);
	return coefficients;
}
