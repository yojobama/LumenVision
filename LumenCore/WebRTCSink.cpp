#ifdef LUMEN_WITH_WEBRTC
#include "WebRTCSink.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>

using namespace rtc;

namespace {
	constexpr uint32_t kSsrcValue = 42;
	constexpr uint8_t H264_PAYLOAD_TYPE = 96;

	// FRC field networks only pass ports 5800-5810 for team use; media uses 5801-5809 (5800 is the web server, 5810 the
	// roboRIO NT4 server). The ICE UDP mux (libjuice) binds one socket to the first free port and reuses it for every later connection.
	constexpr uint16_t kIcePortRangeBegin = 5801;
	constexpr uint16_t kIcePortRangeEnd = 5809;
}

WebRTCSink::WebRTCSink(std::shared_ptr<Logger> logger, std::string id, WebRTCSinkConfig config)
	: ISink(logger, 1 /* maxSources */, false /* requireJson */, true /* requireFrame */, id)
	, m_Logger(logger)
	, m_Config(config)
	, m_PendingConfig(config)
{
	if (m_Logger) m_Logger->EnterLog("WebRTCSink constructed, encoder=" + config.encoderName);
}

WebRTCSink::~WebRTCSink()
{
	ShutdownEncoder();
	std::lock_guard<std::mutex> lock(m_ConnectionMutex);
	for (auto& entry : m_Peers) {
		if (entry.second->connection) entry.second->connection->close();
	}
	m_Peers.clear();
}

// Builds a fresh PeerConnection/Track/SrReporter for one viewer; caller must hold m_ConnectionMutex.
std::shared_ptr<WebRTCSink::Peer> WebRTCSink::CreatePeer()
{
	auto peer = std::make_shared<Peer>();
	peer->order = m_NextPeerOrder++;

	Configuration rtcConfig;
	rtcConfig.portRangeBegin = kIcePortRangeBegin;
	rtcConfig.portRangeEnd = kIcePortRangeEnd;
	rtcConfig.enableIceUdpMux = true;
	peer->connection = std::make_shared<PeerConnection>(rtcConfig);
	std::weak_ptr<Peer> weakPeer = peer;
	peer->connection->onGatheringStateChange([this, weakPeer](PeerConnection::GatheringState state) {
		if (state != PeerConnection::GatheringState::Complete) return;
		if (auto locked = weakPeer.lock()) {
			std::lock_guard<std::mutex> lock(m_GatheringMutex);
			locked->gatheringComplete = true;
			m_GatheringCv.notify_all();
		}
	});

	Description::Video video("video", Description::Direction::SendOnly);
	video.addH264Codec(H264_PAYLOAD_TYPE);
	video.addSSRC(kSsrcValue, "lumen-video");
	peer->track = peer->connection->addTrack(video);

	auto rtpConfig = std::make_shared<RtpPacketizationConfig>(kSsrcValue, "lumen-video", H264_PAYLOAD_TYPE, H264RtpPacketizer::ClockRate);
	auto packetizer = std::make_shared<H264RtpPacketizer>(NalUnit::Separator::StartSequence, rtpConfig);
	peer->srReporter = std::make_shared<RtcpSrReporter>(rtpConfig);
	packetizer->addToChain(peer->srReporter);
	packetizer->addToChain(std::make_shared<RtcpNackResponder>());
	peer->track->setMediaHandler(packetizer);
	return peer;
}

// Drops viewers whose connection has ended, so closed browser tabs do not pile up; caller must hold m_ConnectionMutex.
void WebRTCSink::PruneClosedPeers()
{
	for (auto it = m_Peers.begin(); it != m_Peers.end();) {
		const auto state = it->second->connection->state();
		if (state == PeerConnection::State::Closed || state == PeerConnection::State::Failed) {
			it->second->connection->close();
			it = m_Peers.erase(it);
		} else {
			++it;
		}
	}
}

