#pragma once
#include <opencv2/opencv.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include "FrameFormat.h"

// A refcounted, format-tagged frame handle. Copying is a refcount bump, not a pixel copy (SourceResult holds one and is copied by value).
// AsGray()/AsBgr() are computed lazily and cached: free when the source format already matches, otherwise one cv::cvtColor.
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

	bool empty() const;
	cv::Size size() const;
	FrameFormat format() const;

	// A zero-copy view of a rectangular region with the same format (used by RoiSource to split side-by-side frames).
	// The view is a distinct Frame with its own AsGray()/AsBgr() cache.
	Frame Roi(const cv::Rect& roi) const;

	// Views: a refcount bump when the source format already matches, otherwise one cached, thread-safe conversion.
	const cv::Mat& AsGray() const;
	const cv::Mat& AsBgr() const;

	// Returns a new Frame wrapping the same cached BGR view as AsBgr(), carrying its FramePool ownership. Use this, not
	// AsBgr(), to publish a view as a new SourceResult/Frame; a bare cv::Mat would drop pool ownership (use-after-recycle).
	Frame AsBgrFrame() const;

private:
	struct Storage {
		cv::Mat mat;
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
