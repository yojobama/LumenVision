// Edge-case hunt, area D (finding D9): ISource::RequestFrameOnce against a camera whose colour/grey decision is taken before it blocks for the
// next frame. Lives with the frame/source tests because the fix is in the source side (Stage 2).
#include <catch2/catch_test_macros.hpp>
#include "ISource.h"
#include "Frame.h"
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {
// A camera as CameraFrameSource::CaptureFrame drives it: the colour-or-grey decision (HasActiveColorFrameConsumer, passed to
// ICameraBackend::Grab as preferGray) is taken BEFORE the grab, which then blocks until the driver hands over the next frame
// (V4l2CameraBackend::Grab polls the device), and the frame is decoded the way that decision said.
class EdgeGreyDecodingCamera : public ISource {
public:
	explicit EdgeGreyDecodingCamera(std::string id) : ISource(nullptr, std::move(id)) {}
	~EdgeGreyDecodingCamera() override { Toggle(false); }

protected:
	void CaptureFrame() override
	{
		const bool colour = HasActiveColorFrameConsumer(); // CameraSource.cpp: m_Backend->Grab(!HasActiveColorFrameConsumer())
		std::this_thread::sleep_for(std::chrono::milliseconds(30)); // poll() waiting for the next frame of a 30 fps camera
		cv::Mat image(48, 64, colour ? CV_8UC3 : CV_8UC1, cv::Scalar::all(colour ? 200 : 100));
		SetLatestResult(SourceResult(std::nullopt, Frame(image, colour ? FrameFormat::BGR24 : FrameFormat::GRAY8)));
	}
};
} // namespace

// D9. ISource::RequestFrameOnce (used by Manager::SaveFreshSnapshot: the snapshot button and the robot's NT snapshot request) is cleared by
// the next published result that carries a frame, even one whose colour/annotation decision was taken before the request arrived. A camera
// that only feeds an AprilTag detector decodes grey, and it spends nearly all of its frame period blocked in Grab() after that decision, so the
// request is almost always consumed by a grey frame that was already in flight. SaveFreshSnapshot sees "a new frame" and saves it: the
// snapshot is grey and the request for a colour frame is lost. The loop below is SaveFreshSnapshot's own protocol.
TEST_CASE("D9: a fresh snapshot of a grey-decoding camera is taken from a colour frame", "[edge][frames]") {
	auto camera = std::make_shared<EdgeGreyDecodingCamera>("d9-camera");
	camera->Toggle(true);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	int grey = 0, none = 0;
	const int trials = 12;
	for (int t = 0; t < trials; t++) {
		std::this_thread::sleep_for(std::chrono::milliseconds(7 * t % 30)); // land at different points of the frame period
		// Manager::SaveFreshSnapshot
		const uint64_t framesBefore = camera->GetCurrentFrameCount();
		camera->RequestFrameOnce();
		bool gotFrame = false;
		for (int waitedMs = 0; waitedMs < 1000; waitedMs += 10) {
			if (camera->GetCurrentFrameCount() > framesBefore) {
				SourceResult fresh = camera->GetLatestResult();
				// the frame has to answer the request (Manager::SaveFreshSnapshot): one already in flight does not
				if (fresh.frame.has_value() && !fresh.frame->empty() && !camera->HasPendingFrameRequest()) { gotFrame = true; break; }
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		SourceResult saved = camera->GetLatestResult(); // what SaveSnapshot then writes
		if (!gotFrame || !saved.frame.has_value()) none++;
		else if (saved.frame->format() != FrameFormat::BGR24) grey++;
	}
	camera->Toggle(false);
	INFO(grey << " of " << trials << " snapshots were taken from a grey frame, " << none << " got no frame");
	CHECK(grey == 0);
	CHECK(none == 0);
}

