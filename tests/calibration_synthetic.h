#pragma once
#include "ISource.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

// Renders a planar 9x6-interior-corner checkerboard (25 mm squares) as seen by a pinhole camera.
namespace synthetic {

constexpr int kCols = 9, kRows = 6;
constexpr double kSquare = 0.025;
constexpr int kWidth = 1280, kHeight = 720;
constexpr double kFocal = 900.0;

inline cv::Matx33d Intrinsics() { return cv::Matx33d(kFocal, 0, kWidth / 2.0, 0, kFocal, kHeight / 2.0, 0, 0, 1); }

// Flat board image with a one-square white margin; interior corner (0,0) sits one square in from the pattern's edge.
inline cv::Mat BoardImage(int& pxPerSquare, int& originPx)
{
	pxPerSquare = 40;
	const int squaresX = kCols + 1, squaresY = kRows + 1;
	const int margin = pxPerSquare;
	cv::Mat board(squaresY * pxPerSquare + 2 * margin, squaresX * pxPerSquare + 2 * margin, CV_8UC1, cv::Scalar(255));
	for (int y = 0; y < squaresY; y++)
		for (int x = 0; x < squaresX; x++)
			if ((x + y) % 2 == 0)
				board(cv::Rect(margin + x * pxPerSquare, margin + y * pxPerSquare, pxPerSquare, pxPerSquare)).setTo(0);
	originPx = margin + pxPerSquare;
	return board;
}

// The board seen with rotation rvec and translation t (metres, board frame to camera frame).
inline cv::Mat RenderBoard(const cv::Vec3d& rvec, const cv::Vec3d& t)
{
	static int pxPerSquare = 0, originPx = 0;
	static const cv::Mat board = BoardImage(pxPerSquare, originPx);

	cv::Matx33d R;
	cv::Rodrigues(rvec, R);
	cv::Matx33d plane(R(0, 0), R(0, 1), t[0], R(1, 0), R(1, 1), t[1], R(2, 0), R(2, 1), t[2]);
	const double k = kSquare / pxPerSquare;
	cv::Matx33d pixelToWorld(k, 0, -originPx * k, 0, k, -originPx * k, 0, 0, 1);
	cv::Matx33d H = Intrinsics() * plane * pixelToWorld;

	cv::Mat out;
	cv::warpPerspective(board, out, cv::Mat(H), cv::Size(kWidth, kHeight), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255));
	return out;
}

// Deterministic set of varied board poses; index in [0, 15).
inline void Pose(int i, cv::Vec3d& rvec, cv::Vec3d& t)
{
	rvec = cv::Vec3d((i % 5 - 2) * 0.15, ((i / 5) % 3 - 1) * 0.25, (i % 4 - 1.5) * 0.15);
	t = cv::Vec3d(-0.11 + (i % 3) * 0.03, -0.07 + (i % 2) * 0.03, 0.5 + 0.02 * (i % 5));
}

// Publishes whatever frame the test last set, with an explicit capture timestamp.
class FrameSource : public ISource {
public:
	FrameSource(std::shared_ptr<Logger> logger, std::string id, std::atomic<uint64_t>* clock)
		: ISource(logger, id), m_Clock(clock) {}

	void SetFrame(const cv::Mat& gray) {
		std::lock_guard<std::mutex> lock(m_Mutex);
		cv::cvtColor(gray, m_Frame, cv::COLOR_GRAY2BGR);
	}

	// Blocks until the source has published n more frames, then leaves the sinks time to process them.
	void WaitForFrames(int n) {
		int start = m_Published.load();
		while (m_Published.load() < start + n) std::this_thread::sleep_for(std::chrono::milliseconds(2));
		std::this_thread::sleep_for(std::chrono::milliseconds(120));
	}

protected:
	void CaptureFrame() override {
		cv::Mat frame;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			frame = m_Frame.clone();
		}
		if (!frame.empty()) {
			SetLatestResult(SourceResult(std::nullopt, frame, m_Clock->load()));
			m_Published++;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

private:
	std::atomic<uint64_t>* m_Clock;
	std::mutex m_Mutex;
	cv::Mat m_Frame;
	std::atomic<int> m_Published{0};
};

}