std::shared_ptr<WebRTCSink::Peer> WebRTCSink::FindPeer(const std::string& peerId) const
{
	std::lock_guard<std::mutex> lock(m_ConnectionMutex);
	auto it = m_Peers.find(peerId.empty() ? std::string("default") : peerId);
	return it == m_Peers.end() ? nullptr : it->second;
}

std::string WebRTCSink::CreateOffer(const std::string& peerId)
{
	const std::string key = peerId.empty() ? std::string("default") : peerId;
	std::shared_ptr<Peer> peer;
	{
		std::lock_guard<std::mutex> lock(m_ConnectionMutex);
		PruneClosedPeers();
		auto previous = m_Peers.find(key);
		if (previous != m_Peers.end()) previous->second->connection->close();
		peer = CreatePeer();
		m_Peers[key] = peer;
	}

	peer->connection->setLocalDescription();

	{
		std::unique_lock<std::mutex> lock(m_GatheringMutex);
		if (!m_GatheringCv.wait_for(lock, std::chrono::seconds(10), [&peer] { return peer->gatheringComplete; })) {
			throw std::runtime_error("WebRTCSink::CreateOffer: ICE gathering did not complete within 10s");
		}
	}

	auto description = peer->connection->localDescription();
	if (!description.has_value()) {
		throw std::runtime_error("WebRTCSink::CreateOffer: no local description after gathering completed");
	}
	return std::string(description.value());
}

void WebRTCSink::SetAnswer(const std::string& sdp, const std::string& peerId)
{
	std::shared_ptr<Peer> peer = FindPeer(peerId);
	if (!peer) {
		if (m_Logger) m_Logger->EnterLog(::LogLevel::Warning, "WebRTCSink::SetAnswer: no viewer '" + peerId + "' (no offer was made for it)");
		return;
	}
	try {
		peer->connection->setRemoteDescription(Description(sdp, Description::Type::Answer));
	} catch (const std::exception& e) {
		// Swallowed, not rethrown: an exception crossing the SWIG/P-Invoke boundary without an %exception typemap can
		// terminate the server, and a bad answer just fails this attempt (the browser notices via connectionstatechange/timeout).
		if (m_Logger) m_Logger->EnterLog(::LogLevel::Error, std::string("WebRTCSink::SetAnswer failed: ") + e.what());
	}
}

void WebRTCSink::AddIceCandidate(const std::string& candidate, const std::string& mid, const std::string& peerId)
{
	std::shared_ptr<Peer> peer = FindPeer(peerId);
	if (!peer) return;
	try {
		peer->connection->addRemoteCandidate(Candidate(candidate, mid));
	} catch (const std::exception& e) {
		// libdatachannel throws std::logic_error if a candidate arrives before the remote description is set (a real race);
		// a dropped candidate is harmless, so it is swallowed.
		if (m_Logger) m_Logger->EnterLog(::LogLevel::Warning, std::string("WebRTCSink::AddIceCandidate dropped a candidate: ") + e.what());
	}
}

void WebRTCSink::SetSettings(int bitrateKbps, int fps, int scaleDivisor)
{
	std::lock_guard<std::mutex> lock(m_SettingsMutex);
	m_PendingConfig.bitrateKbps = std::clamp(bitrateKbps, 100, 50000);
	m_PendingConfig.fps = std::clamp(fps, 1, 120);
	m_PendingScaleDivisor = std::clamp(scaleDivisor, 1, 16);
	m_SettingsChanged = true;
}

bool WebRTCSink::IsConnected() const
{
	std::lock_guard<std::mutex> lock(m_ConnectionMutex);
	for (const auto& entry : m_Peers) {
		if (entry.second->connection->state() == PeerConnection::State::Connected) return true;
	}
	return false;
}

