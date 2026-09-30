#include "Manager.h"
#include "ImageFileSource.h"
#include "VideoFileSource.h"
#include "ApriltagDetector.h"
#include "CameraCalibrator.h"
#include "StereoCalibrator.h"
#include "StereoDepthNode.h"
#include "DepthFusionNode.h"
#include "IStereoRoleReceiver.h"
#include "CameraSource.h"
#include "RoiSource.h"
#include "SystemMonitor.h"
#include "ISink.h"
#include "ObjectDetectionSink.h"
#include "MjpegSink.h"
#ifdef LUMEN_WITH_ONNX
#include "OnnxDetectionBackend.h"
#endif
#ifdef LUMEN_WITH_RKNN
#include "RknnDetectionBackend.h"
#endif
#ifdef LUMEN_WITH_NT4
#include "NetworkTablesSink.h"
#endif
#ifdef LUMEN_WITH_WEBRTC
#include "WebRTCSink.h"
#endif
#ifdef LUMEN_WITH_RECORD
#include "RecordSink.h"
#endif

#include <cstring>
#include <cctype>
#include <stdexcept>
#ifdef __linux__
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <climits>
#include <cstdlib>
#include <map>
#endif
#ifdef _WIN32
// Not #include <windows.h>/<mfapi.h> here: Manager.h's `using namespace std;` makes the Windows SDK's unqualified `byte` ambiguous with
// std::byte. WindowsCameraEnumerator.cpp is a `using namespace std`-free translation unit for this.
#include "WindowsCameraEnumerator.h"
#endif
#include <cstring>
#include <iostream>
#include <opencv2/opencv.hpp>

Manager::Manager(string logFile)
{
    m_Logger = std::make_shared<Logger>(logFile);
    m_Logger->EnterLog("Manager constructed");
	m_SystemMonitor = new SystemMonitor(1000); // 1 second interval
	m_SystemMonitor->StartMonitoring();
}

Manager::Manager()
{
    m_Logger = std::make_shared<Logger>("LumenVision.log");
    m_Logger->EnterLog("Manager constructed");
	m_SystemMonitor = new SystemMonitor(1000); // 1 second interval
    m_SystemMonitor->StartMonitoring();
}

Manager::~Manager()
{
	m_SystemMonitor->StopMonitoring();
    m_Logger->EnterLog("Manager destructed");
    delete m_SystemMonitor;
    //m_CameraCalibrationSinks.clear();
    m_Sources.clear();
	m_Sinks.clear();
}

vector<int> Manager::GetAllSinks()
{
    m_Logger->EnterLog("GetAllSinks called");
    vector<int> returnVector;

    auto iterator = m_Sinks.begin();

    while (iterator != m_Sinks.end())
    {
        returnVector.push_back(iterator->first);
        iterator++;
    }

    return returnVector;
}

vector<int> Manager::GetAllSources()
{
	vector<int> sourceIds;

	auto iterator = m_Sources.begin();

    while (iterator != m_Sources.end()) {
		sourceIds.push_back(iterator->first);
        iterator++;
    }

    return sourceIds;
}

std::vector<std::string> Manager::GetAvailableVideoEncoders()
{
    //return FFmpegUtils::GetAvailableVideoEncoders();
    throw std::runtime_error("GetAvailableVideoEncoders not implemented yet");
}

#ifdef __linux__
vector<CameraHardwareInfo> Manager::EnumerateAvailableCameras()
{
    m_Logger->EnterLog("EnumerateAvailableCameras called");
    vector<CameraHardwareInfo> cameras;

    // Stable identity: /dev/videoN numbering reorders between boots and identical cameras share name and serial, so report udev's
    // /dev/v4l/by-path link (physical USB port / CSI bus) as the path when one exists; open() resolves the symlink.
    // Moving a camera to another port makes it a different camera.
    std::map<std::string, std::string> stablePathByNode;
    if (DIR* p_ByPath = opendir("/dev/v4l/by-path")) {
        while (struct dirent* p_Link = readdir(p_ByPath)) {
            if (p_Link->d_name[0] == '.') continue;
            std::string linkPath = std::string("/dev/v4l/by-path/") + p_Link->d_name;
            char resolved[PATH_MAX];
            if (realpath(linkPath.c_str(), resolved) != nullptr) {
                // lexicographically smallest link wins if a node has several (deterministic
                // rather than readdir-order dependent)
                auto [it, inserted] = stablePathByNode.emplace(resolved, linkPath);
                if (!inserted && linkPath < it->second) it->second = linkPath;
            }
        }
        closedir(p_ByPath);
    }

    const char* p_VideoDir = "/dev/";
    DIR* p_Dir = opendir(p_VideoDir);
    if (!p_Dir) {
        m_Logger->EnterLog("Failed to open /dev/ directory");
        return cameras;
    }

    struct dirent* p_Entry;
    while ((p_Entry = readdir(p_Dir)) != nullptr) {
        if (strncmp(p_Entry->d_name, "video", 5) != 0) continue;

        std::string devicePath = std::string(p_VideoDir) + p_Entry->d_name;
        int fd = open(devicePath.c_str(), O_RDONLY);
        if (fd < 0) {
            m_Logger->EnterLog("Failed to open device: " + devicePath);
            continue;
        }

        struct v4l2_capability cap;
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) != 0) {
            close(fd);
            continue;
        }

        // A real capability check. When V4L2_CAP_DEVICE_CAPS is set, cap.capabilities is the UNION across every node of a
        // multi-function device; the per-node truth is in cap.device_caps.
        __u32 effectiveCaps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (!(effectiveCaps & V4L2_CAP_VIDEO_CAPTURE) || !(effectiveCaps & V4L2_CAP_STREAMING)) {
            close(fd);
            continue;
        }

        // and at least one enumerable capture format - a device that passes the capability bits
        // but advertises zero formats isn't something this project can actually open.
        v4l2_fmtdesc fmtDesc{};
        fmtDesc.index = 0;
        fmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &fmtDesc) != 0) {
            close(fd);
            continue;
        }

        std::string rawName = reinterpret_cast<char*>(cap.card);
        std::string formattedName;
        for (char c : rawName) {
            if (isalnum(static_cast<unsigned char>(c))) {
                formattedName += c;
            }
            else if (c == ' ' || c == '-' || c == '.') {
                formattedName += '_';
            }
        }
        size_t start = formattedName.find_first_not_of('_');
        size_t end = formattedName.find_last_not_of('_');
        std::string deviceName = (start != std::string::npos && end != std::string::npos)
            ? formattedName.substr(start, end - start + 1)
            : devicePath;
        close(fd);

        auto stable = stablePathByNode.find(devicePath);
        std::string reportedPath = stable != stablePathByNode.end() ? stable->second : devicePath;
        cameras.push_back(CameraHardwareInfo{ .name = deviceName, .path = reportedPath });
        m_Logger->EnterLog("Camera found: " + deviceName + " at " + reportedPath +
            (reportedPath != devicePath ? " (" + devicePath + ")" : ""));
    }
    closedir(p_Dir);
    return cameras;
}
#else
// Media Foundation's device-source enumerator (MFEnumDeviceSources), the same API the Windows Camera app queries.
// Whether cv::VideoCapture can then OPEN a listed device is a separate matter (see OpenCvCameraBackend.cpp).
vector<CameraHardwareInfo> Manager::EnumerateAvailableCameras()
{
    m_Logger->EnterLog("EnumerateAvailableCameras called");
    vector<CameraHardwareInfo> cameras;

    for (const WindowsCameraDevice& device : EnumerateWindowsCameras(m_Logger)) {
        // MFEnumDeviceSources' array position IS the index OpenCvCameraBackend::Open's numeric-index branch (cv::VideoCapture(index, backend))
        // opens, so return the position rather than the symbolic-link path, which OpenCvCameraBackend cannot open reliably.
        cameras.push_back(CameraHardwareInfo{ .name = device.name, .path = std::to_string(device.index) });
    }

    return cameras;
}
#endif

