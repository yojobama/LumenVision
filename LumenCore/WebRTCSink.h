#pragma once
#ifdef LUMEN_WITH_WEBRTC

#include "ISink.h"
#include <rtc/rtc.hpp>
#include <opencv2/opencv.hpp>
#include <atomic>
#include <condition_variable>
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
// it over WebRTC. Signalling is exposed as plain string methods so libdatachannel types never reach swig.i.
//
// Uses non-trickle ICE: CreateOffer() blocks (with a timeout) until local candidate gathering completes, then returns one
// SDP with every candidate embedded; unsuitable for p2p links with asymmetric NAT that need trickle.
class WebRTCSink : public ISink {
public:
	WebRTCSink(std::shared_ptr<Logger> logger, std::string id, WebRTCSinkConfig config);
	~WebRTCSink() override;

	// returns the complete local SDP offer once ICE gathering finishes, or throws
	// std::runtime_error on timeout
	std::string CreateOffer();
	void SetAnswer(const std::string& sdp);
	void AddIceCandidate(const std::string& candidate, const std::string& mid);
	bool IsConnected() const;
	std::string GetConnectionStatus() const;

private:
	void Process(const std::vector<SourceResult>& results) override;
	void InitializePeerConnection(); // caller must already hold m_ConnectionMutex

	bool EnsureEncoderInitialized(int width, int height);
	void EncodeAndSend(const cv::Mat& bgrFrame);
	void ShutdownEncoder();

	std::shared_ptr<Logger> m_Logger;
	WebRTCSinkConfig m_Config;

	// Guards m_PeerConnection/m_Track/m_SrReporter reassignment: CreateOffer() replaces all three per negotiation,
	// racing with Process()'s thread reading m_Track via EncodeAndSend().
	mutable std::mutex m_ConnectionMutex;
	std::shared_ptr<rtc::PeerConnection> m_PeerConnection;
	std::shared_ptr<rtc::Track> m_Track;
	std::shared_ptr<rtc::RtcpSrReporter> m_SrReporter;

	mutable std::mutex m_GatheringMutex;
	std::condition_variable m_GatheringCv;
	bool m_GatheringComplete = false;

	AVCodecContext* m_CodecContext = nullptr;
	SwsContext* m_SwsContext = nullptr;
	AVFrame* m_YuvFrame = nullptr;
	int m_EncoderWidth = 0;
	int m_EncoderHeight = 0;
	int64_t m_FrameCounter = 0;

#ifdef LUMEN_WITH_RGA
	RgaColorConverter m_RgaConverter;
	// warn only once per sink lifetime, not per frame, so a persistently failing RGA path does not flood the log
	bool m_RgaConversionFailureLogged = false;
#endif
};

#endif // LUMEN_WITH_WEBRTC
