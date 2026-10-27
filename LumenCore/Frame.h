#pragma once
#include <opencv2/opencv.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include "FrameFormat.h"

// A refcounted, format-tagged frame handle. Copying is a refcount bump, not a pixel copy (SourceResult holds one and is copied by value).
// AsGray()/AsBgr() are computed lazily and cached: free when the source format already matches, otherwise one cv::cvtColor.
//
// An NV12 frame is two plane views (Y, then interleaved UV at half size) over a buffer the producer owns, typically a hardware decoder's
// output; nothing is copied or converted until a consumer asks for BGR. AsGray() of an NV12 frame is its Y plane, free.
class Frame
{
public:
	Frame() = default;

	// Implicit so producers can build a result from a bare BGR cv::Mat. There is no implicit Frame -> cv::Mat
	// conversion; consumers call .AsGray()/.AsBgr() explicitly.
	Frame(const cv::Mat& mat);
	Frame(cv::Mat mat, FrameFormat format);
	// Same, plus the pool-owner handle from FramePool::Acquire, kept alive with this Frame (and its copies) so the
	// buffer isn't recycled while in use. nullptr for a normally-allocated Mat.
	Frame(cv::Mat mat, FrameFormat format, std::shared_ptr<void> poolOwner);

	// An NV12 frame: `y` is the luma plane (CV_8UC1, height x width) and `uv` the chroma plane (CV_8UC2, height/2 x width/2), either
	// possibly with a row stride larger than the width. `owner` keeps the memory both views point into alive for as long as the Frame is.
	// The values are full (JPEG) range: NV12 frames come from the hardware JPEG decoder, and AsBgr() and the encoders convert accordingly.
	static Frame FromNv12(cv::Mat y, cv::Mat uv, std::shared_ptr<void> owner);

	bool empty() const;
	// width x height of the picture (for NV12 the luma plane's size)
	cv::Size size() const;
	FrameFormat format() const;
	// the two planes of an NV12 frame; empty Mats for any other format
	bool IsNv12() const;
	const cv::Mat& YPlane() const;
	const cv::Mat& UvPlane() const;

	// A zero-copy view of a rectangular region with the same format (used by RoiSource to split side-by-side frames).
	// The view is a distinct Frame with its own AsGray()/AsBgr() cache. An NV12 region whose edges are not all even cannot be cut
	// from the chroma plane, so it comes back as a BGR frame instead.
	Frame Roi(const cv::Rect& roi) const;

	// Views: a refcount bump when the source format already matches, otherwise one cached, thread-safe conversion.
	const cv::Mat& AsGray() const;
	const cv::Mat& AsBgr() const;

	// The picture as BGR written straight into `dst` (any size/type is replaced by a CV_8UC3 of the picture's size, reusing its allocation
	// when it already fits). Unlike AsBgr() nothing is cached, so it is the way to get a private copy to draw on without converting twice.
	void CopyBgrTo(cv::Mat& dst) const;

	// Returns a new Frame wrapping the same cached BGR view as AsBgr(), carrying its FramePool ownership. Use this, not
	// AsBgr(), to publish a view as a new SourceResult/Frame; a bare cv::Mat would drop pool ownership (use-after-recycle).
	Frame AsBgrFrame() const;

private:
	struct Storage {
		cv::Mat mat; // the picture; for NV12 the luma plane
		cv::Mat uv;  // NV12 only: the interleaved chroma plane
		FrameFormat format = FrameFormat::BGR24;
		std::mutex viewMutex;
		std::optional<cv::Mat> grayView;
		std::optional<cv::Mat> bgrView;
		// non-null when `mat` or a lazy view is backed by a FramePool buffer (see the pool-taking constructor);
		// one owner per buffer, matched by field name
		std::shared_ptr<void> matPoolOwner;
		std::shared_ptr<void> grayPoolOwner;
		std::shared_ptr<void> bgrPoolOwner;
	};
	std::shared_ptr<Storage> m_Storage;
};