bool Manager::BindStereoSources(int sinkId, int leftSourceId, int rightSourceId) {
    m_Logger->EnterLog("BindStereoSources called with sinkId=" + std::to_string(sinkId) +
        ", leftSourceId=" + std::to_string(leftSourceId) + ", rightSourceId=" + std::to_string(rightSourceId));

    auto sinkIt = m_Sinks.find(sinkId);
    if (sinkIt == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return false;
    }
    auto leftIt = m_Sources.find(leftSourceId);
    auto rightIt = m_Sources.find(rightSourceId);
    if (leftIt == m_Sources.end() || rightIt == m_Sources.end()) {
        m_Logger->EnterLog("BindStereoSources: left or right source not found");
        return false;
    }

    IStereoRoleReceiver* p_RoleReceiver = dynamic_cast<IStereoRoleReceiver*>(sinkIt->second.get());
    if (p_RoleReceiver == nullptr) {
        m_Logger->EnterLog(LogLevel::Error, "BindStereoSources: sink " + std::to_string(sinkId) + " is not a stereo node");
        return false;
    }

    bool bound = sinkIt->second->BindSource(leftIt->second) && sinkIt->second->BindSource(rightIt->second);
    if (bound) {
        // recorded explicitly by source ID, as ISink::BindSource's bind order has no left/right notion (see IStereoRoleReceiver.h)
        p_RoleReceiver->SetStereoRoles(leftIt->second->GetID(), rightIt->second->GetID());
    }
    m_Logger->EnterLog("BindStereoSources result: " + std::to_string(bound));
    return bound;
}

bool Manager::BindSourceToSink(int sourceId, int sinkId) {
    m_Logger->EnterLog("BindSourceToSink called with sourceId=" + std::to_string(sourceId) + ", sinkId=" + std::to_string(sinkId));
    auto source = m_Sources.find(sourceId);
    if (source == m_Sources.end()) {
        m_Logger->EnterLog("Source not found: " + std::to_string(sourceId));
        return false;
    }
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return false;
    }

    bool result = sink->second->BindSource(source->second);
    m_Logger->EnterLog("BindSourceToSink result: " + std::to_string(result));
    return result;
}

bool Manager::UnbindSourceFromSink(int sinkId) {
    m_Logger->EnterLog("UnbindSourceFromSink called with sinkId=" + std::to_string(sinkId));
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return false;
    }

    for (const auto& sourcePair : m_Sources) {
        if (sink->second->UnbindSource(sourcePair.second->GetID())) {
            m_Logger->EnterLog("UnbindSourceFromSink result: true");
            return true;
        }
    }

    bool result = false;
    m_Logger->EnterLog("UnbindSourceFromSink result: " + std::to_string(result));
    return result;
}

bool Manager::DeleteSink(int sinkId)
{
    m_Logger->EnterLog("DeleteSink called with sinkId=" + std::to_string(sinkId));
    auto sinkIt = m_Sinks.find(sinkId);
    if (sinkIt == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return false;
    }

    std::shared_ptr<ISink> p_Sink = sinkIt->second;
    p_Sink->Toggle(false);

    // dual-role nodes (e.g. CameraCalibrator, ApriltagDetector) are registered under the same id
    // in m_Sources too; stop that half and unbind every other sink from it before erasing either,
    // so nothing is left holding a dangling reference
    auto sourceIt = m_Sources.find(sinkId);
    if (sourceIt != m_Sources.end() && sourceIt->second == std::dynamic_pointer_cast<ISource>(p_Sink)) {
        sourceIt->second->Toggle(false);
        string sourceStringId = sourceIt->second->GetID();
        for (auto& otherSinkPair : m_Sinks) {
            if (otherSinkPair.first != sinkId) {
                otherSinkPair.second->UnbindSource(sourceStringId);
            }
        }
        m_Sources.erase(sourceIt);
    }

    m_Sinks.erase(sinkIt);
    m_Logger->EnterLog("DeleteSink removed sinkId=" + std::to_string(sinkId));
    return true;
}

bool Manager::DeleteSource(int sourceId)
{
    m_Logger->EnterLog("DeleteSource called with sourceId=" + std::to_string(sourceId));
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        m_Logger->EnterLog("Source not found: " + std::to_string(sourceId));
        return false;
    }

    std::shared_ptr<ISource> p_Source = sourceIt->second;
    string sourceStringId = p_Source->GetID();
    for (auto& sinkPair : m_Sinks) {
        sinkPair.second->UnbindSource(sourceStringId);
    }
    p_Source->Toggle(false);

    // dual-role nodes are registered under the same id in m_Sinks too; stop that half as well
    auto sinkIt = m_Sinks.find(sourceId);
    if (sinkIt != m_Sinks.end() && sinkIt->second == std::dynamic_pointer_cast<ISink>(p_Source)) {
        sinkIt->second->Toggle(false);
        m_Sinks.erase(sinkIt);
    }

    m_Sources.erase(sourceIt);
    m_Logger->EnterLog("DeleteSource removed sourceId=" + std::to_string(sourceId));
    return true;
}

int Manager::CreateCameraSource(CameraHardwareInfo info)
{
    m_Logger->EnterLog("CreateCameraSource called with name=" + info.name + ", path=" + info.path);
    
    int id = GenerateUUID();

    auto p_Source = std::make_shared<CameraFrameSource>(info.path, info.name, m_Logger, std::to_string(id));

    m_Sources.emplace(id, p_Source);

    m_Logger->EnterLog("CameraFrameSource created with id=" + std::to_string(id));
    
    return id;
}

int Manager::CreateCameraSource(CameraHardwareInfo info, int id)
{
    m_Logger->EnterLog("CreateCameraSource called with name=" + info.name + ", path=" + info.path);

    auto p_Source = std::make_shared<CameraFrameSource>(info.path, info.name, m_Logger, std::to_string(id));

    m_Sources.emplace(id, p_Source);

    m_Logger->EnterLog("CameraFrameSource created with id=" + std::to_string(id));

    return id;
}

vector<CameraMode> Manager::GetCameraModes(int sourceId)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("GetCameraModes: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("GetCameraModes: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->GetAvailableModes();
}

CameraMode Manager::GetCameraCurrentMode(int sourceId)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("GetCameraCurrentMode: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("GetCameraCurrentMode: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->GetCurrentMode();
}

bool Manager::SetCameraMode(int sourceId, CameraMode mode)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("SetCameraMode: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("SetCameraMode: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    bool applied = p_CameraSource->SetMode(mode);
    m_Logger->EnterLog("SetCameraMode sourceId=" + std::to_string(sourceId) +
        " requested " + std::to_string(mode.width) + "x" + std::to_string(mode.height) +
        "@" + std::to_string(mode.fps) + " -> ioctl " + (applied ? "ok" : "FAILED"));
    return applied;
}

bool Manager::SetCameraExposure(int sourceId, int exposureAbsolute)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("SetCameraExposure: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("SetCameraExposure: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->SetExposure(exposureAbsolute);
}

bool Manager::SetCameraAutoExposure(int sourceId, bool enabled)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("SetCameraAutoExposure: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("SetCameraAutoExposure: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->SetAutoExposure(enabled);
}

bool Manager::SetCameraGain(int sourceId, int gain)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("SetCameraGain: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("SetCameraGain: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->SetGain(gain);
}

CameraControlRange Manager::GetCameraExposureRange(int sourceId)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("GetCameraExposureRange: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("GetCameraExposureRange: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->GetExposureRange();
}

CameraControlRange Manager::GetCameraGainRange(int sourceId)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("GetCameraGainRange: no source with id=" + std::to_string(sourceId));
    }
    auto p_CameraSource = std::dynamic_pointer_cast<CameraFrameSource>(sourceIt->second);
    if (!p_CameraSource) {
        throw std::runtime_error("GetCameraGainRange: source id=" + std::to_string(sourceId) + " is not a camera source");
    }
    return p_CameraSource->GetGainRange();
}

