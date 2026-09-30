#[[
  The core C++ library, plus its SWIG-generated C# bindings.
]]

find_package(SWIG 4.0 REQUIRED COMPONENTS csharp)
include(UseSWIG)

set(LUMEN_CORE_SOURCES
    ApriltagDetection.cpp
    ApriltagDetector.cpp
    AprilTagFieldLayout.cpp
    CameraCalibrationResult.cpp
    CameraCalibrator.cpp
    CameraSource.cpp
    CpuApriltagBackend.cpp
    CpuAffinity.cpp
    Frame.cpp
    FramePool.cpp
    ISource.cpp
    ImageFileSource.cpp
    StereoCalibrationResult.cpp
    StereoCalibrator.cpp
    SgbmStereoBackend.cpp
    CodecStereoBackend.cpp
    StereoDepthNode.cpp
    DepthFusionNode.cpp
    ISink.cpp
    Logger.cpp
    Manager.cpp
    MjpegSink.cpp
    NetworkTablesSink.cpp
    ObjectDetection.cpp
    ObjectDetectionSink.cpp
    OnnxDetectionBackend.cpp
    OpenCvCameraBackend.cpp
    PixelUnpack.cpp
    RecordSink.cpp
    ResultPacket.cpp
    RoiSource.cpp
    SourceResult.cpp
    SystemMonitor.cpp
    VideoFileSource.cpp
    VkApriltagBackend.cpp
    WebRTCSink.cpp
    YoloPostProcess.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # <linux/videodev2.h> is Linux-only; OpenCvCameraBackend is the cross-platform fallback.
    list(APPEND LUMEN_CORE_SOURCES V4l2CameraBackend.cpp)
endif()

# --- Optional: hardware MJPEG decode via the RK3588's JPEG-decode VPU (rockchip_mpp) ------------
# MppJpegDecoder uses MPP's advanced decode_put_packet()/decode_get_frame() interface (the simple decode() API does not work for
# MJPEG); see MppJpegDecoder.cpp.
option(LUMEN_ENABLE_EXPERIMENTAL_MPP_JPEG "Hardware MJPEG decode via rockchip_mpp - see this file's own comment" ON)
set(LUMEN_WITH_MPP_JPEG OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LUMEN_ROCKCHIP_MPP QUIET rockchip_mpp)
    endif()
    if(LUMEN_ROCKCHIP_MPP_FOUND AND LUMEN_ENABLE_EXPERIMENTAL_MPP_JPEG)
        set(LUMEN_WITH_MPP_JPEG ON)
        list(APPEND LUMEN_CORE_SOURCES MppJpegDecoder.cpp)
    elseif(NOT LUMEN_ROCKCHIP_MPP_FOUND)
        message(WARNING "rockchip_mpp not found - V4l2CameraBackend will use software JPEG decode only")
    endif()
endif()
# Plain variables are local to this subdirectory's scope; PARENT_SCOPE exposes them to the sibling tests/ scope, whose
# `if(LUMEN_WITH_MPP_JPEG)` block would otherwise see them unset.
set(LUMEN_WITH_MPP_JPEG ${LUMEN_WITH_MPP_JPEG} PARENT_SCOPE)
set(LUMEN_ROCKCHIP_MPP_FOUND ${LUMEN_ROCKCHIP_MPP_FOUND} PARENT_SCOPE)
set(LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS ${LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS} PARENT_SCOPE)
set(LUMEN_ROCKCHIP_MPP_LIBRARIES ${LUMEN_ROCKCHIP_MPP_LIBRARIES} PARENT_SCOPE)

if(WIN32)
    # Manager::EnumerateAvailableCameras' Windows implementation (Media Foundation MFEnumDeviceSources), in its own translation
    # unit to avoid a `using namespace std;`/Windows-SDK COM header ambiguity (see WindowsCameraEnumerator.h).
    list(APPEND LUMEN_CORE_SOURCES WindowsCameraEnumerator.cpp)
endif()

if(LUMEN_WITH_RKNN)
    # guarded so the file isn't compiled, and <rknn_api.h> isn't searched for, when LumenFeatures.cmake refused this flag (non-aarch64).
    list(APPEND LUMEN_CORE_SOURCES RknnDetectionBackend.cpp)
endif()

if(LUMEN_WITH_RGA)
    # <im2d.h> only exists on the Pi image.
    list(APPEND LUMEN_CORE_SOURCES RgaColorConverter.cpp)
endif()

# --- SWIG source properties -------------------------------------------------------------------
# Generated C# goes directly into Server/Interop/ (gitignored, regenerated on every configure/build) for Server.csproj's glob.
# The generated C++ wrapper (swig_wrap.cxx) stays in the build tree via OUTFILE_DIR.
set_property(SOURCE swig.i PROPERTY CPLUSPLUS ON)
# Lets SWIG's preprocessing pass resolve swig.i's #include chain by reusing the target's include directories.
set_property(SOURCE swig.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)
# SWIG dependency tracking ignores the #include lines in swig.i's %{ %} block, so these headers are declared explicitly to
# re-run SWIG when they change.
set_property(SOURCE swig.i PROPERTY DEPENDS
    Manager.h
    CameraCalibrationResult.h
    IDetectionBackend.h
    IApriltagBackend.h
    CalibrationBoardType.h
    StereoCalibrationResult.h
    StereoDepthBackendKind.h
    StereoFrameOutput.h
    CameraMode.h
    FrameFormat.h
)

swig_add_library(LumenCore
    TYPE SHARED
    LANGUAGE csharp
    OUTPUT_DIR "${CMAKE_SOURCE_DIR}/Server/Interop"
    OUTFILE_DIR "${CMAKE_CURRENT_BINARY_DIR}/swig"
    SOURCES swig.i ${LUMEN_CORE_SOURCES}
)

# swig_wrap.cxx is generated in the build tree (OUTFILE_DIR), so its quote-include of "Manager.h" needs LumenCore/ on the include path.
# USE_TARGET_INCLUDE_DIRECTORIES (set above) makes SWIG's own preprocessing use it too.
target_include_directories(LumenCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(LUMEN_WITH_MPP_JPEG)
    target_compile_definitions(LumenCore PRIVATE LUMEN_WITH_MPP_JPEG)
    target_include_directories(LumenCore PRIVATE ${LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS})
    target_link_libraries(LumenCore PRIVATE ${LUMEN_ROCKCHIP_MPP_LIBRARIES})
endif()

# No PREFIX override: the default lib prefix on Linux and none on Windows lets one DllImport("LumenCore") resolve on both.
# A Windows DLL exports nothing by default, but the test executable and lumen-bench link LumenCore's internals (Logger, ISource, ...).
if(WIN32 AND (LUMEN_BUILD_TESTS OR LUMEN_BUILD_TOOLS))
    set_target_properties(LumenCore PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set_target_properties(LumenCore PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
# CXX_STANDARD only affects LumenCore's own sources; PUBLIC cxx_std_20 makes consumers (tests/) compile as C++20, as the public
# headers use std::jthread.
target_compile_features(LumenCore PUBLIC cxx_std_20)

target_compile_definitions(LumenCore PRIVATE ${LUMEN_ENABLED_DEFINES})

# published verbatim by NetworkTablesSink's ".version" topic; PROJECT_VERSION comes from the top-level project() call.
target_compile_definitions(LumenCore PRIVATE LUMEN_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
    # NOMINMAX/WIN32_LEAN_AND_MEAN stop <windows.h> (pulled in by WPILib/ONNX Runtime headers) defining min/max/GetMessage macros,
    # which break std::max/std::min and Log::GetMessage(). /utf-8: WPILib's bundled fmt statically asserts on it.
    target_compile_definitions(LumenCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(LumenCore PRIVATE /W3 /permissive- /utf-8)
    # Media Foundation (WindowsCameraEnumerator.cpp) ships in the Windows SDK, so plain .lib names suffice; mfreadwrite provides
    # IMFSourceReader (mode enumeration).
    target_link_libraries(LumenCore PRIVATE mfplat mf mfuuid mfreadwrite Ole32)
else()
    target_compile_options(LumenCore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Required dependencies (every configuration needs these) ---------------------------------
if(NOT DEFINED OpenCV_DIR AND EXISTS "/usr/local/lib/cmake/opencv5")
    # /usr/local may hold both OpenCV 5.0 and a stale 4.x install with similarly named config directories, so point find_package at
    # the 5.x one explicitly. Presets override OpenCV_DIR; this is only the default for the install-deps.sh Linux layout.
    set(OpenCV_DIR "/usr/local/lib/cmake/opencv5" CACHE PATH "OpenCV 5.x config directory")
endif()
find_package(OpenCV 5.0 REQUIRED CONFIG
    COMPONENTS core imgproc imgcodecs videoio dnn objdetect calib stereo geometry
)
find_package(Threads REQUIRED)
# Declared explicitly: on Linux the header comes from apt's nlohmann-json3-dev under /usr/include, but other platforms (MSVC) need
# the CMake package (SourceResult.h includes <nlohmann/json.hpp>).
find_package(nlohmann_json 3 REQUIRED CONFIG)

# PUBLIC for OpenCV and nlohmann_json: SourceResult.h, included by every consumer, #includes both headers, so their usage
# requirements are part of the public interface. Threads stays PRIVATE.
target_link_libraries(LumenCore PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)
target_link_libraries(LumenCore PRIVATE Threads::Threads)
target_include_directories(LumenCore PUBLIC ${OpenCV_INCLUDE_DIRS})

# SgbmStereoBackend (STEREO_BACKEND_SGBM) is pure OpenCV with no feature flag: always available on every platform.

# --- apriltag: exactly one, enforced by the build graph ---------------------------------------
# The system apriltag and vkapriltag's FetchContent copy build a library with the same SONAME (libapriltag.so.3), and only the
# patched 3.4.5 build exports the symbols VkApriltagBackend needs (quad_decode_index, reconcile_detections): exactly one apriltag may exist.
#
# When LUMEN_WITH_VULKAN_APRILTAG is ON, vkapriltag's subdirectory is added first and its FetchContent-built `apriltag` target is the
# one apriltag everything links against, including CpuApriltagBackend.cpp. When OFF, find_package(apriltag) resolves the installed build.
if(LUMEN_WITH_VULKAN_APRILTAG)
    find_package(Vulkan REQUIRED)
    set(VKAPRILTAG_BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(VKAPRILTAG_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    # vkapriltag (pinned to an upstream commit, unpatched) calls target_compile_options(apriltag PRIVATE -std=gnu17) unconditionally.
    # When `apriltag` is an ALIAS of an installed package (FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS) CMake rejects that call, so this override
    # skips ALIAS targets and forwards every other call exactly (arguments bracket-quoted so ';' survives). The NOT COMMAND guard overrides once.
    if(NOT COMMAND _target_compile_options)
        function(target_compile_options tgt)
            if(TARGET "${tgt}")
                get_target_property(_lumen_aliased "${tgt}" ALIASED_TARGET)
                if(_lumen_aliased)
                    message(STATUS "Skipping target_compile_options on ALIAS target '${tgt}' (-> ${_lumen_aliased}) - options are meaningless on an installed package")
                    return()
                endif()
            endif()
            set(_lumen_args "")
            math(EXPR _lumen_last "${ARGC} - 1")
            foreach(_lumen_i RANGE 0 ${_lumen_last})
                string(APPEND _lumen_args " [==[${ARGV${_lumen_i}}]==]")
            endforeach()
            cmake_language(EVAL CODE "_target_compile_options(${_lumen_args})")
        endfunction()
    endif()

    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/vkapriltag/apriltags_vulkan" vkapriltag)
    # vkapriltag's library creates a plain `apriltag` target (not a namespaced alias); link against that.
    target_link_libraries(LumenCore PRIVATE apriltag vkapriltag::vkapriltag Vulkan::Vulkan)
else()
    find_package(apriltag REQUIRED CONFIG)
    target_link_libraries(LumenCore PRIVATE apriltag::apriltag)
endif()

# --- Optional: codec-stereo (hardware-motion-vector stereo depth submodule) -------------------
if(LUMEN_WITH_CODEC_STEREO)
    set(CS_ENABLE_REF_SAD ON CACHE BOOL "" FORCE)
    # CS_ENABLE_LAVC needs ffmpeg, so only enable it where LumenFFmpeg.cmake found it.
    include(LumenFFmpeg)
    if(LUMEN_FFMPEG_FOUND)
        set(CS_ENABLE_LAVC ON CACHE BOOL "" FORCE)
    else()
        set(CS_ENABLE_LAVC OFF CACHE BOOL "" FORCE)
    endif()
    # CS_ENABLE_RKMPP is never enabled (see StereoDepthBackendKind.h). CS_ENABLE_RKMPP_HWENC reuses the LUMEN_ROCKCHIP_MPP probe above.
    set(CS_ENABLE_RKMPP OFF CACHE BOOL "" FORCE)
    if(LUMEN_ROCKCHIP_MPP_FOUND)
        set(CS_ENABLE_RKMPP_HWENC ON CACHE BOOL "" FORCE)
    else()
        set(CS_ENABLE_RKMPP_HWENC OFF CACHE BOOL "" FORCE)
    endif()
    set(CS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(CS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # Its harness pkg-configs the system opencv4, which collides with OpenCV 5.0; LumenCore doesn't need it.
    set(CS_BUILD_HARNESS OFF CACHE BOOL "" FORCE)
    set(CS_BUILD_PIPELINE ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/codec-stereo" codec-stereo)
    # codec_stereo declares PUBLIC m (UNIX) and Threads::Threads, which CMake target linking propagates automatically.
    target_link_libraries(LumenCore PRIVATE codec_stereo)
endif()

# --- Optional: ONNX Runtime object detection backend -------------------------------------------
if(LUMEN_WITH_ONNX)
    find_package(OnnxRuntime REQUIRED)
    target_link_libraries(LumenCore PRIVATE OnnxRuntime::onnxruntime)
endif()

# --- Optional: NetworkTables 4 publishing -------------------------------------------------------
if(LUMEN_WITH_NT4)
    find_package(Ntcore REQUIRED)
    target_link_libraries(LumenCore PRIVATE Ntcore::ntcore)
endif()

# ffmpeg (Lumen::ffmpeg) is shared by WebRTC (RTP only) and RECORD (MP4 muxing via libavformat); included once here, and
# LumenFFmpeg.cmake guards its add_library with `if(NOT TARGET Lumen::ffmpeg)`.
if(LUMEN_WITH_WEBRTC OR LUMEN_WITH_RECORD)
    include(LumenFFmpeg)
    if(NOT LUMEN_FFMPEG_FOUND)
        message(FATAL_ERROR "LUMEN_WITH_WEBRTC or LUMEN_WITH_RECORD is ON but ffmpeg (libavcodec/avformat/avutil/swscale) was not found via pkg-config")
    endif()
    target_link_libraries(LumenCore PRIVATE Lumen::ffmpeg)
endif()

# --- Optional: WebRTC live view -----------------------------------------------------------------
if(LUMEN_WITH_WEBRTC)
    find_package(LibDataChannel REQUIRED)
    target_link_libraries(LumenCore PRIVATE LibDataChannel::LibDataChannel)
endif()

# --- Optional: Rockchip NPU object detection (aarch64 only) ----------------------------------
if(LUMEN_WITH_RKNN)
    find_package(RKNN REQUIRED)
    target_link_libraries(LumenCore PRIVATE RKNN::rknn)
endif()

# --- Optional: Rockchip RGA hardware colour conversion for WebRTCSink (aarch64 only) ----------
# librga ships its own .pc file (written by install-deps.sh --with-mpp), so plain pkg-config suffices.
if(LUMEN_WITH_RGA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUMEN_RGA REQUIRED IMPORTED_TARGET librga)
    target_link_libraries(LumenCore PRIVATE PkgConfig::LUMEN_RGA)
endif()

# --- Packaging: collect every transitive runtime dependency next to the built library ---------
# Windows has no rpath, so every DLL LumenCore depends on (OpenCV, ONNX Runtime, ntcore, ffmpeg, ...) must sit next to it for the .NET host.
# A POST_BUILD copy (not install()) is used since Server.csproj only triggers `cmake --build`; $<TARGET_FILE_DIR:LumenCore> is the per-config output directory.
# Windows-only: on Linux $<TARGET_RUNTIME_DLLS:...> is empty and `cmake -E copy_if_different` with no sources is malformed.
# Server.csproj globs this directory, so no file list is kept in sync.
if(WIN32)
    # LUMEN_FFMPEG_RUNTIME_DLLS (LumenFFmpeg.cmake): pkg-config targets have no IMPORTED_LOCATION on Windows, so
    # $<TARGET_RUNTIME_DLLS:...> can't find them; they are copied in the same command.
    add_custom_command(TARGET LumenCore POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:LumenCore>"
                ${LUMEN_FFMPEG_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:LumenCore>"
        COMMAND_EXPAND_LISTS
    )
else()
    # Linux finds its dependencies via rpath/ldconfig, so this copy is for PACKAGING (scripts/build-deb.sh): it stages every vendored/from-source
    # runtime dependency next to libLumenCore.so for Server.csproj's *.so glob. See CopyLinuxRuntimeDeps.cmake for why only /usr/local and /opt/lumenvision-ffmpeg are copied.
    add_custom_command(TARGET LumenCore POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                -D "EXECUTABLE=$<TARGET_FILE:LumenCore>"
                -D "DEST_DIR=$<TARGET_FILE_DIR:LumenCore>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CopyLinuxRuntimeDeps.cmake"
    )
endif()

# --- The SWIG/native pairing stamp -------------------------------------------------------------
# Server.csproj's CheckLumenCoreExists checks this stamp matches the native library, catching C# bindings from one configure paired
# with a native library from another (an EntryPointNotFoundException at runtime otherwise).
string(REPLACE ";" "," _lumen_stamp_defines "${LUMEN_SWIG_DEFINES}")
file(WRITE "${CMAKE_SOURCE_DIR}/Server/Interop/.swig-stamp"
    "swig=${SWIG_VERSION}\nmodule=LumenCore\ndefines=${_lumen_stamp_defines}\npreset=${CMAKE_BUILD_TYPE}-${CMAKE_SYSTEM_PROCESSOR}\n"
)
