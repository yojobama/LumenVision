#pragma once
#ifdef LUMEN_WITH_NT4

#include "ISink.h"
#include <networktables/NetworkTableInstance.h>
#include <networktables/BooleanTopic.h>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class SystemMonitor;

struct NetworkTablesConfig {
	// exactly one of these should be set; teamNumber takes precedence if both are
	std::optional<unsigned int> teamNumber;
	std::string serverAddress; // used when teamNumber is not set
	unsigned int port = 0;     // 0 = NT4 default port
	std::string clientIdentity = "lumenvision";
	std::string rootTable = "lumenvision";
};

// One bound source's rolling stream stats, tracked across Process() calls to derive fps.
struct NetworkTablesStreamStats {
	uint64_t lastFrameNumber = 0;
	uint64_t lastCaptureTimeUs = 0;
	double fps = 0.0;
};

// Terminal sink (not an ISource): publishes every bound source's latest JSON result to an NT4 server,
// one subtable per source id.
class NetworkTablesSink : public ISink
{
public:
	// Non-owning pointer to Manager's shared SystemMonitor; must outlive this sink.
	// May be null, in which case ".status" omits the cpu/temperature/ram fields.
	NetworkTablesSink(std::shared_ptr<Logger> logger, std::string id, NetworkTablesConfig config,
		SystemMonitor* systemMonitor = nullptr);
	~NetworkTablesSink();

	bool IsConnected() const;
	// small JSON status blob (connected, server, identity, latency)
	std::string GetConnectionStatus() const;

	// Drains "<sourceId>/config/pipelineIndex" and ".../driverMode" writes since the last call as a JSON
	// array of {"sourceId", "pipelineIndex"?, "driverMode"?}; each request is returned once.
	std::string PollConfigRequests();

	// Drains the coprocessor-wide "<rootTable>/config/recording" boolean: -1 if unwritten since the last
	// call, else 0/1. Desired state, not a toggle.
	int PollRecordingRequest();

	// Publishes "<rootTable>/status/recording": whether the coprocessor is actually recording.
	void SetRecordingStatus(bool recording);

private:
	// nt::Event callback for every topic under this sink's root table; parses the source id and
	// config/pipelineIndex|driverMode writes out of the topic name.
	void OnConfigValueChanged(const nt::Event& event);
	void Process(const std::vector<SourceResult>& results) override;

	// Writes one source's JSON to its NT subtable: legacy parallel arrays, flattened best-target scalars and
	// the binary "result" packet (see BuildResultPacket). Non-AprilTag JSON falls back to a "raw" string topic.
	void PublishSourceResult(const SourceResult& result);

	nt::NetworkTableInstance m_Instance;
	NetworkTablesConfig m_Config;
	std::shared_ptr<Logger> m_Logger;
	SystemMonitor* m_SystemMonitor;
	uint64_t m_Heartbeat = 0;
	uint64_t m_ConstructedAtUs = 0;
	std::unordered_map<std::string, NetworkTablesStreamStats> m_StreamStats;

	NT_Listener m_ConfigListener = 0;
	std::mutex m_ConfigMutex;
	struct PendingConfigRequest {
		std::optional<int> pipelineIndex;
		std::optional<bool> driverMode;
	};
	// guarded by m_ConfigMutex; OnConfigValueChanged runs on ntcore's listener thread
	std::unordered_map<std::string, PendingConfigRequest> m_PendingConfig;
	std::optional<bool> m_PendingRecording; // guarded by m_ConfigMutex too

	nt::BooleanPublisher m_RecordingStatusPublisher;
};

#endif // LUMEN_WITH_NT4