int Manager::CreateRoiSource(int upstreamSourceId, int x, int y, int width, int height)
{
    m_Logger->EnterLog("CreateRoiSource called with upstreamSourceId=" + std::to_string(upstreamSourceId) +
        " roi=(" + std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(width) + "," + std::to_string(height) + ")");

    if (m_Sources.find(upstreamSourceId) == m_Sources.end()) {
        throw std::runtime_error("CreateRoiSource: no source with id=" + std::to_string(upstreamSourceId));
    }

    int id = GenerateUUID();
    auto p_RoiSource = std::make_shared<RoiSource>(m_Logger, std::to_string(id), cv::Rect(x, y, width, height));

    // RoiSource is both an ISink (consumes the upstream frame) and an ISource (produces the
    // cropped one) - registered in both maps for the same reason ApriltagDetector is.
    m_Sinks.emplace(id, p_RoiSource);
    m_Sources.emplace(id, p_RoiSource);

    BindSourceToSink(upstreamSourceId, id);

    m_Logger->EnterLog("RoiSource created with id=" + std::to_string(id));
    return id;
}

void Manager::SetDriverMode(int sinkId, bool enabled)
{
    auto sinkIt = m_Sinks.find(sinkId);
    if (sinkIt == m_Sinks.end()) {
        throw std::runtime_error("SetDriverMode: no sink with id=" + std::to_string(sinkId));
    }
    if (auto apriltagDetector = std::dynamic_pointer_cast<ApriltagDetector>(sinkIt->second)) {
        apriltagDetector->SetDriverMode(enabled);
        return;
    }
    if (auto objectDetectionSink = std::dynamic_pointer_cast<ObjectDetectionSink>(sinkIt->second)) {
        objectDetectionSink->SetDriverMode(enabled);
        return;
    }
    throw std::runtime_error("SetDriverMode: sink id=" + std::to_string(sinkId) + " does not support driver mode");
}

bool Manager::GetDriverMode(int sinkId)
{
    auto sinkIt = m_Sinks.find(sinkId);
    if (sinkIt == m_Sinks.end()) {
        throw std::runtime_error("GetDriverMode: no sink with id=" + std::to_string(sinkId));
    }
    if (auto apriltagDetector = std::dynamic_pointer_cast<ApriltagDetector>(sinkIt->second)) {
        return apriltagDetector->GetDriverMode();
    }
    if (auto objectDetectionSink = std::dynamic_pointer_cast<ObjectDetectionSink>(sinkIt->second)) {
        return objectDetectionSink->GetDriverMode();
    }
    throw std::runtime_error("GetDriverMode: sink id=" + std::to_string(sinkId) + " does not support driver mode");
}

bool Manager::LoadFieldLayout(int sinkId, string jsonPath)
{
    auto sinkIt = m_Sinks.find(sinkId);
    if (sinkIt == m_Sinks.end()) {
        throw std::runtime_error("LoadFieldLayout: no sink with id=" + std::to_string(sinkId));
    }
    auto apriltagDetector = std::dynamic_pointer_cast<ApriltagDetector>(sinkIt->second);
    if (!apriltagDetector) {
        throw std::runtime_error("LoadFieldLayout: sink id=" + std::to_string(sinkId) + " is not an ApriltagDetector");
    }
    bool ok = apriltagDetector->LoadFieldLayout(jsonPath);
    m_Logger->EnterLog(ok
        ? "LoadFieldLayout: loaded " + std::to_string(apriltagDetector->GetFieldLayoutTagCount()) + " tag(s) from " + jsonPath
        : "LoadFieldLayout: failed to load " + jsonPath);
    return ok;
}

int Manager::GetFieldLayoutTagCount(int sinkId)
{
    auto sinkIt = m_Sinks.find(sinkId);
    if (sinkIt == m_Sinks.end()) {
        throw std::runtime_error("GetFieldLayoutTagCount: no sink with id=" + std::to_string(sinkId));
    }
    auto apriltagDetector = std::dynamic_pointer_cast<ApriltagDetector>(sinkIt->second);
    if (!apriltagDetector) {
        throw std::runtime_error("GetFieldLayoutTagCount: sink id=" + std::to_string(sinkId) + " is not an ApriltagDetector");
    }
    return static_cast<int>(apriltagDetector->GetFieldLayoutTagCount());
}

bool Manager::SaveSnapshot(int sourceId, string path)
{
    auto sourceIt = m_Sources.find(sourceId);
    if (sourceIt == m_Sources.end()) {
        throw std::runtime_error("SaveSnapshot: no source with id=" + std::to_string(sourceId));
    }

    SourceResult result = sourceIt->second->GetLatestResult();
    if (!result.frame.has_value() || result.frame->empty()) {
        m_Logger->EnterLog(LogLevel::Warning, "SaveSnapshot: source id=" + std::to_string(sourceId) + " has no frame yet");
        return false;
    }

    bool ok = cv::imwrite(path, result.frame->AsBgr());
    m_Logger->EnterLog(ok ? "SaveSnapshot: wrote " + path : "SaveSnapshot: cv::imwrite failed for " + path);
    return ok;
}

int Manager::CreateVideoFileSource(string path, int fps)
{
    m_Logger->EnterLog("CreateVideoFileSource called with path=" + path);
    int id = GenerateUUID();

    auto p_Source = std::make_shared<VideoFileFrameSource>(m_Logger, path, fps, std::to_string(id));

    m_Sources.emplace(id, p_Source);

    m_Logger->EnterLog("VideoFileFrameSource created with id=" + std::to_string(id));
    return id;
}

int Manager::CreateVideoFileSource(string path, int fps, int id)
{
    m_Logger->EnterLog("CreateVideoFileSource called with path=" + path);

    auto p_Source = std::make_shared<VideoFileFrameSource>(m_Logger, path, fps, std::to_string(id));

    m_Sources.emplace(id, p_Source);

    m_Logger->EnterLog("VideoFileFrameSource created with id=" + std::to_string(id));
    return id;
}

int Manager::CreateImageFileSource(string path)
{
    m_Logger->EnterLog("CreateImageFileSource called with path=" + path);
    int id = GenerateUUID();

    auto p_Source = std::make_shared<ImageFileFrameSource>(path, m_Logger, std::to_string(id));

    m_Sources.emplace(id, p_Source);

    m_Logger->EnterLog("ImageFileFrameSource created with id=" + std::to_string(id));
    return id;
}

int Manager::CreateImageFileSource(string path, int id)
{
    m_Logger->EnterLog("CreateImageFileSource called with path=" + path);

    auto p_Source = std::make_shared<ImageFileFrameSource>(path, m_Logger, std::to_string(id));

    m_Sources.emplace(id, p_Source);

    m_Logger->EnterLog("ImageFileFrameSource created with id=" + std::to_string(id));
    return id;
}

int Manager::CreateApriltagDetector(CameraCalibrationResult calibrationResult, double tagSize /* metres */)
{
    m_Logger->EnterLog("CreateApriltagDetector called");
    int id = GenerateUUID();

	auto p_Sink = std::make_shared<ApriltagDetector>(m_Logger, std::to_string(id), calibrationResult, tagSize);

    m_Sinks.emplace(id, p_Sink);

    m_Logger->EnterLog("ApriltagDetector created with id=" + std::to_string(id));
    return id;
}

int Manager::CreateApriltagDetector(int id, CameraCalibrationResult calibrationResult, double tagSize /* metres */)
{
    m_Logger->EnterLog("CreateApriltagDetector called");

    auto p_Detector = std::make_shared<ApriltagDetector>(m_Logger, std::to_string(id), calibrationResult, tagSize);

    // ApriltagDetector is both an ISink (consumes camera frames) and an ISource (produces detections),
    // so it must be registered in both maps to be reachable from either side
    m_Sinks.emplace(id, p_Detector);
    m_Sources.emplace(id, p_Detector);

    m_Logger->EnterLog("ApriltagDetector created with id=" + std::to_string(id));
    return id;
}

namespace {
    constexpr double DEFAULT_APRILTAG_SIZE_METERS = 0.1651; // default FRC AprilTag size (6.5 inches), used until real calibration data is supplied
}

int Manager::CreateApriltagDetector()
{
    int id = GenerateUUID();
    return CreateApriltagDetector(id);
}

int Manager::CreateApriltagDetector(int id)
{
    return CreateApriltagDetector(id, CameraCalibrationResult(), DEFAULT_APRILTAG_SIZE_METERS);
}

