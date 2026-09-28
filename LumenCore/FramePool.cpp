#include "FramePool.h"

FramePool& FramePool::Instance()
{
	static FramePool instance;
	return instance;
}

cv::Mat FramePool::Acquire(int rows, int cols, int type, std::shared_ptr<void>& owner)
{
	Key key{ rows, cols, type };
	size_t bytesNeeded = static_cast<size_t>(rows) * static_cast<size_t>(cols) * CV_ELEM_SIZE(type);

	std::shared_ptr<std::vector<uint8_t>> buffer;
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Free.find(key);
		if (it != m_Free.end() && !it->second.empty()) {
			buffer = std::move(it->second.back());
			it->second.pop_back();
		}
	}

	if (!buffer) {
		buffer = std::make_shared<std::vector<uint8_t>>(bytesNeeded);
	}

	// Aliasing-style: `owner`'s deleter captures `buffer`, keeping it alive until the last copy of `owner`
	// is destroyed, then pushes it back onto the free list.
	uint8_t* rawData = buffer->data();
	FramePool* self = this;
	owner = std::shared_ptr<void>(rawData, [self, buffer, key](void*) mutable {
		std::lock_guard<std::mutex> lock(self->m_Mutex);
		self->m_Free[key].push_back(std::move(buffer));
	});

	return cv::Mat(rows, cols, type, rawData);
}
