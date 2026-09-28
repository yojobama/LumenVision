#ifdef _WIN32

#include "WindowsCameraEnumerator.h"
#include "Logger.h"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

namespace {
    // UVC webcams natively report NV12/YUY2/MJPG (as in V4l2CameraBackend's FourCcToFrameFormat); RGB24/RGB32 are included
    // since some drivers report them. Microsoft's "RGB24" subtype is BGR byte order, so BGR24 is the correct mapping.
    FrameFormat MfSubtypeToFrameFormat(const GUID& subtype)
    {
        if (subtype == MFVideoFormat_NV12) return FrameFormat::NV12;
        if (subtype == MFVideoFormat_YUY2) return FrameFormat::YUYV;
        if (subtype == MFVideoFormat_MJPG) return FrameFormat::MJPEG;
        if (subtype == MFVideoFormat_RGB24) return FrameFormat::BGR24;
        if (subtype == MFVideoFormat_RGB32) return FrameFormat::BGR24; // closest 3-channel match; alpha/pad byte dropped
        // MJPEG as the default for a compressed/unrecognised format, matching FourCcToFrameFormat on Linux (not a claim the subtype is MJPEG)
        return FrameFormat::MJPEG;
    }
}

std::vector<WindowsCameraDevice> EnumerateWindowsCameras(const std::shared_ptr<Logger>& logger)
{
    std::vector<WindowsCameraDevice> cameras;

    // MFSTARTUP_LITE skips the platform pipeline, which enumerating device sources does not need, and starts faster
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        if (logger) logger->EnterLog(LogLevel::Error, "EnumerateWindowsCameras: MFStartup failed, hr=" + std::to_string(hr));
        return cameras;
    }

    IMFAttributes* pAttributes = nullptr;
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;

    hr = MFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr)) {
        hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    if (SUCCEEDED(hr)) {
        hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
    }

    if (SUCCEEDED(hr)) {
        for (UINT32 i = 0; i < count; i++) {
            WCHAR* friendlyName = nullptr;
            UINT32 nameLength = 0;
            HRESULT nameHr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &nameLength);

            std::string name;
            if (SUCCEEDED(nameHr) && friendlyName != nullptr) {
                int utf8Length = WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, nullptr, 0, nullptr, nullptr);
                if (utf8Length > 0) {
                    name.resize(static_cast<size_t>(utf8Length) - 1);
                    WideCharToMultiByte(CP_UTF8, 0, friendlyName, -1, name.data(), utf8Length, nullptr, nullptr);
                }
                CoTaskMemFree(friendlyName);
            }
            if (name.empty()) {
                name = "Camera " + std::to_string(i);
            }

            cameras.push_back(WindowsCameraDevice{ name, static_cast<int>(i) });
            if (logger) logger->EnterLog("EnumerateWindowsCameras: found \"" + name + "\" at index " + std::to_string(i));

            ppDevices[i]->Release();
        }
        CoTaskMemFree(ppDevices);
    } else {
        if (logger) logger->EnterLog(LogLevel::Error, "EnumerateWindowsCameras: MFEnumDeviceSources failed, hr=" + std::to_string(hr));
    }

    if (pAttributes != nullptr) pAttributes->Release();
    MFShutdown();

    return cameras;
}

std::vector<CameraMode> EnumerateWindowsCameraModes(int deviceIndex, const std::shared_ptr<Logger>& logger)
{
    std::vector<CameraMode> modes;

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) {
        if (logger) logger->EnterLog(LogLevel::Error, "EnumerateWindowsCameraModes: MFStartup failed, hr=" + std::to_string(hr));
        return modes;
    }

    IMFAttributes* pAttributes = nullptr;
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;

    hr = MFCreateAttributes(&pAttributes, 1);
    if (SUCCEEDED(hr)) {
        hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    }
    if (SUCCEEDED(hr)) {
        // re-enumerated rather than cached: deviceIndex is only meaningful as this call's array position (as in OpenCvCameraBackend::Open)
        hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);
    }

    if (SUCCEEDED(hr) && deviceIndex >= 0 && static_cast<UINT32>(deviceIndex) < count) {
        IMFMediaSource* pSource = nullptr;
        hr = ppDevices[deviceIndex]->ActivateObject(IID_PPV_ARGS(&pSource));

        IMFSourceReader* pReader = nullptr;
        if (SUCCEEDED(hr)) {
            hr = MFCreateSourceReaderFromMediaSource(pSource, nullptr, &pReader);
        }

        if (SUCCEEDED(hr)) {
            for (DWORD typeIndex = 0; ; typeIndex++) {
                IMFMediaType* pType = nullptr;
                HRESULT typeHr = pReader->GetNativeMediaType(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), typeIndex, &pType);
                if (typeHr == MF_E_NO_MORE_TYPES || FAILED(typeHr)) break;

                UINT32 width = 0, height = 0;
                MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
                UINT32 fpsNum = 0, fpsDen = 0;
                MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen);
                GUID subtype = GUID_NULL;
                pType->GetGUID(MF_MT_SUBTYPE, &subtype);

                CameraMode mode;
                mode.width = static_cast<int>(width);
                mode.height = static_cast<int>(height);
                mode.fps = fpsDen > 0 ? static_cast<double>(fpsNum) / fpsDen : 0.0;
                mode.pixelFormat = MfSubtypeToFrameFormat(subtype);
                modes.push_back(mode);

                pType->Release();
            }
        } else if (logger) {
            logger->EnterLog(LogLevel::Error, "EnumerateWindowsCameraModes: failed to open a source reader for index " + std::to_string(deviceIndex) + ", hr=" + std::to_string(hr));
        }

        if (pReader != nullptr) pReader->Release();
        if (pSource != nullptr) pSource->Release();
    } else if (FAILED(hr) && logger) {
        logger->EnterLog(LogLevel::Error, "EnumerateWindowsCameraModes: MFEnumDeviceSources failed, hr=" + std::to_string(hr));
    }

    if (ppDevices != nullptr) {
        for (UINT32 i = 0; i < count; i++) ppDevices[i]->Release();
        CoTaskMemFree(ppDevices);
    }
    if (pAttributes != nullptr) pAttributes->Release();
    MFShutdown();

    return modes;
}

#endif // _WIN32