int Manager::CreateApriltagDetector(CameraCalibrationResult calibrationResult, double tagSize,
    ApriltagBackendKind backendKind, int frameWidth, int frameHeight, int nthreads, float quadDecimate, bool refineEdges)
{
    int id = GenerateUUID();
    return CreateApriltagDetector(id, calibrationResult, tagSize, backendKind, frameWidth, frameHeight, nthreads, quadDecimate, refineEdges);
}

int Manager::CreateApriltagDetector(int id, CameraCalibrationResult calibrationResult, double tagSize,
    ApriltagBackendKind backendKind, int frameWidth, int frameHeight, int nthreads, float quadDecimate, bool refineEdges)
{
    ApriltagTuning tuning;
    tuning.nthreads = nthreads;
    tuning.quadDecimate = quadDecimate;
    tuning.refineEdges = refineEdges;
    return CreateApriltagDetector(id, calibrationResult, tagSize, backendKind, frameWidth, frameHeight, tuning);
}

int Manager::CreateApriltagDetector(CameraCalibrationResult calibrationResult, double tagSize,
    ApriltagBackendKind backendKind, int frameWidth, int frameHeight, ApriltagTuning tuning)
{
    int id = GenerateUUID();
    return CreateApriltagDetector(id, calibrationResult, tagSize, backendKind, frameWidth, frameHeight, tuning);
}

int Manager::CreateApriltagDetector(int id, CameraCalibrationResult calibrationResult, double tagSize,
    ApriltagBackendKind backendKind, int frameWidth, int frameHeight, ApriltagTuning tuning)
{
    m_Logger->EnterLog("CreateApriltagDetector called with id=" + std::to_string(id) + ", backend=" + std::to_string(backendKind));

    auto p_Detector = std::make_shared<ApriltagDetector>(m_Logger, std::to_string(id), calibrationResult, tagSize,
        backendKind, frameWidth, frameHeight, tuning);

    m_Sinks.emplace(id, p_Detector);
    m_Sources.emplace(id, p_Detector);
    return id;
}

string Manager::GetApriltagDetectorBackendName(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return "";

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return "";

    return p_Detector->GetBackendName();
}

ApriltagBackendKind Manager::GetApriltagDetectorBackendKind(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return APRILTAG_BACKEND_CPU;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return APRILTAG_BACKEND_CPU;

    return p_Detector->GetBackendKind();
}

double Manager::GetApriltagDetectorTagSize(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return 0.0;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return 0.0;

    return p_Detector->GetTagSize();
}

CameraCalibrationResult Manager::GetApriltagDetectorCalibration(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return CameraCalibrationResult();

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return CameraCalibrationResult();

    return p_Detector->GetCalibration();
}

int Manager::GetApriltagDetectorThreads(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return 0;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return 0;

    return p_Detector->GetThreads();
}

float Manager::GetApriltagDetectorQuadDecimate(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return 0.0f;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return 0.0f;

    return p_Detector->GetQuadDecimate();
}

bool Manager::GetApriltagDetectorQuadDecimateSupported(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return false;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return false;

    return p_Detector->GetQuadDecimateSupported();
}

bool Manager::GetApriltagDetectorRefineEdges(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return false;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return false;

    return p_Detector->GetRefineEdges();
}

RefineEdgesMode Manager::GetApriltagDetectorRefineMode(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return REFINE_EXACT;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return REFINE_EXACT;

    return p_Detector->GetRefineMode();
}

bool Manager::GetApriltagDetectorRefineModeSupported(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return false;

    ApriltagDetector* p_Detector = dynamic_cast<ApriltagDetector*>(sink->second.get());
    if (p_Detector == nullptr) return false;

    return p_Detector->GetRefineModeSupported();
}

namespace {
	CameraCalibrator* FindCalibrator(map<int, std::shared_ptr<ISink>>& sinks, int calibratorId)
	{
		auto sink = sinks.find(calibratorId);
		if (sink == sinks.end()) return nullptr;
		return dynamic_cast<CameraCalibrator*>(sink->second.get());
	}
}

int Manager::CreateCameraCalibrator()
{
	int id = GenerateUUID();
	return CreateCameraCalibrator(id);
}

int Manager::CreateCameraCalibrator(int id)
{
	m_Logger->EnterLog("CreateCameraCalibrator called with id=" + std::to_string(id));

	auto p_Calibrator = std::make_shared<CameraCalibrator>(m_Logger, std::to_string(id));

	// CameraCalibrator is both an ISink (consumes calibration frames) and an ISource (can feed calibrated
	// frames onward), so it must be registered in both maps to be reachable from either side
	m_Sinks.emplace(id, p_Calibrator);
	m_Sources.emplace(id, p_Calibrator);

	m_Logger->EnterLog("CameraCalibrator created with id=" + std::to_string(id));
	return id;
}

int Manager::CreateCameraCalibrator(CalibrationBoardType boardType, int rows, int cols,
	float squareSizeMeters, float markerSizeMeters, int arucoDictionaryId)
{
	int id = GenerateUUID();
	return CreateCameraCalibrator(id, boardType, rows, cols, squareSizeMeters, markerSizeMeters, arucoDictionaryId);
}

int Manager::CreateCameraCalibrator(int id, CalibrationBoardType boardType, int rows, int cols,
	float squareSizeMeters, float markerSizeMeters, int arucoDictionaryId)
{
	m_Logger->EnterLog("CreateCameraCalibrator called with id=" + std::to_string(id) + ", boardType=" + std::to_string(boardType));

	CalibrationBoardConfig config;
	config.type = boardType;
	config.rows = rows;
	config.cols = cols;
	config.squareSizeMeters = squareSizeMeters;
	config.markerSizeMeters = markerSizeMeters;
	config.arucoDictionaryId = arucoDictionaryId;

	auto p_Calibrator = std::make_shared<CameraCalibrator>(m_Logger, std::to_string(id), config);
	m_Sinks.emplace(id, p_Calibrator);
	m_Sources.emplace(id, p_Calibrator);
	return id;
}

CameraCalibrationResult Manager::GetCameraCalibrationResult(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	if (p_Calibrator == nullptr) {
		m_Logger->EnterLog("CameraCalibrator not found: " + std::to_string(calibratorId));
		return CameraCalibrationResult();
	}
	return p_Calibrator->GetCalibrationResult();
}

CameraCalibrationResult Manager::RunCameraCalibration(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	if (p_Calibrator == nullptr) {
		throw std::runtime_error("CameraCalibrator not found: " + std::to_string(calibratorId));
	}
	return p_Calibrator->RunCalibration();
}

int Manager::GetCameraCalibrationSnapshotCount(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	return p_Calibrator == nullptr ? 0 : p_Calibrator->GetSnapshotCount();
}

bool Manager::RemoveCameraCalibrationSnapshot(int calibratorId, int index)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	return p_Calibrator != nullptr && p_Calibrator->RemoveSnapshot(index);
}

void Manager::ClearCameraCalibrationSnapshots(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	if (p_Calibrator != nullptr) p_Calibrator->ClearSnapshots();
}

vector<double> Manager::GetCameraCalibrationSnapshotCorners(int calibratorId, int index)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	return p_Calibrator == nullptr ? vector<double>() : p_Calibrator->GetSnapshotCorners(index);
}

int Manager::GetCameraCalibrationFrameWidth(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	return p_Calibrator == nullptr ? 0 : p_Calibrator->GetFrameWidth();
}

int Manager::GetCameraCalibrationFrameHeight(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	return p_Calibrator == nullptr ? 0 : p_Calibrator->GetFrameHeight();
}

bool Manager::SaveCameraCalibrationBoardDetection(int calibratorId)
{
	CameraCalibrator* p_Calibrator = FindCalibrator(m_Sinks, calibratorId);
	if (p_Calibrator == nullptr) {
		m_Logger->EnterLog("CameraCalibrator not found: " + std::to_string(calibratorId));
		return false;
	}
	return p_Calibrator->SaveBoardDetection();
}

int Manager::CreateApriltagDetectorFromCalibrator(int calibratorId, double tagSize /* metres */)
{
	int id = GenerateUUID();
	return CreateApriltagDetectorFromCalibrator(id, calibratorId, tagSize);
}

