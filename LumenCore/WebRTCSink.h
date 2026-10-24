#pragma once
#ifdef LUMEN_WITH_WEBRTC

#include "ISink.h"
#include <rtc/rtc.hpp>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#ifdef LUMEN_WITH_RGA
#include "RgaColorConverter.h"
#endif

struct WebRTCSinkConfig {
	int bitrateKbps = 4000;
	int fps = 30;
	// "libx264" by default; the Orange Pi build can pass "h264_rkmpp". A plain string so Manager needs no encoder-name enum.
	std::string encoderName = "libx264";
};

// Terminal sink: binds to any single frame-producing node (raw camera or a detector's annotated output) and encodes+streams
// it over WebRTC to any number of viewers. Each viewer is a peer with its own PeerConnection, named by the viewer in its signalling
// calls (an empty name is one shared "default" peer for clients that do not name themselves); one encoder feeds them all.
// Signalling is exposed as plain string methods so libdatachannel types never reach swig.i.
//
// Uses non-trickle ICE: CreateOffer() blocks (with a timeout) until local candidate gathering completes, then returns one
// SDP with every candidate embedded; unsuitable for p2p links with asymmetric NAT that need trickle.
class WebRTCSink : public ISink {
public:
	WebRTCSink(std::shared_ptr<Logger> logger, std::string id, WebRTCSinkConfig config);
	~WebRTCSink() override;

	// returns the complete local SDP offer for the named viewer once ICE gathering finishes, or throws
	// std::runtime_error on timeout; a repeat offer for the same name replaces that viewer's connection only
	std::string CreateOffer(const std::string& peerId = "");
	void SetAnswer(const std::string& sdp, const std::string& peerId = "");
	void AddIceCandidate(const std::string& candidate, const std::string& mid, const std::string& peerId = "");
	// whether any viewer is connected
	bool IsConnected() const;
	// true while a viewer's track is open: with nobody watching, bound sources decode in grey and detectors skip their overlay
	bool IsConsumingFrames() const override;
	// {"connected": any viewer connected, "peers": viewer count, "iceState", "gatheringComplete"} (the last two of the newest viewer)
	std::string GetConnectionStatus() const;

	// Changes the target bitrate, frame rate and size divisor (N shrinks frames to 1/N width and height; 1 = full size). The encoder is
	// rebuilt with them before the next frame, so the stream restarts on a keyframe.
	void SetSettings(int bitrateKbps, int fps, int scaleDivisor);

private:
	void Process(const std::vector<SourceResult>& results) override;

	// One viewer: its own connection, video track and RTCP sender reports.
	struct Peer {
		std::shared_ptr<rtc::PeerConnection> connection;
		std::shared_ptr<rtc::Track> track;
		std::shared_ptr<rtc::RtcpSrReporter> srReporter;
		bool gatheringComplete = false; // guarded by m_GatheringMutex
		bool streaming = false;         // the encoder has been given a keyframe for this viewer; touched by the encoding thread only
		uint64_t order = 0;             // creation order, to find the newest
	};
	std::shared_ptr<Peer> CreatePeer(); // caller must already hold m_ConnectionMutex
	std::shared_ptr<Peer> FindPeer(const std::string& peerId) const;
	void PruneClosedPeers(); // caller must already hold m_ConnectionMutex

	bool EnsureEncoderInitialized(int width, int height);
	void EncodeAndSend(const cv::Mat& bgrFrame, uint64_t captureTimeUs);
	void ShutdownEncoder();

	std::shared_ptr<Logger> m_Logger;
	WebRTCSinkConfig m_Config;
	// settings requested by SetSettings(), picked up by the encoding thread
	std::mutex m_SettingsMutex;
	WebRTCSinkConfig m_PendingConfig;
	int m_PendingScaleDivisor = 1;
	bool m_SettingsChanged = false;
	int m_ScaleDivisor = 1;

	// Set when a viewer joins or asks for a new picture (PLI/FIR): the next frame is encoded as an IDR frame, which the viewer can start from.
	// Shared with the viewers' RTCP handlers, which may run after Process() has moved on.
	std::shared_ptr<std::atomic<bool>> m_KeyframeRequested = std::make_shared<std::atomic<bool>>(true);

	// Guards m_Peers: signalling threads add and replace viewers while Process()'s thread reads their tracks in EncodeAndSend().
	mutable std::mutex m_ConnectionMutex;
	std::map<std::string, std::shared_ptr<Peer>> m_Peers;
	uint64_t m_NextPeerOrder = 1;

	// Guards every Peer::gatheringComplete; one condition variable serves all viewers' CreateOffer() waits.
	mutable std::mutex m_GatheringMutex;
	std::condition_variable m_GatheringCv;

	AVCodecContext* m_CodecContext = nullptr;
	SwsContext* m_SwsContext = nullptr;
	AVFrame* m_YuvFrame = nullptr;
	int m_EncoderWidth = 0;
	int m_EncoderHeight = 0;
	int64_t m_FrameCounter = 0;
	// RTP timestamps follow the camera's capture times (relative to the first frame sent), so they stay monotonic when the encoder is
	// rebuilt and track the real frame spacing instead of the nominal frame rate
	uint64_t m_FirstCaptureUs = 0;
	double m_LastSentSeconds = 0.0;

#ifdef LUMEN_WITH_RGA
	RgaColorConverter m_RgaConverter;
	// warn only once per sink lifetime, not per frame, so a persistently failing RGA path does not flood the log
	bool m_RgaConversionFailureLogged = false;
#endif
};

#endif // LUMEN_WITH_WEBRTC
