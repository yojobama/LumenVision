#pragma once
#include <atomic>
#include "ISource.h"
#include "ISink.h"
#include "IStereoRoleReceiver.h"
#include "IStereoDepthBackend.h"
#include "StereoCalibrationResult.h"
#include "StereoDepthBackendKind.h"
#include "StereoFrameOutput.h"
#include "StereoPairer.h"

#include <deque>
#include <memory>
#include <optional>
#include <mutex>

// ISource+ISink, maxSources=2 (left/right frame-producing nodes). Rectifies each paired frame using maps from a
// StereoCalibrationResult, runs an IStereoDepthBackend on the pair and converts the block-grid disparity to depth.
class StereoDepthNode : public ISink, public ISource, public IStereoRoleReceiver
{
public:
	StereoDepthNode(std::shared_ptr<Logger> logger, std::string id,
		StereoDepthBackendKind backend, StereoCalibrationResult calibration,
		double minDepthMeters, double maxDepthMeters,
		int64_t maxSkewUs, StereoFrameOutput frameOutput);
	~StereoDepthNode();

	// IStereoRoleReceiver
	void SetStereoRoles(const std::string& leftSourceId, const std::string& rightSourceId) override;

	std::string GetBackendName() const;

	// summary stats from the most recent pair; carried by GetSinkResult's JSON since the full cols*rows grid is too
	// large to push over NT4/REST every frame
	double GetLastValidFraction() const;
	double GetLastMedianDepthMeters() const;

	// in-process access to the full block-grid depth map (not serialised via SourceResult/JSON); returns false if no
	// pair has been processed. outDepth is cols*rows metres, 0.0f for invalid cells
	// the crop's top-left in the rectified image; subtract it from the rectified principal point to project output pixels
	void GetCropOrigin(int& x, int& y) const { x = m_CropX; y = m_CropY; }
	// The depth grid computed for the pair captured nearest to `captureTimeUs` (the last few are kept), so a detection made on the output frame of
	// one pair is fused with that pair's depth and not a newer one's; 0 asks for the newest. Same outputs as GetLastDepthGrid.
	bool GetDepthGridFor(uint64_t captureTimeUs, std::vector<float>& outDepth, int& cols, int& rows, int& blockW, int& blockH) const;
	bool GetLastDepthGrid(std::vector<float>& outDepth, int& cols, int& rows, int& blockW, int& blockH) const;
	// calibration this node is using (DepthFusionNode needs rectifiedFx/Cx/Cy)
	StereoCalibrationResult GetCalibration() const { return m_Calibration; }

private:
	void Process(const std::vector<SourceResult>& results) override;
	void EnsureRectifyMaps(const cv::Size& sourceSize);
	void EnsureBackend(int croppedW, int croppedH);
	// synthesises a known 16px shift from a real captured frame and checks which sign the backend recovers;
	// runs once, on the first successful pair, and logs the result
	void RunSignSelfCheckIfNeeded(const cv::Mat& rectLeftGray);

	std::shared_ptr<Logger> m_Logger;
	StereoDepthBackendKind m_BackendKind;
	StereoCalibrationResult m_Calibration;
	double m_MinDepthMeters, m_MaxDepthMeters;
	int64_t m_MaxSkewUs;
	StereoFrameOutput m_FrameOutput;

	std::string m_LeftSourceId, m_RightSourceId;
	std::optional<StereoPairer> m_Pairer;

	// rectification maps, built once and rebuilt if the source resolution changes (a calibration is only valid at its own resolution)
	cv::Mat m_MapLx, m_MapLy, m_MapRx, m_MapRy;
	cv::Size m_RectifiedSourceSize;
	int m_CropW = 0, m_CropH = 0;
	// where the crop sits in the rectified image: the top-left of the intersection of both eyes' valid regions (the output frame and the depth grid
	// start there, so a pixel of the output is at (x + cropX, y + cropY) in the rectified image)
	std::atomic<int> m_CropX{ 0 }, m_CropY{ 0 };

	std::unique_ptr<IStereoDepthBackend> m_Backend;
	bool m_SignCheckDone = false;
	bool m_InvertDisparitySign = false;

	mutable std::mutex m_StatsMutex;
	double m_LastValidFraction = 0.0;
	double m_LastMedianDepthMeters = 0.0;
	struct DepthSnapshot {
		uint64_t captureTimeUs = 0;
		std::vector<float> depth;
		int cols = 0, rows = 0;
	};
	std::deque<DepthSnapshot> m_RecentGrids; // guarded by m_StatsMutex, oldest first
	std::vector<float> m_LastDepthGrid;
	int m_LastCols = 0, m_LastRows = 0;
	bool m_HasDepthGrid = false;
};
