#pragma once
#ifdef _WIN32

#include <memory>
#include <string>
#include <vector>

#include "CameraMode.h"

class Logger;

// Separate from Manager.cpp because Manager.h has `using namespace std;`, which makes the Windows SDK headers' unqualified
// `byte` ambiguous with std::byte (C2872) once mfapi.h is included; this translation unit has no using-directive.
struct WindowsCameraDevice {
    std::string name;
    int index; // MFEnumDeviceSources' array position
};

// Enumerates video capture devices via Media Foundation's MFEnumDeviceSources (as the Windows Camera app does).
// Logger is optional (nullable) so this can be unit-tested.
std::vector<WindowsCameraDevice> EnumerateWindowsCameras(const std::shared_ptr<Logger>& logger);

// Enumerates the native capture modes (resolution/fps/pixel format) of MFEnumDeviceSources' deviceIndex'th device via
// IMFSourceReader's GetNativeMediaType (the Windows equivalent of V4l2CameraBackend's VIDIOC_ENUM_FRAMESIZES walk).
std::vector<CameraMode> EnumerateWindowsCameraModes(int deviceIndex, const std::shared_ptr<Logger>& logger);

#endif // _WIN32
