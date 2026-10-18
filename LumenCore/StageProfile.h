#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

// Rolling window of a node's recent per-stage timings, read through Manager::GetNodeProfiles (GET /api/profile).
// "age" is the frame's age when the node published (the latency the UI shows); other names are the node's own stages.
class StageProfile
{
public:
	struct Stat {
		std::string name;
		uint64_t count = 0;      // samples since the profile was created or reset
		double meanUs = 0;       // over the window
		double p50Us = 0;
		double p95Us = 0;
		double maxUs = 0;
		double lastUs = 0;
	};

	// samples kept per stage: 8 s of a 30 fps node
	static constexpr size_t kWindow = 240;

	void Record(std::string_view stage, double us);
	std::vector<Stat> Snapshot() const;
	void Reset();

private:
	struct Series {
		std::string name;
		uint64_t count = 0;
		std::array<float, kWindow> samples{};
		size_t next = 0;
		size_t filled = 0;
	};
	mutable std::mutex m_Mutex;
	std::vector<Series> m_Series;
};

// Times a stage and records it when it ends. `profile` may be null (nothing recorded).
class ScopedStage
{
public:
	ScopedStage(StageProfile* profile, std::string_view stage)
		: m_Profile(profile), m_Stage(stage), m_Start(std::chrono::steady_clock::now()) {}
	~ScopedStage() { Stop(); }
	ScopedStage(const ScopedStage&) = delete;
	ScopedStage& operator=(const ScopedStage&) = delete;

	// ends the stage early, so the rest of the scope is not counted
	void Stop()
	{
		if (!m_Profile) return;
		m_Profile->Record(m_Stage, std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - m_Start).count());
		m_Profile = nullptr;
	}

private:
	StageProfile* m_Profile;
	std::string_view m_Stage;
	std::chrono::steady_clock::time_point m_Start;
};

// Records the time since the previous mark under a stage name: lap.Mark("a"); lap.Mark("b");
// Skip() restarts the clock without recording.
class StageLap
{
public:
	explicit StageLap(StageProfile* profile) : m_Profile(profile), m_Last(std::chrono::steady_clock::now()) {}
	void Mark(std::string_view stage)
	{
		auto now = std::chrono::steady_clock::now();
		if (m_Profile) m_Profile->Record(stage, std::chrono::duration<double, std::micro>(now - m_Last).count());
		m_Last = now;
	}
	void Skip() { m_Last = std::chrono::steady_clock::now(); }

private:
	StageProfile* m_Profile;
	std::chrono::steady_clock::time_point m_Last;
};

// One profile per node id. Both halves of a dual-role node (a detector is a sink of its camera and a source for what binds to it) share it,
// and it goes away with the last node holding it.
class NodeProfiles
{
public:
	static std::shared_ptr<StageProfile> For(const std::string& nodeId);
	// every live node's profile, as {"<id>": [{"stage":..., "count":..., "meanMs":..., "p50Ms":..., "p95Ms":..., "maxMs":..., "lastMs":...}]}
	static std::string AllAsJson();
	static std::string AsJson(const std::string& nodeId);
	// forgets every recorded sample, to measure from a clean start
	static void ResetAll();
};
