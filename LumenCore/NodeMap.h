#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

// An id -> node map that several threads may use at once (the server's REST handlers, its state broadcast and the NetworkTables control
// tick all call into Manager together). It has the small part of std::map's interface Manager uses, so find() can still be compared with
// end(), but find() returns a copy of the entry: the node it names stays alive for as long as the result does, even if another thread
// erases it from the map in the meantime. Nodes are destroyed outside the lock, because destroying one joins its threads.
template <typename T>
class NodeMap {
public:
	struct Entry {
		int first = 0;
		std::shared_ptr<T> second;
	};
	struct EndTag {};

	class Found {
	public:
		Found() = default;
		explicit Found(Entry entry) : m_Entry(std::move(entry)), m_IsFound(true) {}
		const Entry* operator->() const { return &m_Entry; }
		const Entry& operator*() const { return m_Entry; }
		friend bool operator==(const Found& found, EndTag) { return !found.m_IsFound; }
		friend bool operator!=(const Found& found, EndTag) { return found.m_IsFound; }
		friend bool operator==(EndTag, const Found& found) { return !found.m_IsFound; }
		friend bool operator!=(EndTag, const Found& found) { return found.m_IsFound; }

	private:
		Entry m_Entry;
		bool m_IsFound = false;
	};

	EndTag end() const { return EndTag{}; }

	Found find(int id) const
	{
		std::shared_lock<std::shared_mutex> lock(m_Mutex);
		auto it = m_Map.find(id);
		if (it == m_Map.end()) return Found();
		return Found(Entry{ it->first, it->second });
	}

	// std::map::emplace semantics: an id that is already there keeps its node
	bool emplace(int id, std::shared_ptr<T> node)
	{
		std::unique_lock<std::shared_mutex> lock(m_Mutex);
		return m_Map.emplace(id, std::move(node)).second;
	}

	bool erase(int id)
	{
		std::shared_ptr<T> doomed;
		{
			std::unique_lock<std::shared_mutex> lock(m_Mutex);
			auto it = m_Map.find(id);
			if (it == m_Map.end()) return false;
			doomed = std::move(it->second);
			m_Map.erase(it);
		}
		return true; // `doomed` drops here, outside the lock
	}

	// every entry at this moment, in id order; safe to walk while other threads add or remove nodes
	std::vector<Entry> snapshot() const
	{
		std::shared_lock<std::shared_mutex> lock(m_Mutex);
		std::vector<Entry> entries;
		entries.reserve(m_Map.size());
		for (const auto& [id, node] : m_Map) entries.push_back(Entry{ id, node });
		return entries;
	}

	bool empty() const
	{
		std::shared_lock<std::shared_mutex> lock(m_Mutex);
		return m_Map.empty();
	}

	void clear()
	{
		std::map<int, std::shared_ptr<T>> doomed;
		{
			std::unique_lock<std::shared_mutex> lock(m_Mutex);
			doomed.swap(m_Map);
		}
	}

private:
	mutable std::shared_mutex m_Mutex;
	std::map<int, std::shared_ptr<T>> m_Map;
};
