#pragma once
#include "IApriltagBackend.h"
#include <apriltag/tag36h11.h>

// Wraps apriltag_detector_detect() behind IApriltagBackend.
class CpuApriltagBackend : public IApriltagBackend {
public:
	// see ApriltagTuning for what each field's "use the default" value means here
	explicit CpuApriltagBackend(ApriltagTuning tuning = ApriltagTuning());
	~CpuApriltagBackend() override;

	zarray_t* Detect(const cv::Mat& grayFrame) override;
	void ReleaseResult(zarray_t* detections) override;
	std::string Name() const override { return "CPU (apriltag)"; }

	int GetThreads() const override { return m_Detector->nthreads; }
	float GetQuadDecimate() const override { return m_Detector->quad_decimate; }
	bool GetQuadDecimateSupported() const override { return true; }
	bool GetRefineEdges() const override { return m_Detector->refine_edges; }
	RefineEdgesMode GetRefineMode() const override { return REFINE_UPSTREAM; }
	bool GetRefineModeSupported() const override { return false; }

private:
	apriltag_detector_t* m_Detector;
	apriltag_family_t* m_Family;
};