int Manager::CreateApriltagDetectorFromCalibrator(int id, int calibratorId, double tagSize /* metres */)
{
	m_Logger->EnterLog("CreateApriltagDetectorFromCalibrator called with calibratorId=" + std::to_string(calibratorId));

	// pull the calibration result out of the CameraCalibrator sink and hand it to the new ApriltagDetector
	// so it can resolve the tag's real world location
	CameraCalibrationResult calibrationResult = GetCameraCalibrationResult(calibratorId);

	return CreateApriltagDetector(id, calibrationResult, tagSize);
}

namespace {
    // default board, matching CameraCalibrator's default
    const StereoCalibrationBoardConfig DEFAULT_STEREO_BOARD_CONFIG;

    StereoCalibrator* FindStereoCalibrator(map<int, std::shared_ptr<ISink>>& sinks, int calibratorId)
    {
        auto sink = sinks.find(calibratorId);
        if (sink == sinks.end()) return nullptr;
        return dynamic_cast<StereoCalibrator*>(sink->second.get());
    }

    StereoDepthNode* FindStereoDepthNode(map<int, std::shared_ptr<ISink>>& sinks, int nodeId)
    {
        auto sink = sinks.find(nodeId);
        if (sink == sinks.end()) return nullptr;
        return dynamic_cast<StereoDepthNode*>(sink->second.get());
    }
}

int Manager::CreateStereoCalibrator()
{
    int id = GenerateUUID();
    return CreateStereoCalibrator(id);
}

int Manager::CreateStereoCalibrator(int id)
{
    m_Logger->EnterLog("CreateStereoCalibrator called with id=" + std::to_string(id));

    auto p_Calibrator = std::make_shared<StereoCalibrator>(m_Logger, std::to_string(id));

    // like CameraCalibrator, dual-role: both an ISink (consumes the left/right camera pair) and
    // an ISource (produces the side-by-side rectified preview + status JSON)
    m_Sinks.emplace(id, p_Calibrator);
    m_Sources.emplace(id, p_Calibrator);

    m_Logger->EnterLog("StereoCalibrator created with id=" + std::to_string(id));
    return id;
}

int Manager::CreateStereoCalibrator(CalibrationBoardType boardType, int rows, int cols, float squareSizeMeters)
{
    int id = GenerateUUID();
    return CreateStereoCalibrator(id, boardType, rows, cols, squareSizeMeters);
}

int Manager::CreateStereoCalibrator(int id, CalibrationBoardType boardType, int rows, int cols, float squareSizeMeters)
{
    m_Logger->EnterLog("CreateStereoCalibrator called with id=" + std::to_string(id) + ", boardType=" + std::to_string(boardType));

    StereoCalibrationBoardConfig config;
    config.type = boardType;
    config.rows = rows;
    config.cols = cols;
    config.squareSizeMeters = squareSizeMeters;

    auto p_Calibrator = std::make_shared<StereoCalibrator>(m_Logger, std::to_string(id), config);
    m_Sinks.emplace(id, p_Calibrator);
    m_Sources.emplace(id, p_Calibrator);
    return id;
}

bool Manager::SaveStereoCalibrationDetection(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    if (p_Calibrator == nullptr) {
        m_Logger->EnterLog("StereoCalibrator not found: " + std::to_string(calibratorId));
        return false;
    }
    return p_Calibrator->SaveStereoDetection();
}

int Manager::GetStereoCalibrationPairCount(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    return p_Calibrator == nullptr ? 0 : p_Calibrator->GetPairCount();
}

bool Manager::RemoveStereoCalibrationPair(int calibratorId, int index)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    return p_Calibrator != nullptr && p_Calibrator->RemovePair(index);
}

void Manager::ClearStereoCalibrationPairs(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    if (p_Calibrator != nullptr) p_Calibrator->ClearPairs();
}

vector<double> Manager::GetStereoCalibrationPairCorners(int calibratorId, int index, string eye)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    return p_Calibrator == nullptr ? vector<double>() : p_Calibrator->GetPairCorners(index, eye);
}

int Manager::GetStereoCalibrationFrameWidth(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    return p_Calibrator == nullptr ? 0 : p_Calibrator->GetFrameWidth();
}

int Manager::GetStereoCalibrationFrameHeight(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    return p_Calibrator == nullptr ? 0 : p_Calibrator->GetFrameHeight();
}

StereoCalibrationResult Manager::RunStereoCalibration(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    if (p_Calibrator == nullptr) {
        throw std::runtime_error("StereoCalibrator not found: " + std::to_string(calibratorId));
    }
    return p_Calibrator->RunCalibration();
}

StereoCalibrationResult Manager::GetStereoCalibrationResult(int calibratorId)
{
    StereoCalibrator* p_Calibrator = FindStereoCalibrator(m_Sinks, calibratorId);
    if (p_Calibrator == nullptr) {
        m_Logger->EnterLog("StereoCalibrator not found: " + std::to_string(calibratorId));
        return StereoCalibrationResult();
    }
    return p_Calibrator->GetCalibrationResult();
}

int Manager::CreateStereoDepthNode(StereoDepthBackendKind backend, StereoCalibrationResult calibration,
    double minDepthMeters, double maxDepthMeters, int maxSkewUs, StereoFrameOutput frameOutput)
{
    int id = GenerateUUID();
    return CreateStereoDepthNode(id, backend, calibration, minDepthMeters, maxDepthMeters, maxSkewUs, frameOutput);
}

int Manager::CreateStereoDepthNode(int id, StereoDepthBackendKind backend, StereoCalibrationResult calibration,
    double minDepthMeters, double maxDepthMeters, int maxSkewUs, StereoFrameOutput frameOutput)
{
    m_Logger->EnterLog("CreateStereoDepthNode called with id=" + std::to_string(id) + ", backend=" + std::to_string(backend));

    auto p_Node = std::make_shared<StereoDepthNode>(m_Logger, std::to_string(id), backend, calibration,
        minDepthMeters, maxDepthMeters, maxSkewUs, frameOutput);

    // dual-role: ISink (consumes the left/right pair) and ISource (produces depth JSON + a
    // colormap/rectified frame) - a downstream WebRTCSink or DepthFusionNode binds to this same id
    m_Sinks.emplace(id, p_Node);
    m_Sources.emplace(id, p_Node);

    m_Logger->EnterLog("StereoDepthNode created with id=" + std::to_string(id));
    return id;
}

string Manager::GetStereoDepthBackendName(int sinkId)
{
    StereoDepthNode* p_Node = FindStereoDepthNode(m_Sinks, sinkId);
    return p_Node == nullptr ? "" : p_Node->GetBackendName();
}

double Manager::GetStereoDepthValidFraction(int sinkId)
{
    StereoDepthNode* p_Node = FindStereoDepthNode(m_Sinks, sinkId);
    return p_Node == nullptr ? 0.0 : p_Node->GetLastValidFraction();
}

double Manager::GetStereoDepthMedianDepthMeters(int sinkId)
{
    StereoDepthNode* p_Node = FindStereoDepthNode(m_Sinks, sinkId);
    return p_Node == nullptr ? 0.0 : p_Node->GetLastMedianDepthMeters();
}

int Manager::CreateDepthFusionNode()
{
    int id = GenerateUUID();
    return CreateDepthFusionNode(id);
}

int Manager::CreateDepthFusionNode(int id)
{
    m_Logger->EnterLog("CreateDepthFusionNode called with id=" + std::to_string(id));

    auto p_Node = std::make_shared<DepthFusionNode>(m_Logger, std::to_string(id));

    // dual-role, same as every other detector-shaped node: ISink (consumes the detector's
    // bbox JSON) and ISource (produces the fused distance/bearing JSON + annotated frame)
    m_Sinks.emplace(id, p_Node);
    m_Sources.emplace(id, p_Node);

    m_Logger->EnterLog("DepthFusionNode created with id=" + std::to_string(id));
    return id;
}

