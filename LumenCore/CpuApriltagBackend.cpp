#include "CpuApriltagBackend.h"

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
	image_u8_t img = {
		grayFrame.cols,
		grayFrame.rows,
		grayFrame.cols,
		grayFrame.data
	};
	return apriltag_detector_detect(m_Detector, &img);
}

void CpuApriltagBackend::ReleaseResult(zarray_t* detections)
{
	apriltag_detections_destroy(detections);
}
