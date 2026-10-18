#include "StageProfile.h"
#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>

void StageProfile::Record(std::string_view stage, double us)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	Series* series = nullptr;
	for (Series& candidate : m_Series) {
		if (candidate.name == stage) { series = &candidate; break; }
	}
	if (!series) {
		m_Series.emplace_back();
		series = &m_Series.back();
		series->name = std::string(stage);
	}
	series->samples[series->next] = static_cast<float>(us);
	series->next = (series->next + 1) % kWindow;
	series->filled = std::min(series->filled + 1, kWindow);
	series->count++;
}

std::vector<StageProfile::Stat> StageProfile::Snapshot() const
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	std::vector<Stat> stats;
	stats.reserve(m_Series.size());
	for (const Series& series : m_Series) {
		if (series.filled == 0) continue;
		std::vector<float> window(series.samples.begin(), series.samples.begin() + series.filled);
		double sum = 0;
		for (float v : window) sum += v;
		std::sort(window.begin(), window.end());
		auto percentile = [&](double p) { return static_cast<double>(window[std::min(window.size() - 1, static_cast<size_t>(p * window.size()))]); };

		Stat stat;
		stat.name = series.name;
		stat.count = series.count;
		stat.meanUs = sum / window.size();
		stat.p50Us = percentile(0.50);
		stat.p95Us = percentile(0.95);
		stat.maxUs = window.back();
		stat.lastUs = series.samples[(series.next + kWindow - 1) % kWindow];
		stats.push_back(std::move(stat));
	}
	return stats;
}

void StageProfile::Reset()
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Series.clear();
}

namespace {
std::mutex g_RegistryMutex;
std::map<std::string, std::weak_ptr<StageProfile>> g_Registry;

nlohmann::json StatsToJson(const std::vector<StageProfile::Stat>& stats)
{
	nlohmann::json array = nlohmann::json::array();
	for (const StageProfile::Stat& s : stats) {
		array.push_back({
			{"stage", s.name}, {"count", s.count},
			{"meanMs", s.meanUs / 1000.0}, {"p50Ms", s.p50Us / 1000.0}, {"p95Ms", s.p95Us / 1000.0},
			{"maxMs", s.maxUs / 1000.0}, {"lastMs", s.lastUs / 1000.0},
		});
	}
	return array;
}
}

std::shared_ptr<StageProfile> NodeProfiles::For(const std::string& nodeId)
{
	std::lock_guard<std::mutex> lock(g_RegistryMutex);
	// forget profiles whose nodes are gone, so ids that come and go do not accumulate
	for (auto it = g_Registry.begin(); it != g_Registry.end();) {
		it = it->second.expired() ? g_Registry.erase(it) : std::next(it);
	}
	std::weak_ptr<StageProfile>& slot = g_Registry[nodeId];
	std::shared_ptr<StageProfile> profile = slot.lock();
	if (!profile) {
		profile = std::make_shared<StageProfile>();
		slot = profile;
	}
	return profile;
}

std::string NodeProfiles::AllAsJson()
{
	std::map<std::string, std::shared_ptr<StageProfile>> live;
	{
		std::lock_guard<std::mutex> lock(g_RegistryMutex);
		for (auto& entry : g_Registry) {
			if (auto profile = entry.second.lock()) live[entry.first] = profile;
		}
	}
	nlohmann::json all = nlohmann::json::object();
	for (auto& entry : live) all[entry.first] = StatsToJson(entry.second->Snapshot());
	return all.dump();
}

std::string NodeProfiles::AsJson(const std::string& nodeId)
{
	std::shared_ptr<StageProfile> profile;
	{
		std::lock_guard<std::mutex> lock(g_RegistryMutex);
		auto it = g_Registry.find(nodeId);
		if (it != g_Registry.end()) profile = it->second.lock();
	}
	return StatsToJson(profile ? profile->Snapshot() : std::vector<StageProfile::Stat>{}).dump();
}

void NodeProfiles::ResetAll()
{
	std::vector<std::shared_ptr<StageProfile>> live;
	{
		std::lock_guard<std::mutex> lock(g_RegistryMutex);
		for (auto& entry : g_Registry) {
			if (auto profile = entry.second.lock()) live.push_back(profile);
		}
	}
	for (auto& profile : live) profile->Reset();
}
