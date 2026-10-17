#include "FramePool.h"
#include <algorithm>
#include <cstdlib>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif

FramePool::Block::Block(size_t requested)
{
	capacity = (std::max<size_t>(requested, 1) + kBlockAlignment - 1) / kBlockAlignment * kBlockAlignment;
#ifdef _WIN32
	data = static_cast<uint8_t*>(_aligned_malloc(capacity, kBlockAlignment));
#else
	data = static_cast<uint8_t*>(std::aligned_alloc(kBlockAlignment, capacity));
#endif
	if (!data) throw std::bad_alloc();
}

FramePool::Block::~Block()
{
#ifdef _WIN32
	_aligned_free(data);
#else
	std::free(data);
#endif
}

FramePool& FramePool::Instance()
{
	static FramePool instance;
	return instance;
}

cv::Mat FramePool::Acquire(int rows, int cols, int type, std::shared_ptr<void>& owner)
{
	Key key{ rows, cols, type };
	size_t bytesNeeded = static_cast<size_t>(rows) * static_cast<size_t>(cols) * CV_ELEM_SIZE(type);

	std::shared_ptr<Block> buffer;
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto it = m_Free.find(key);
		if (it != m_Free.end() && !it->second.empty()) {
			buffer = std::move(it->second.back());
			it->second.pop_back();
		}
	}

	if (!buffer) {
		buffer = std::make_shared<Block>(bytesNeeded);
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Capacities[buffer->data] = buffer->capacity;
	}

	// Aliasing-style: `owner`'s deleter captures `buffer`, keeping it alive until the last copy of `owner`
	// is destroyed, then pushes it back onto the free list.
	uint8_t* rawData = buffer->data;
	FramePool* self = this;
	owner = std::shared_ptr<void>(rawData, [self, buffer, key](void*) mutable {
		std::lock_guard<std::mutex> lock(self->m_Mutex);
		self->m_Free[key].push_back(std::move(buffer));
	});

	return cv::Mat(rows, cols, type, rawData);
}

size_t FramePool::BlockCapacity(const void* data)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	auto it = m_Capacities.find(data);
	return it == m_Capacities.end() ? 0 : it->second;
}