bool Manager::SetDepthFusionDepthNode(int fusionSinkId, int depthNodeSourceId)
{
    auto sinkIt = m_Sinks.find(fusionSinkId);
    if (sinkIt == m_Sinks.end()) {
        m_Logger->EnterLog("SetDepthFusionDepthNode: fusion sink not found: " + std::to_string(fusionSinkId));
        return false;
    }
    DepthFusionNode* p_Fusion = dynamic_cast<DepthFusionNode*>(sinkIt->second.get());
    if (p_Fusion == nullptr) {
        m_Logger->EnterLog(LogLevel::Error, "SetDepthFusionDepthNode: sink " + std::to_string(fusionSinkId) + " is not a DepthFusionNode");
        return false;
    }

    auto sourceIt = m_Sources.find(depthNodeSourceId);
    if (sourceIt == m_Sources.end()) {
        m_Logger->EnterLog("SetDepthFusionDepthNode: depth node source not found: " + std::to_string(depthNodeSourceId));
        return false;
    }
    auto p_DepthNode = std::dynamic_pointer_cast<StereoDepthNode>(sourceIt->second);
    if (!p_DepthNode) {
        m_Logger->EnterLog(LogLevel::Error, "SetDepthFusionDepthNode: source " + std::to_string(depthNodeSourceId) + " is not a StereoDepthNode");
        return false;
    }

    p_Fusion->SetStereoDepthNode(p_DepthNode);
    return true;
}

int Manager::CreateObjectDetectionSink(ObjectDetectionProvider provider)
{
    throw std::runtime_error("CreateObjectDetectionSink requires a model - use the overload that takes modelPath/labelsPath/variant/thresholds");
}

int Manager::CreateObjectDetectionSink(ObjectDetectionProvider provider, int id)
{
    throw std::runtime_error("CreateObjectDetectionSink requires a model - use the overload that takes modelPath/labelsPath/variant/thresholds");
}

int Manager::CreateObjectDetectionSink(ObjectDetectionProvider provider, string modelPath, string labelsPath,
    YoloVariant variant, float confThreshold, float nmsThreshold, int inputSize)
{
    int id = GenerateUUID();
    return CreateObjectDetectionSink(id, provider, modelPath, labelsPath, variant, confThreshold, nmsThreshold, inputSize);
}

int Manager::CreateObjectDetectionSink(int id, ObjectDetectionProvider provider, string modelPath, string labelsPath,
    YoloVariant variant, float confThreshold, float nmsThreshold, int inputSize)
{
    m_Logger->EnterLog("CreateObjectDetectionSink called with id=" + std::to_string(id) + ", modelPath=" + modelPath);

    DetectionBackendConfig config;
    config.modelPath = modelPath;
    config.labelsPath = labelsPath;
    config.variant = variant;
    config.confThreshold = confThreshold;
    config.nmsThreshold = nmsThreshold;
    config.inputWidth = inputSize;
    config.inputHeight = inputSize;

    std::shared_ptr<IDetectionBackend> backend;
    switch (provider) {
#ifdef LUMEN_WITH_ONNX
        case ONNX:
            backend = std::make_shared<OnnxDetectionBackend>();
            break;
#endif
#ifdef LUMEN_WITH_RKNN
        case RKNN:
            backend = std::make_shared<RknnDetectionBackend>();
            break;
#else
        case RKNN:
            throw std::runtime_error("RKNN object detection backend is not compiled into this build (LUMEN_WITH_RKNN is off)");
#endif
        default:
            throw std::runtime_error("unknown ObjectDetectionProvider");
    }

    if (!backend || !backend->Load(config)) {
        throw std::runtime_error("failed to load detection model: " + modelPath);
    }

    auto p_Sink = std::make_shared<ObjectDetectionSink>(m_Logger, std::to_string(id), backend);

    // ObjectDetectionSink is both an ISink (consumes camera frames) and an ISource (produces
    // detections), so it must be registered in both maps to be reachable from either side
    m_Sinks.emplace(id, p_Sink);
    m_Sources.emplace(id, p_Sink);

    m_Logger->EnterLog("ObjectDetectionSink created with id=" + std::to_string(id) + " using backend=" + backend->Name());
    return id;
}

std::string Manager::GetObjectDetectionSinkBackendName(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return "none";

    ObjectDetectionSink* p_Sink = dynamic_cast<ObjectDetectionSink*>(sink->second.get());
    if (p_Sink == nullptr) return "none";

    return p_Sink->GetBackendName();
}

// Bodies only (see Manager.h for why these are declared unconditionally). Every symbol is defined in both #ifdef branches so linking and
// SWIG wrapping always find it: Create throws a catchable error (swig.i's %exception turns it into an ApplicationException) when NT4 is
// compiled out, and query methods return the same "nothing here" answer as for a missing sink id.
#ifdef LUMEN_WITH_NT4
int Manager::CreateNetworkTablesSinkForTeam(int teamNumber, string rootTable, string clientIdentity)
{
    int id = GenerateUUID();
    return CreateNetworkTablesSinkForTeam(id, teamNumber, rootTable, clientIdentity);
}

int Manager::CreateNetworkTablesSinkForTeam(int id, int teamNumber, string rootTable, string clientIdentity)
{
    NetworkTablesConfig config;
    config.teamNumber = static_cast<unsigned int>(teamNumber);
    config.rootTable = rootTable;
    config.clientIdentity = clientIdentity;

    m_Logger->EnterLog("CreateNetworkTablesSinkForTeam called with id=" + std::to_string(id) + ", team=" + std::to_string(teamNumber));

    auto p_Sink = std::make_shared<NetworkTablesSink>(m_Logger, std::to_string(id), config, m_SystemMonitor);

    // terminal sink: it has no ISource half, so it only ever goes into m_Sinks
    m_Sinks.emplace(id, p_Sink);
    return id;
}

int Manager::CreateNetworkTablesSinkForServer(string serverAddress, int port, string rootTable, string clientIdentity)
{
    int id = GenerateUUID();
    return CreateNetworkTablesSinkForServer(id, serverAddress, port, rootTable, clientIdentity);
}

int Manager::CreateNetworkTablesSinkForServer(int id, string serverAddress, int port, string rootTable, string clientIdentity)
{
    NetworkTablesConfig config;
    config.serverAddress = serverAddress;
    config.port = static_cast<unsigned int>(port);
    config.rootTable = rootTable;
    config.clientIdentity = clientIdentity;

    m_Logger->EnterLog("CreateNetworkTablesSinkForServer called with id=" + std::to_string(id) + ", server=" + serverAddress);

    auto p_Sink = std::make_shared<NetworkTablesSink>(m_Logger, std::to_string(id), config, m_SystemMonitor);
    m_Sinks.emplace(id, p_Sink);
    return id;
}

bool Manager::IsNetworkTablesSinkConnected(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return false;

    NetworkTablesSink* p_NtSink = dynamic_cast<NetworkTablesSink*>(sink->second.get());
    return p_NtSink != nullptr && p_NtSink->IsConnected();
}

string Manager::GetNetworkTablesSinkStatus(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return "{}";
    }

    NetworkTablesSink* p_NtSink = dynamic_cast<NetworkTablesSink*>(sink->second.get());
    if (p_NtSink == nullptr) {
        m_Logger->EnterLog("Sink " + std::to_string(sinkId) + " is not a NetworkTablesSink");
        return "{}";
    }

    return p_NtSink->GetConnectionStatus();
}

string Manager::PollNetworkTablesSinkConfigRequests(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return "[]";
    }

    NetworkTablesSink* p_NtSink = dynamic_cast<NetworkTablesSink*>(sink->second.get());
    if (p_NtSink == nullptr) {
        m_Logger->EnterLog("Sink " + std::to_string(sinkId) + " is not a NetworkTablesSink");
        return "[]";
    }

    return p_NtSink->PollConfigRequests();
}

int Manager::PollNetworkTablesSinkRecordingRequest(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return -1;
    NetworkTablesSink* p_NtSink = dynamic_cast<NetworkTablesSink*>(sink->second.get());
    return p_NtSink ? p_NtSink->PollRecordingRequest() : -1;
}

