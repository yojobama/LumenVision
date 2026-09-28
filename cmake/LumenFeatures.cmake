#[[
  Single source of truth for LumenCore's optional-backend feature flags.

  Two lists come out of this file:
  - LUMEN_ENABLED_DEFINES: only the flags that are ON; passed to the C++ compiler.
  - LUMEN_SWIG_DEFINES: always the full flag list; passed to swig so the generated C# API is
    identical across presets. Disabled features are handled at runtime (declarations always
    exist, bodies throw; see Manager::GetEnabledFeatures()).
]]

set(_LUMEN_FEATURE_NAMES
    ONNX
    NT4
    WEBRTC
    RECORD
    VULKAN_APRILTAG
    CODEC_STEREO
    RKNN
    RGA
)

set(_LUMEN_FEATURE_ONNX_DESC            "ONNX Runtime object detection backend")
set(_LUMEN_FEATURE_NT4_DESC             "NetworkTables 4 publishing sink")
set(_LUMEN_FEATURE_WEBRTC_DESC          "WebRTC live-view sink")
set(_LUMEN_FEATURE_RECORD_DESC          "RecordSink - segmented MP4 recording with a JSON-Lines telemetry sidecar")
set(_LUMEN_FEATURE_VULKAN_APRILTAG_DESC "Vulkan compute AprilTag backend")
set(_LUMEN_FEATURE_CODEC_STEREO_DESC    "codec-stereo hardware-motion-vector depth backend")
set(_LUMEN_FEATURE_RKNN_DESC            "Rockchip NPU object detection backend (aarch64 only)")
set(_LUMEN_FEATURE_RGA_DESC              "Rockchip RGA hardware BGR->NV12 conversion for WebRTCSink (aarch64 only)")

# RKNN and RGA default OFF because they are Rockchip-only
set(_LUMEN_FEATURE_ONNX_DEFAULT            ON)
set(_LUMEN_FEATURE_NT4_DEFAULT             ON)
set(_LUMEN_FEATURE_WEBRTC_DEFAULT          ON)
set(_LUMEN_FEATURE_RECORD_DEFAULT          ON)
set(_LUMEN_FEATURE_VULKAN_APRILTAG_DEFAULT ON)
set(_LUMEN_FEATURE_CODEC_STEREO_DEFAULT    ON)
set(_LUMEN_FEATURE_RKNN_DEFAULT            OFF)
set(_LUMEN_FEATURE_RGA_DEFAULT              OFF)

set(LUMEN_SWIG_DEFINES "")
set(LUMEN_ENABLED_DEFINES "")
set(LUMEN_ENABLED_FEATURES "")

foreach(_name ${_LUMEN_FEATURE_NAMES})
    option(LUMEN_WITH_${_name} "${_LUMEN_FEATURE_${_name}_DESC}" ${_LUMEN_FEATURE_${_name}_DEFAULT})
    list(APPEND LUMEN_SWIG_DEFINES "LUMEN_WITH_${_name}")
    if(LUMEN_WITH_${_name})
        list(APPEND LUMEN_ENABLED_DEFINES "LUMEN_WITH_${_name}")
        list(APPEND LUMEN_ENABLED_FEATURES "${_name}")
    endif()
endforeach()

# RKNN and RGA exist only on aarch64
if(LUMEN_WITH_RKNN AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR
        "LUMEN_WITH_RKNN=ON but CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}' - "
        "RKNN is the Orange Pi's NPU and only exists on aarch64. Turn it off for this configure.")
endif()
if(LUMEN_WITH_RGA AND NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    message(FATAL_ERROR
        "LUMEN_WITH_RGA=ON but CMAKE_SYSTEM_PROCESSOR is '${CMAKE_SYSTEM_PROCESSOR}' - "
        "RGA is the Orange Pi's 2D accelerator and only exists on aarch64. Turn it off for this configure.")
endif()

message(STATUS "LumenCore enabled features: ${LUMEN_ENABLED_FEATURES}")