std::string WebRTCSink::GetConnectionStatus() const
{
	bool anyConnected = false;
	size_t peerCount = 0;
	std::shared_ptr<Peer> newest;
	{
		std::lock_guard<std::mutex> lock(m_ConnectionMutex);
		peerCount = m_Peers.size();
		for (const auto& entry : m_Peers) {
			if (entry.second->connection->state() == PeerConnection::State::Connected) anyConnected = true;
			if (!newest || entry.second->order > newest->order) newest = entry.second;
		}
	}
	bool gatheringComplete = false;
	if (newest) {
		std::lock_guard<std::mutex> gLock(m_GatheringMutex);
		gatheringComplete = newest->gatheringComplete;
	}
	nlohmann::json status{
		{"connected", anyConnected},
		{"peers", peerCount},
		{"iceState", static_cast<int>(newest ? newest->connection->iceState() : PeerConnection::IceState::Closed)},
		{"gatheringComplete", gatheringComplete},
	};
	return status.dump();
}

bool WebRTCSink::EnsureEncoderInitialized(int width, int height)
{
	if (m_CodecContext && m_EncoderWidth == width && m_EncoderHeight == height) {
		return true;
	}
	ShutdownEncoder();

	const AVCodec* codec = avcodec_find_encoder_by_name(m_Config.encoderName.c_str());
	if (!codec) {
		if (m_Logger) m_Logger->EnterLog(::LogLevel::Error, "WebRTCSink: encoder not found: " + m_Config.encoderName);
		return false;
	}

	m_CodecContext = avcodec_alloc_context3(codec);
	m_CodecContext->width = width;
	m_CodecContext->height = height;
	m_CodecContext->time_base = AVRational{ 1, m_Config.fps };
	m_CodecContext->framerate = AVRational{ m_Config.fps, 1 };
	// NV12 when RGA is compiled in (what RgaColorConverter produces, and the format the RK3588 VPU prefers for
	// h264_rkmpp); YUV420P otherwise.
#ifdef LUMEN_WITH_RGA
	const AVPixelFormat convertedPixFmt = AV_PIX_FMT_NV12;
#else
	const AVPixelFormat convertedPixFmt = AV_PIX_FMT_YUV420P;
#endif
	m_CodecContext->pix_fmt = convertedPixFmt;
	m_CodecContext->bit_rate = static_cast<int64_t>(m_Config.bitrateKbps) * 1000;
	m_CodecContext->gop_size = m_Config.fps * 2;
	m_CodecContext->max_b_frames = 0; // zero-latency streaming, not file encoding
	av_opt_set(m_CodecContext->priv_data, "preset", "ultrafast", 0);
	av_opt_set(m_CodecContext->priv_data, "tune", "zerolatency", 0);

	if (avcodec_open2(m_CodecContext, codec, nullptr) < 0) {
		if (m_Logger) m_Logger->EnterLog(::LogLevel::Error, "WebRTCSink: avcodec_open2 failed for " + m_Config.encoderName);
		avcodec_free_context(&m_CodecContext);
		return false;
	}

	// always set up: sws_scale is the runtime fallback whenever RgaColorConverter::ConvertBgrToNv12 fails (see EncodeAndSend)
	m_SwsContext = sws_getContext(width, height, AV_PIX_FMT_BGR24, width, height, convertedPixFmt,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	m_YuvFrame = av_frame_alloc();
	m_YuvFrame->format = convertedPixFmt;
	m_YuvFrame->width = width;
	m_YuvFrame->height = height;
	av_frame_get_buffer(m_YuvFrame, 32);

	m_EncoderWidth = width;
	m_EncoderHeight = height;
	m_FrameCounter = 0;
	return true;
}

void WebRTCSink::ShutdownEncoder()
{
	if (m_YuvFrame) av_frame_free(&m_YuvFrame);
	if (m_SwsContext) { sws_freeContext(m_SwsContext); m_SwsContext = nullptr; }
	if (m_CodecContext) avcodec_free_context(&m_CodecContext);
	m_EncoderWidth = m_EncoderHeight = 0;
}

void WebRTCSink::EncodeAndSend(const cv::Mat& sourceFrame)
{
	{
		std::lock_guard<std::mutex> lock(m_SettingsMutex);
		if (m_SettingsChanged) {
			m_Config.bitrateKbps = m_PendingConfig.bitrateKbps;
			m_Config.fps = m_PendingConfig.fps;
			m_ScaleDivisor = m_PendingScaleDivisor;
			m_SettingsChanged = false;
			ShutdownEncoder(); // rebuilt below with the new bitrate and frame rate
		}
	}
	cv::Mat scaled;
	if (m_ScaleDivisor > 1) cv::resize(sourceFrame, scaled, cv::Size(), 1.0 / m_ScaleDivisor, 1.0 / m_ScaleDivisor, cv::INTER_AREA);
	const cv::Mat& bgrFrame = m_ScaleDivisor > 1 ? scaled : sourceFrame;

	std::vector<std::shared_ptr<Peer>> openPeers;
	{
		std::lock_guard<std::mutex> lock(m_ConnectionMutex);
		PruneClosedPeers();
		for (const auto& entry : m_Peers) {
			if (entry.second->track && entry.second->track->isOpen()) openPeers.push_back(entry.second);
		}
	}
	if (openPeers.empty()) return;
	// a viewer that has just opened its track can only start decoding at a keyframe, so the encoder restarts (its first frame is one)
	bool newViewer = false;
	for (const auto& peer : openPeers) {
		if (!peer->streaming) { peer->streaming = true; newViewer = true; }
	}
	if (newViewer) ShutdownEncoder();
	if (!EnsureEncoderInitialized(bgrFrame.cols, bgrFrame.rows)) return;

	// RGA first (hardware); sws_scale is the fallback when RGA is not compiled in or the hardware call fails at
	// runtime (busy/absent RGA, unsupported size)
	bool converted = false;
#ifdef LUMEN_WITH_RGA
	converted = m_RgaConverter.ConvertBgrToNv12(bgrFrame, m_YuvFrame);
	if (!converted && m_Logger && !m_RgaConversionFailureLogged) {
		m_Logger->EnterLog(::LogLevel::Warning, "WebRTCSink: RGA colour conversion failed, falling back to sws_scale (logged once)");
		m_RgaConversionFailureLogged = true;
	}
#endif
	if (!converted) {
		const uint8_t* srcSlices[1] = { bgrFrame.data };
		int srcStride[1] = { static_cast<int>(bgrFrame.step) };
		sws_scale(m_SwsContext, srcSlices, srcStride, 0, bgrFrame.rows, m_YuvFrame->data, m_YuvFrame->linesize);
	}
	m_YuvFrame->pts = m_FrameCounter++;

	if (avcodec_send_frame(m_CodecContext, m_YuvFrame) < 0) return;

	AVPacket* packet = av_packet_alloc();
	while (avcodec_receive_packet(m_CodecContext, packet) == 0) {
		binary sample(reinterpret_cast<byte*>(packet->data), reinterpret_cast<byte*>(packet->data) + packet->size);
		double elapsedSeconds = static_cast<double>(m_FrameCounter) / m_Config.fps;
		for (const auto& peer : openPeers) {
			try {
				peer->track->sendFrame(sample, std::chrono::duration<double>(elapsedSeconds));
			} catch (const std::exception& e) {
				if (m_Logger) m_Logger->EnterLog(::LogLevel::Warning, std::string("WebRTCSink: sendFrame failed: ") + e.what());
			}
		}
		av_packet_unref(packet);
	}
	av_packet_free(&packet);
}

void WebRTCSink::Process(const std::vector<SourceResult>& results)
{
	for (const SourceResult& result : results) {
		if (result.frame.has_value() && !result.frame.value().empty()) {
			EncodeAndSend(result.frame->AsBgr());
		}
	}
}

#endif // LUMEN_WITH_WEBRTC