void Manager::SetNetworkTablesSinkRecordingStatus(int sinkId, bool recording)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return;
    if (NetworkTablesSink* p_NtSink = dynamic_cast<NetworkTablesSink*>(sink->second.get()))
        p_NtSink->SetRecordingStatus(recording);
}
#else
int Manager::CreateNetworkTablesSinkForTeam(int, string, string)
{
    throw std::runtime_error("NetworkTables (NT4) support is not compiled into this build of LumenCore");
}
int Manager::CreateNetworkTablesSinkForTeam(int, int, string, string)
{
    throw std::runtime_error("NetworkTables (NT4) support is not compiled into this build of LumenCore");
}
int Manager::CreateNetworkTablesSinkForServer(string, int, string, string)
{
    throw std::runtime_error("NetworkTables (NT4) support is not compiled into this build of LumenCore");
}
int Manager::CreateNetworkTablesSinkForServer(int, string, int, string, string)
{
    throw std::runtime_error("NetworkTables (NT4) support is not compiled into this build of LumenCore");
}
bool Manager::IsNetworkTablesSinkConnected(int) { return false; }
string Manager::GetNetworkTablesSinkStatus(int) { return "{}"; }
string Manager::PollNetworkTablesSinkConfigRequests(int) { return "[]"; }
int Manager::PollNetworkTablesSinkRecordingRequest(int) { return -1; }
void Manager::SetNetworkTablesSinkRecordingStatus(int, bool) {}
#endif

// Declared unconditionally with an #ifdef'd body, as in the NT4 block above.
#ifdef LUMEN_WITH_WEBRTC
int Manager::CreateWebRTCSink(int bitrateKbps, int fps, string encoderName)
{
    int id = GenerateUUID();
    return CreateWebRTCSink(id, bitrateKbps, fps, encoderName);
}

int Manager::CreateWebRTCSink(int id, int bitrateKbps, int fps, string encoderName)
{
    WebRTCSinkConfig config;
    config.bitrateKbps = bitrateKbps;
    config.fps = fps;
    config.encoderName = encoderName;

    m_Logger->EnterLog("CreateWebRTCSink called with id=" + std::to_string(id) + ", encoder=" + encoderName);

    auto p_Sink = std::make_shared<WebRTCSink>(m_Logger, std::to_string(id), config);
    m_Sinks.emplace(id, p_Sink);
    return id;
}

namespace {
    // shared by all WebRTC accessor methods below - a WebRTCSink lookup + cast is used
    // repeatedly, and every failure mode should behave the same way
    std::shared_ptr<WebRTCSink> FindWebRTCSink(map<int, std::shared_ptr<ISink>>& sinks, int sinkId)
    {
        auto sink = sinks.find(sinkId);
        if (sink == sinks.end()) return nullptr;
        return std::dynamic_pointer_cast<WebRTCSink>(sink->second);
    }
}

string Manager::WebRTCCreateOffer(int sinkId)
{
    auto sink = FindWebRTCSink(m_Sinks, sinkId);
    if (!sink) throw std::runtime_error("no WebRTCSink with id " + std::to_string(sinkId));
    return sink->CreateOffer();
}

void Manager::WebRTCSetAnswer(int sinkId, string sdp)
{
    auto sink = FindWebRTCSink(m_Sinks, sinkId);
    if (!sink) throw std::runtime_error("no WebRTCSink with id " + std::to_string(sinkId));
    sink->SetAnswer(sdp);
}

void Manager::WebRTCAddIceCandidate(int sinkId, string candidate, string mid)
{
    auto sink = FindWebRTCSink(m_Sinks, sinkId);
    if (!sink) throw std::runtime_error("no WebRTCSink with id " + std::to_string(sinkId));
    sink->AddIceCandidate(candidate, mid);
}

bool Manager::IsWebRTCSinkConnected(int sinkId)
{
    auto sink = FindWebRTCSink(m_Sinks, sinkId);
    return sink != nullptr && sink->IsConnected();
}

string Manager::GetWebRTCSinkStatus(int sinkId)
{
    auto sink = FindWebRTCSink(m_Sinks, sinkId);
    return sink ? sink->GetConnectionStatus() : "{}";
}

string Manager::GetPreferredWebRTCEncoder()
{
    // a runtime probe: upstream FFmpeg's --enable-rkmpp is decode-only, so the h264_rkmpp encoder exists only in the
    // nyanmisaka/ffmpeg-rockchip fork.
    if (avcodec_find_encoder_by_name("h264_rkmpp")) return "h264_rkmpp";
    return "libx264";
}
#else
int Manager::CreateWebRTCSink(int, int, string)
{
    throw std::runtime_error("WebRTC support is not compiled into this build of LumenCore");
}
int Manager::CreateWebRTCSink(int, int, int, string)
{
    throw std::runtime_error("WebRTC support is not compiled into this build of LumenCore");
}
string Manager::WebRTCCreateOffer(int)
{
    throw std::runtime_error("WebRTC support is not compiled into this build of LumenCore");
}
void Manager::WebRTCSetAnswer(int, string)
{
    throw std::runtime_error("WebRTC support is not compiled into this build of LumenCore");
}
void Manager::WebRTCAddIceCandidate(int, string, string)
{
    throw std::runtime_error("WebRTC support is not compiled into this build of LumenCore");
}
bool Manager::IsWebRTCSinkConnected(int) { return false; }
string Manager::GetWebRTCSinkStatus(int) { return "{}"; }
string Manager::GetPreferredWebRTCEncoder() { return "libx264"; }
#endif

// Always available (pure OpenCV); no LUMEN_WITH_* guard (see MjpegSink.h).
int Manager::CreateMjpegSink(int jpegQuality)
{
    int id = GenerateUUID();
    return CreateMjpegSink(id, jpegQuality);
}

int Manager::CreateMjpegSink(int id, int jpegQuality)
{
    m_Logger->EnterLog("CreateMjpegSink called with id=" + std::to_string(id) + ", jpegQuality=" + std::to_string(jpegQuality));

    auto p_Sink = std::make_shared<MjpegSink>(m_Logger, std::to_string(id), jpegQuality);
    m_Sinks.emplace(id, p_Sink);
    return id;
}

string Manager::GetMjpegFrameBase64(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return "";

    auto mjpegSink = std::dynamic_pointer_cast<MjpegSink>(sink->second);
    return mjpegSink ? mjpegSink->GetLatestJpegBase64() : "";
}

// Declared unconditionally with an #ifdef'd body, as in the NT4/WebRTC blocks above.
#ifdef LUMEN_WITH_RECORD
int Manager::CreateRecordSink(string dstFolder, string encoderName, int bitrateKbps, int fps, int segmentSeconds, int64_t maxFolderSizeBytes, int maxFileCount)
{
    int id = GenerateUUID();
    return CreateRecordSink(id, dstFolder, encoderName, bitrateKbps, fps, segmentSeconds, maxFolderSizeBytes, maxFileCount);
}

int Manager::CreateRecordSink(int id, string dstFolder, string encoderName, int bitrateKbps, int fps, int segmentSeconds, int64_t maxFolderSizeBytes, int maxFileCount)
{
    RecordSinkConfig config;
    config.dstFolder = dstFolder;
    config.encoderName = encoderName;
    config.bitrateKbps = bitrateKbps;
    config.fps = fps;
    config.segmentSeconds = segmentSeconds;
    config.maxFolderSizeBytes = maxFolderSizeBytes;
    config.maxFileCount = maxFileCount;

    m_Logger->EnterLog("CreateRecordSink called with id=" + std::to_string(id) + ", dstFolder=" + dstFolder + ", encoder=" + encoderName);

    auto p_Sink = std::make_shared<RecordSink>(m_Logger, std::to_string(id), config);
    m_Sinks.emplace(id, p_Sink);
    return id;
}

vector<string> Manager::GetRecordSinkSegments(int sinkId)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return {};

    auto recordSink = std::dynamic_pointer_cast<RecordSink>(sink->second);
    return recordSink ? recordSink->ListSegments() : vector<string>();
}

bool Manager::DeleteRecordSinkSegment(int sinkId, string filename)
{
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) return false;

    auto recordSink = std::dynamic_pointer_cast<RecordSink>(sink->second);
    return recordSink && recordSink->DeleteSegment(filename);
}
#else
int Manager::CreateRecordSink(string, string, int, int, int, int64_t, int)
{
    throw std::runtime_error("Recording support is not compiled into this build of LumenCore");
}
int Manager::CreateRecordSink(int, string, string, int, int, int, int64_t, int)
{
    throw std::runtime_error("Recording support is not compiled into this build of LumenCore");
}
vector<string> Manager::GetRecordSinkSegments(int) { return {}; }
bool Manager::DeleteRecordSinkSegment(int, string) { return false; }
#endif

