#include "Frame.h"
#include "FramePool.h"
#include <stdexcept>

Frame::Frame(const cv::Mat& mat)
	: Frame(mat, FrameFormat::BGR24)
{
}

Frame::Frame(cv::Mat mat, FrameFormat format)
	: m_Storage(std::make_shared<Storage>())
{
	m_Storage->mat = std::move(mat);
	m_Storage->format = format;
}

Frame::Frame(cv::Mat mat, FrameFormat format, std::shared_ptr<void> poolOwner)
	: m_Storage(std::make_shared<Storage>())
{
	m_Storage->mat = std::move(mat);
	m_Storage->format = format;
	m_Storage->matPoolOwner = std::move(poolOwner);
}

Frame Frame::FromNv12(cv::Mat y, cv::Mat uv, std::shared_ptr<void> owner)
{
	if (y.type() != CV_8UC1 || uv.type() != CV_8UC2 || y.rows % 2 != 0 || y.cols % 2 != 0 || uv.rows != y.rows / 2 || uv.cols != y.cols / 2)
		throw std::invalid_argument("Frame::FromNv12: needs a CV_8UC1 luma plane and a CV_8UC2 chroma plane of half its width and height");
	Frame frame(std::move(y), FrameFormat::NV12, std::move(owner));
	frame.m_Storage->uv = std::move(uv);
	return frame;
}

bool Frame::IsNv12() const
{
	return m_Storage && m_Storage->format == FrameFormat::NV12;
}

const cv::Mat& Frame::YPlane() const
{
	static const cv::Mat none;
	return IsNv12() ? m_Storage->mat : none;
}

const cv::Mat& Frame::UvPlane() const
{
	static const cv::Mat none;
	return IsNv12() ? m_Storage->uv : none;
}

bool Frame::empty() const
{
	return !m_Storage || m_Storage->mat.empty();
}

cv::Size Frame::size() const
{
	return m_Storage ? m_Storage->mat.size() : cv::Size();
}

FrameFormat Frame::format() const
{
	return m_Storage ? m_Storage->format : FrameFormat::BGR24;
}

Frame Frame::Roi(const cv::Rect& roi) const
{
	if (!m_Storage) throw std::runtime_error("Frame::Roi called on an empty (default-constructed) Frame");
	if (m_Storage->format == FrameFormat::NV12) {
		if (roi.x % 2 != 0 || roi.y % 2 != 0 || roi.width % 2 != 0 || roi.height % 2 != 0) {
			cv::Mat bgr;
			CopyBgrTo(bgr);
			return Frame(bgr(roi).clone(), FrameFormat::BGR24);
		}
		// both planes are cut at half the luma coordinates and keep the frame's buffer alive
		return FromNv12(m_Storage->mat(roi), m_Storage->uv(cv::Rect(roi.x / 2, roi.y / 2, roi.width / 2, roi.height / 2)), m_Storage->matPoolOwner);
	}
	// cv::Mat's ROI constructor is zero-copy: a new header sharing the same refcounted data.
	return Frame(m_Storage->mat(roi), m_Storage->format);
}

const cv::Mat& Frame::AsGray() const
{
	if (!m_Storage) throw std::runtime_error("Frame::AsGray called on an empty (default-constructed) Frame");

	std::lock_guard<std::mutex> guard(m_Storage->viewMutex);
	if (m_Storage->format == FrameFormat::GRAY8 || m_Storage->format == FrameFormat::NV12) {
		return m_Storage->mat; // for NV12 the luma plane is the grey picture
	}
	if (!m_Storage->grayView.has_value()) {
		cv::Mat gray;
		std::shared_ptr<void> owner;
		switch (m_Storage->format) {
		case FrameFormat::BGR24:
			// Acquire()d at the target shape before cv::cvtColor, which only skips its internal allocation
			// when `gray` already has the right rows/cols/type.
			gray = FramePool::Instance().Acquire(m_Storage->mat.rows, m_Storage->mat.cols, CV_8UC1, owner);
			cv::cvtColor(m_Storage->mat, gray, cv::COLOR_BGR2GRAY);
			break;
		case FrameFormat::RGB24:
			gray = FramePool::Instance().Acquire(m_Storage->mat.rows, m_Storage->mat.cols, CV_8UC1, owner);
			cv::cvtColor(m_Storage->mat, gray, cv::COLOR_RGB2GRAY);
			break;
		default:
			// MJPEG/YUYV have no decode path here; fail loudly rather than return garbage pixels.
			throw std::runtime_error("Frame::AsGray: no conversion implemented for this format");
		}
		m_Storage->grayView = gray;
		m_Storage->grayPoolOwner = std::move(owner);
	}
	return *m_Storage->grayView;
}

