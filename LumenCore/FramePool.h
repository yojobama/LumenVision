#pragma once
#include <opencv2/opencv.hpp>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

// Recycles full-frame-sized pixel buffers instead of allocating one per capture/detection cycle. Built on shared_ptr
// refcounting; only full-frame image buffers opt in.
//
// Thread-safety: Acquire() may be called concurrently from any thread. A buffer returns to the pool exactly once, when the
// last reference is released, so a Frame shared across several sink threads is safe.
class FramePool
{
public:
	static FramePool& Instance();

	// Returns a cv::Mat of exactly this shape: recycled from the free list when a buffer of the same rows/cols/type is
	// available, otherwise freshly allocated. `owner` is the buffer's lifetime handle: keep it alive while the Mat (or Mats
	// copied from it) is in use. The Mat wraps `owner`'s buffer without owning it; .clone() gives a self-contained copy.
	cv::Mat Acquire(int rows, int cols, int type, std::shared_ptr<void>& owner);

	// Buffers start on a kBlockAlignment boundary and are sized to a multiple of it, so a GPU can import them in place.
	// They are never freed while the process runs, so an import stays valid.
	static constexpr size_t kBlockAlignment = 4096;

	// The size to import for a pooled buffer starting at `data`: its capacity (the row data rounded up to kBlockAlignment).
	// 0 when `data` is not the start of a buffer from this pool.
	size_t BlockCapacity(const void* data);

private:
	// an aligned allocation, released with the pool
	struct Block {
		explicit Block(size_t capacity);
		~Block();
		Block(const Block&) = delete;
		Block& operator=(const Block&) = delete;
		uint8_t* data = nullptr;
		size_t capacity = 0;
	};

	struct Key {
		int rows, cols, type;
		bool operator==(const Key& other) const {
			return rows == other.rows && cols == other.cols && type == other.type;
		}
	};
	struct KeyHash {
		size_t operator()(const Key& k) const {
			return (static_cast<size_t>(k.rows) * 73856093u) ^
			       (static_cast<size_t>(k.cols) * 19349663u) ^
			       (static_cast<size_t>(k.type) * 83492791u);
		}
	};

	std::mutex m_Mutex;
	std::unordered_map<Key, std::vector<std::shared_ptr<Block>>, KeyHash> m_Free;
	std::unordered_map<const void*, size_t> m_Capacities; // every block ever allocated, by start address
};