void Manager::StartAllSources()
{
    auto iterator = m_Sources.begin();

    while (iterator != m_Sources.end()) {
        iterator->second->Toggle(true);
        iterator++;
    }
}

void Manager::StopAllSources()
{
    auto iterator = m_Sources.begin();

    while (iterator != m_Sources.end()) {
        iterator->second->Toggle(false);
        iterator++;
    }
}

bool Manager::StopSourceById(int sourceId)
{
    auto source = m_Sources.find(sourceId);
    if (source == m_Sources.end()) {
        return false;
    }
    source->second->Toggle(false);
    return true;
}

bool Manager::StartSourceById(int sourceId)
{
    auto source = m_Sources.find(sourceId);
    if (source == m_Sources.end()) {
        return false;
    }
    source->second->Toggle(true);
    return true;
}

bool Manager::IsSourceActive(int sourceId)
{
	auto source = m_Sources.find(sourceId);
    if (source == m_Sources.end()) {
        return false;
    }
    return source->second->GetToggleStatus();
}

void Manager::StartAllSinks() {
    if (!m_Sinks.empty()) {
        auto iterator = m_Sinks.begin();

        while (iterator != m_Sinks.end()) {
            iterator->second->Toggle(true);
            iterator++;
        }
    }
}

void Manager::StopAllSinks() {
    auto iterator = m_Sinks.begin();

    while (iterator != m_Sinks.end()) {
        iterator->second->Toggle(false);
        iterator++;
    }
}

bool Manager::StopSinkById(int sinkId) {
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        return false;
    }
    sink->second->Toggle(false);
    return true;
}

bool Manager::IsSinkActive(int sinkId)
{
	auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
		return false;
	}
    return sink->second->GetToggleStatus();
}

bool Manager::StartSinkById(int sinkId) {
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        return false;
    }
    sink->second->Toggle(true);
    return true;
}

string Manager::GetSinkResult(int sinkId)
{
    m_Logger->EnterLog("GetSinkResult called with sinkId=" + std::to_string(sinkId));
    auto sink = m_Sinks.find(sinkId);
    if (sink == m_Sinks.end()) {
        m_Logger->EnterLog("Sink not found: " + std::to_string(sinkId));
        return "{}";
    }

    // a sink's result is only meaningful when it is also a source (ApriltagDetector,
    // CameraCalibrator, ObjectDetectionSink); terminal sinks (NetworkTablesSink, WebRTCSink)
    // consume results but don't produce any of their own
    ISource* p_AsSource = dynamic_cast<ISource*>(sink->second.get());
    if (p_AsSource == nullptr) {
        return "{}";
    }

    SourceResult result = p_AsSource->GetLatestResult();
    if (!result.json.has_value()) {
        return "{}";
    }

    return result.json.value().dump();
}

string Manager::GetAllSinkResults()
{
    m_Logger->EnterLog("GetAllSinkResults called");
    nlohmann::json allResults = nlohmann::json::object();

    for (auto& sinkPair : m_Sinks) {
        ISource* p_AsSource = dynamic_cast<ISource*>(sinkPair.second.get());
        if (p_AsSource == nullptr) {
            continue;
        }

        SourceResult result = p_AsSource->GetLatestResult();
        if (result.json.has_value()) {
            allResults[std::to_string(sinkPair.first)] = result.json.value();
        }
    }

    return allResults.dump();
}

//bool Manager::SetSinkResult(int sinkId, string result)
//{
//    m_Logger->EnterLog("SetSinkResult called with sinkId=" + std::to_string(sinkId) + ", result=" + result);
//    if (m_Sinks.find(sinkId) == m_Sinks.end()) {
//        m_Logger->EnterLog("Result entry not found for sinkId: " + std::to_string(sinkId));
//        return false;
//    }
//    else {
//        // TODO: fix
//        m_Sinks.find(sinkId)->second->() = result;
//    }
//        m_Logger->EnterLog("Result set for sinkId: " + std::to_string(sinkId));
//    return true;
//}

int Manager::GenerateUUID()
{
    m_Logger->EnterLog("GenerateUUID called");
    std::mt19937 engine(std::chrono::high_resolution_clock::now().time_since_epoch().count());

    std::uniform_int_distribution<int> dist(0, 2147483647);

    int randomNumber = dist(engine);
    m_Logger->EnterLog("Generated UUID: " + std::to_string(randomNumber));
    return randomNumber;
}

//int Manager::CreateCameraCalibrationSink(int width, int height)
//{
//	int id = GenerateUUID();
//
//    // TODO: fix
//    auto p_Sink = std::make_shared<CameraCalibrationSink>(m_Logger, nullptr, FrameSpec(height, width, CV_8UC3));
//
//	m_CameraCalibrationSinks.emplace(id, p_Sink);
//
//
//    return id;
//}

//void Manager::BindSourceToCalibrationSink(int sourceId)
//{
//	auto sink = m_CameraCalibrationSinks.find(sourceId);
//    if (sink != m_CameraCalibrationSinks.end() && m_Sources.find(sourceId) != m_Sources.end()) {
//        sink->second->BindSource(m_Sources.find(sourceId)->second.get());
//    }
//}
//
//void Manager::CameraCalibrationSinkGrabFrame(int sinkId)
//{
//    auto sink = m_CameraCalibrationSinks.find(sinkId);
//    if (sink != m_CameraCalibrationSinks.end()) {
//        sink->second->GrabAndProcessFrame();
//    } else {
//        m_Logger->EnterLog("CameraCalibrationSink not found with id: " + std::to_string(sinkId));
//	}
//}
//
//CameraCalibrationResult Manager::GetCameraCalibrationResults(int sinkId)
//{
//	auto sink = m_CameraCalibrationSinks.find(sinkId);
//    if (sink != m_CameraCalibrationSinks.end()) {
//		return sink->second->GetResults();
//    }
//    // Return empty result if not found
//    return CameraCalibrationResult();
//}

int Manager::GetMemoryUsageBytes()
{
    return m_SystemMonitor->GetRAMUsage();
}

/*
	returns the CPU usage in percents
*/
int Manager::GetCPUUsage()
{
    return m_SystemMonitor->GetCPUUsage();
}

/*
	returns the CPU temperature in degrees Celsius 
*/
int Manager::GetCpuTemperature()
{
    return m_SystemMonitor->GetCPUTemperature();
}

/*
   returns the disk usage in percents
*/
int Manager::GetDiskUsage()
{
    return m_SystemMonitor->GetDiskUsage();
}

uint64_t Manager::GetFrameCount(int id)
{
    auto sourceIt = m_Sources.find(id);
    return sourceIt == m_Sources.end() ? 0 : sourceIt->second->GetCurrentFrameCount();
}

int64_t Manager::GetLatencyUs(int id)
{
    auto sourceIt = m_Sources.find(id);
    if (sourceIt == m_Sources.end()) return 0;

    SourceResult result = sourceIt->second->GetLatestResult();
    return static_cast<int64_t>(result.producedTimeUs) - static_cast<int64_t>(result.captureTimeUs);
}

// Every LUMEN_WITH_* backend this .so/.dll was built with, from a literal list because it must reflect what THIS translation unit
// was compiled with (the runtime counterpart to the always-present SWIG surface).
vector<string> Manager::GetEnabledFeatures()
{
    vector<string> features;
#ifdef LUMEN_WITH_ONNX
    features.push_back("ONNX");
#endif
#ifdef LUMEN_WITH_NT4
    features.push_back("NT4");
#endif
#ifdef LUMEN_WITH_WEBRTC
    features.push_back("WEBRTC");
#endif
#ifdef LUMEN_WITH_VULKAN_APRILTAG
    features.push_back("VULKAN_APRILTAG");
#endif
#ifdef LUMEN_WITH_CODEC_STEREO
    features.push_back("CODEC_STEREO");
#endif
#ifdef LUMEN_WITH_MPP_JPEG
    features.push_back("MPP_JPEG");
#endif
#ifdef LUMEN_WITH_RKNN
    features.push_back("RKNN");
#endif
    return features;
}

