%module LumenCore
%{
#include "Manager.h"
#include "CameraCalibrationResult.h"
#include "IDetectionBackend.h"
#include "IApriltagBackend.h"
#include "CalibrationBoardType.h"
#include "StereoCalibrationResult.h"
#include "StereoDepthBackendKind.h"
#include "StereoFrameOutput.h"

// On Windows, Manager.h transitively pulls in <windows.h> via NetworkTablesSink.h's WPILib
// (wpinet) headers, which #defines GetMessage as a macro (GetMessageA/W) - textually rewriting
// every later reference to Log::GetMessage() in SWIG's own generated code below this block
// (error C2039 "'GetMessageA' is not a member of 'Log'": the *declaration* in Logger.h parsed
// fine, since it came before this point, but every *call site* SWIG emits afterward did not).
// NOMINMAX/WIN32_LEAN_AND_MEAN (see LumenCore/CMakeLists.txt) don't cover this - GetMessage
// lives in winuser.h, which stays included either way. Undoing it here, after every header this
// block includes has had its chance to define it, is the one point guaranteed to run before any
// of SWIG's own generated code does.
#ifdef GetMessage
#undef GetMessage
#endif
%}

%include "std_string.i"
%include "std_vector.i"
%include "std_unique_ptr.i"
%include "exception.i"
// ROADMAP.md Phase 8a: GetFrameCount/GetLatencyUs are the first uint64_t/int64_t return types
// SWIG has ever had to bind in this project - without this, SWIG has no typemap for either and
// silently falls back to generating an opaque SWIGTYPE_p_uint64_t/SWIGTYPE_p_int64_t pointer
// wrapper instead of a real ulong/long (confirmed the hard way: it compiles fine on both sides,
// so nothing catches it short of actually looking at the generated C#). stdint.i maps them to
// C#'s ulong/long properly.
%include "stdint.i"

// Without this, ANY C++ exception thrown by application code (a bad model file, a missing
// sink id, an unimplemented backend, ...) crosses the P/Invoke boundary completely uncaught and
// calls std::terminate(), killing the entire server process - not just the one request that
// triggered it. Confirmed three separate times this session (WebRTCSink::SetAnswer,
// WebRTCSink::AddIceCandidate, and Manager::CreateObjectDetectionSink on an invalid model file -
// each is a completely ordinary, expected failure mode, not a bug, yet each took the whole
// process down). SWIG's C# exception marshaling machinery (SWIGExceptionHelper /
// SWIGRegisterExceptionCallbacks, visible in every generated stack trace) already exists in the
// generated wrapper, but does nothing unless a wrapped call actually goes through SWIG_exception
// - which requires a %exception block. There were none anywhere in this file. This one applies
// globally to every function/method %include'd below, converting any std::exception (or truly
// unknown exception) into a real, catchable System.ApplicationException on the C# side instead.
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "unknown C++ exception");
    }
}

// Every plain-enum header Manager.h merely #includes must ALSO be %include'd explicitly here
// for SWIG to see it as a real enum rather than falling back to an opaque SWIGTYPE_p_* handle -
// confirmed three times now (YoloVariant, ApriltagBackendKind, CalibrationBoardType): relying
// on Manager.h's own #include is not enough, even though SWIG's preprocessor otherwise follows
// #include chains fine (e.g. through ISink.h -> SourceResult.h -> <opencv2/opencv.hpp>).
%include "IDetectionBackend.h"
%include "IApriltagBackend.h"
%include "CalibrationBoardType.h"
%include "StereoDepthBackendKind.h"
%include "StereoFrameOutput.h"
%include "FrameFormat.h"
%include "CameraMode.h"
%include "FrameTransform.h"
%include "Manager.h"
%include "CameraCalibrationResult.h"
%include "StereoCalibrationResult.h"
%include "Logger.h"

namespace std {
    %template(VectorInt) vector<int>;
    %template(VectorDouble) vector<double>;
    %template(VectorString) vector<string>;
    %template(VectorLog) vector<Log>;
    %template(VectorCameraHardwareInfo) vector<CameraHardwareInfo>;
    %template(VectorCameraMode) vector<CameraMode>;
    %template(VectorCameraControlInfo) vector<CameraControlInfo>;
    %template(UniquePtrLog) unique_ptr<Log>;
}