Frame Frame::AsBgrFrame() const
{
	if (!m_Storage) throw std::runtime_error("Frame::AsBgrFrame called on an empty (default-constructed) Frame");
	const cv::Mat& bgr = AsBgr(); // populates the cache first (a no-op if already BGR24 or cached)

	std::lock_guard<std::mutex> guard(m_Storage->viewMutex);
	std::shared_ptr<void> owner = (m_Storage->format == FrameFormat::BGR24) ? m_Storage->matPoolOwner : m_Storage->bgrPoolOwner;
	return Frame(bgr, FrameFormat::BGR24, owner);
}

const cv::Mat& Frame::AsBgr() const
{
	if (!m_Storage) throw std::runtime_error("Frame::AsBgr called on an empty (default-constructed) Frame");

	std::lock_guard<std::mutex> guard(m_Storage->viewMutex);
	if (m_Storage->format == FrameFormat::BGR24) {
		return m_Storage->mat;
	}
	if (!m_Storage->bgrView.has_value()) {
		cv::Mat bgr;
		std::shared_ptr<void> owner;
		switch (m_Storage->format) {
		case FrameFormat::RGB24:
			bgr = FramePool::Instance().Acquire(m_Storage->mat.rows, m_Storage->mat.cols, CV_8UC3, owner);
			cv::cvtColor(m_Storage->mat, bgr, cv::COLOR_RGB2BGR);
			break;
		case FrameFormat::GRAY8:
			bgr = FramePool::Instance().Acquire(m_Storage->mat.rows, m_Storage->mat.cols, CV_8UC3, owner);
			cv::cvtColor(m_Storage->mat, bgr, cv::COLOR_GRAY2BGR);
			break;
		case FrameFormat::NV12:
			bgr = FramePool::Instance().Acquire(m_Storage->mat.rows, m_Storage->mat.cols, CV_8UC3, owner);
			cv::cvtColorTwoPlane(m_Storage->mat, m_Storage->uv, bgr, cv::COLOR_YUV2BGR_NV12);
			break;
		default:
			throw std::runtime_error("Frame::AsBgr: no conversion implemented for this format");
		}
		m_Storage->bgrView = bgr;
		m_Storage->bgrPoolOwner = std::move(owner);
	}
	return *m_Storage->bgrView;
}

void Frame::CopyBgrTo(cv::Mat& dst) const
{
	if (!m_Storage) throw std::runtime_error("Frame::CopyBgrTo called on an empty (default-constructed) Frame");
	std::lock_guard<std::mutex> guard(m_Storage->viewMutex);
	const cv::Mat& mat = m_Storage->mat;
	switch (m_Storage->format) {
	case FrameFormat::BGR24:
		mat.copyTo(dst);
		break;
	case FrameFormat::RGB24:
		cv::cvtColor(mat, dst, cv::COLOR_RGB2BGR);
		break;
	case FrameFormat::GRAY8:
		cv::cvtColor(mat, dst, cv::COLOR_GRAY2BGR);
		break;
	case FrameFormat::NV12:
		// a cached conversion is reused rather than made twice
		if (m_Storage->bgrView.has_value()) m_Storage->bgrView->copyTo(dst);
		else cv::cvtColorTwoPlane(mat, m_Storage->uv, dst, cv::COLOR_YUV2BGR_NV12);
		break;
	default:
		throw std::runtime_error("Frame::CopyBgrTo: no conversion implemented for this format");
	}
}
