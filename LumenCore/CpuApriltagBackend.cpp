#include "CpuApriltagBackend.h"
#include <algorithm>

CpuApriltagBackend::CpuApriltagBackend(ApriltagTuning tuning)
{
	m_FamilyKind = tuning.family;
	m_MaxHamming = ClampMaxHamming(tuning.maxHamming);
	m_Family = CreateApriltagFamily(m_FamilyKind);
	m_Detector = apriltag_detector_create();
	apriltag_detector_add_family_bits(m_Detector, m_Family, m_MaxHamming);
	m_Detector->quad_sigma = tuning.quadSigma;

	// <= 0 for nthreads/quadDecimate uses the library default (1 thread, quad_decimate 2.0); refine_edges defaults on.
	// quad_decimate only lowers quad-search resolution (payload decoding stays full resolution); refine_edges offsets its coarser quads.
	if (tuning.nthreads > 0) m_Detector->nthreads = tuning.nthreads;
	if (tuning.quadDecimate > 0.0f) m_Detector->quad_decimate = tuning.quadDecimate;
	m_Detector->refine_edges = tuning.refineEdges;
}

CpuApriltagBackend::~CpuApriltagBackend()
{
	apriltag_detector_destroy(m_Detector);
	DestroyApriltagFamily(m_FamilyKind, m_Family);
}

zarray_t* CpuApriltagBackend::Detect(const cv::Mat& grayFrame)
{
	// libapriltag's quad search reads outside a picture a few pixels across (a thin or tiny ROI crop in front of an AprilTag node killed the service
	// with a segmentation fault), and no tag fits in one: such a frame has no detections
	constexpr float kMinDecimatedSide = 8.0f;
	const float decimation = std::max(1.0f, m_Detector->quad_decimate);
	if (static_cast<float>(std::min(grayFrame.cols, grayFrame.rows)) / decimation < kMinDecimatedSide) {
		return zarray_create(sizeof(apriltag_detection_t*));
	}
	// the row stride, not the width: a crop of a larger frame (half of a side-by-side camera) is a view with its parent's stride
	image_u8_t img = {
		grayFrame.cols,
		grayFrame.rows,
		static_cast<int32_t>(grayFrame.step[0]),
		grayFrame.data
	};
	return apriltag_detector_detect(m_Detector, &img);
}

void CpuApriltagBackend::ReleaseResult(zarray_t* detections)
{
	apriltag_detections_destroy(detections);
}
