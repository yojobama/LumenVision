#include "CpuApriltagBackend.h"

CpuApriltagBackend::CpuApriltagBackend(ApriltagTuning tuning)
{
	m_Family = tag36h11_create();
	m_Detector = apriltag_detector_create();
	apriltag_detector_add_family(m_Detector, m_Family);

	// <= 0 for nthreads/quadDecimate uses the library default (1 thread, quad_decimate 2.0); refine_edges defaults on.
	// quad_decimate only lowers quad-search resolution (payload decoding stays full resolution); refine_edges offsets its coarser quads.
	if (tuning.nthreads > 0) m_Detector->nthreads = tuning.nthreads;
	if (tuning.quadDecimate > 0.0f) m_Detector->quad_decimate = tuning.quadDecimate;
	m_Detector->refine_edges = tuning.refineEdges;
}

CpuApriltagBackend::~CpuApriltagBackend()
{
	apriltag_detector_destroy(m_Detector);
	tag36h11_destroy(m_Family);
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
