#include "MjpegHwEncoder.h"
#include <algorithm>
#include <cstdlib>

#if defined(LUMEN_WITH_WEBRTC) || defined(LUMEN_WITH_RECORD)
#include "Nv12Convert.h"
#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

struct MjpegHwEncoder::Impl {
	const AVCodec* codec = nullptr; // null: no hardware JPEG encoder here (or turned off)
	bool disabled = false;
	AVCodecContext* context = nullptr;
	AVFrame* frame = nullptr;
	AVPacket* packet = nullptr;
	int width = 0, height = 0, quality = 0;
	AVPixelFormat format = AV_PIX_FMT_NONE;
	int64_t pts = 0;
	Nv12ToAvFrame nv12{ false }; // a JPEG keeps the camera's full range
	cv::Mat scaledBgr;

	Impl()
	{
		const char* off = std::getenv("LUMEN_MJPEG_HW_DISABLE");
		disabled = off && off[0] == '1';
		if (!disabled) codec = avcodec_find_encoder_by_name("mjpeg_rkmpp");
	}

	~Impl() { Close(); }

	void Close()
	{
		avcodec_free_context(&context);
		av_frame_free(&frame);
		av_packet_free(&packet);
		width = height = quality = 0;
		format = AV_PIX_FMT_NONE;
	}

	bool Open(int w, int h, AVPixelFormat fmt, int q, std::string& error)
	{
		if (context && w == width && h == height && fmt == format && q == quality) return true;
		Close();
		context = avcodec_alloc_context3(codec);
		context->width = w;
		context->height = h;
		context->pix_fmt = fmt;
		context->time_base = AVRational{ 1, 30 };
		context->framerate = AVRational{ 30, 1 };
		// the camera's NV12 is already JPEG range, and a JPEG is written in full range whatever the input: saying so spares a range conversion
		context->color_range = AVCOL_RANGE_JPEG;
		// one frame in, its JPEG out; without this the encoder keeps up to eight frames in flight and hands each one back late
		context->flags |= AV_CODEC_FLAG_LOW_DELAY;
		// a fixed quality factor (the encoder's own scale, 1-99), as cv::IMWRITE_JPEG_QUALITY is
		const int qualityFactor = std::clamp(q, 1, 99);
		av_opt_set_int(context->priv_data, "qp_init", qualityFactor, 0);
		av_opt_set_int(context->priv_data, "qp_min", qualityFactor, 0);
		av_opt_set_int(context->priv_data, "qp_max", qualityFactor, 0);
		if (avcodec_open2(context, codec, nullptr) < 0) {
			error = "mjpeg_rkmpp could not be opened";
			Close();
			return false;
		}
		frame = av_frame_alloc();
		frame->format = fmt;
		frame->width = w;
		frame->height = h;
		frame->color_range = AVCOL_RANGE_JPEG;
		packet = av_packet_alloc();
		if (av_frame_get_buffer(frame, 64) < 0 || !packet) {
			error = "no encoder input buffer";
			Close();
			return false;
		}
		width = w;
		height = h;
		format = fmt;
		quality = q;
		return true;
	}
};

bool MjpegHwEncoder::Encode(const Frame& src, int quality, int scaleDivisor, std::vector<uint8_t>& jpeg)
{
	Impl& impl = *m_Impl;
	if (!impl.codec) {
		m_LastError = impl.disabled ? "turned off by LUMEN_MJPEG_HW_DISABLE" : "this FFmpeg has no mjpeg_rkmpp encoder";
		return false;
	}
	const int divisor = std::max(1, scaleDivisor);
	const cv::Size full = src.size();
	// even sides, as NV12 needs
	const int w = (full.width / divisor) & ~1, h = (full.height / divisor) & ~1;
	if (w < 16 || h < 16) {
		m_LastError = "frame too small";
		return false;
	}
	const bool nv12 = src.IsNv12();
	if (!impl.Open(w, h, nv12 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_BGR0, quality, m_LastError)) return false;
	if (av_frame_make_writable(impl.frame) < 0) {
		m_LastError = "encoder input buffer not writable";
		return false;
	}

	if (nv12) {
		if (!impl.nv12.Convert(src, impl.frame)) {
			m_LastError = "NV12 planes could not be copied";
			return false;
		}
	} else {
		const cv::Mat* bgr = &src.AsBgr();
		if (bgr->cols != w || bgr->rows != h) {
			cv::resize(*bgr, impl.scaledBgr, cv::Size(w, h), 0, 0, cv::INTER_AREA);
			bgr = &impl.scaledBgr;
		}
		// 32-bit BGR (the fourth byte unused) is what the encoder takes in; written straight into its input buffer
		cv::Mat input(h, w, CV_8UC4, impl.frame->data[0], static_cast<size_t>(impl.frame->linesize[0]));
		cv::cvtColor(*bgr, input, cv::COLOR_BGR2BGRA);
	}

	impl.frame->pts = impl.pts++;
	if (avcodec_send_frame(impl.context, impl.frame) < 0) {
		m_LastError = "the encoder refused the frame";
		impl.Close();
		return false;
	}
	if (avcodec_receive_packet(impl.context, impl.packet) < 0) {
		m_LastError = "the encoder returned no JPEG";
		impl.Close();
		return false;
	}
	jpeg.assign(impl.packet->data, impl.packet->data + impl.packet->size);
	av_packet_unref(impl.packet);
	m_LastError.clear();
	return true;
}

#else

struct MjpegHwEncoder::Impl {};

bool MjpegHwEncoder::Encode(const Frame&, int, int, std::vector<uint8_t>&)
{
	m_LastError = "built without FFmpeg";
	return false;
}

#endif

MjpegHwEncoder::MjpegHwEncoder() : m_Impl(std::make_unique<Impl>()) {}

MjpegHwEncoder::~MjpegHwEncoder() = default;
