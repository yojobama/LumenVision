#[[
  The core C++ library, plus its SWIG-generated C# bindings. Everything in this file replaces
  LumenCore.vcxproj (the WSL2/Remote_GCC MSBuild toolset) - see ROADMAP.md Phase A2 for why: that
  project's ApplicationType=Linux is a project-level global, so a Windows/MSVC configuration
  cannot be added to it at all, which is what actually forces this migration rather than it being
  a nice-to-have.
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
    RoiSource.cpp
    SourceResult.cpp
    SystemMonitor.cpp
    VideoFileSource.cpp
    VkApriltagBackend.cpp
    WebRTCSink.cpp
    YoloPostProcess.cpp
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # <linux/videodev2.h> and friends - genuinely Linux-only, unlike most of this project's C++,
    # which is confirmed portable (see this file's own header comment / ROADMAP.md's "ground
    # truth" section). MediaFoundationCameraBackend is the Windows equivalent (ROADMAP.md Phase 3,
    # not yet written); OpenCvCameraBackend remains the cross-platform fallback either way.
    list(APPEND LUMEN_CORE_SOURCES V4l2CameraBackend.cpp)
endif()

# --- Optional: hardware MJPEG decode via the RK3588's JPEG-decode VPU (rockchip_mpp) ------------
# Two structurally different fixes have gone into MppJpegDecoder so far - registering a real
# MPP_DEC_SET_EXT_BUF_GROUP output buffer group (real MPP decode requires one; confirmed against
# rockchip-linux/mpp's own utils/mpi_dec_utils.c), and backing the INPUT packet with a real
# DMA-capable MppBuffer instead of a raw heap pointer, plus setting up MppDecCfg (split_parse)
# before the first decode call (also required per that same reference). All three are real,
# necessary fixes and stay in the code. None of them were sufficient: on real hardware (Orange Pi
# 5 Plus, RK3588, vendor kernel 6.1.115-rk35xx), api->decode() still crashes - confirmed via kernel
# fault correlation (dmesg pc/lr against /proc/self/maps) to fault at the IDENTICAL relative offset,
# mpp_dec_decode+0x420, across TWO independent MPP versions (develop@14729dd and the tagged 1.1.0
# release this project now pins to - see install-deps.sh's MPP_REF). That offset lands inside
# mpp_dec_decode's own mpp_parser_prepare() call (mpp/codec/mpp_dec_no_thread.c in the 1.1.0 tag),
# i.e. inside MPP's own internal JPEG-bitstream parser, not in anything this caller controls
# directly - version-independent and calling-pattern-independent within what's been tried, which
# points at a genuine incompatibility between this vendor kernel/driver and MPP's JPEG decode path
# rather than a caller-side bug. h264_rkmpp ENCODE is independently confirmed working on this same
# board (RecordSink/WebRTCSink), so the hardware/kernel/MPP stack as a whole is not broken - only
# this specific decode path is. Left OFF by default until this is actually resolved; flip
# LUMEN_ENABLE_EXPERIMENTAL_MPP_JPEG on for further hardware debugging only, not for a real build.
option(LUMEN_ENABLE_EXPERIMENTAL_MPP_JPEG "Hardware MJPEG decode via rockchip_mpp - see this file's own comment" OFF)
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
# LUMEN_WITH_MPP_JPEG/LUMEN_ROCKCHIP_MPP_* are plain variables local to THIS subdirectory's own
# scope by default - add_subdirectory(tests) in the root CMakeLists.txt is a SEPARATE sibling
# scope (added after this one, per that file's own ordering), which would otherwise never see
# these at all despite its own `if(LUMEN_WITH_MPP_JPEG)` block referencing them (confirmed the
# hard way: the CI arm64 runner compiled MppJpegDecoder.cpp into LumenCore correctly but never
# even compiled the test meant to exercise it - the guard silently evaluated false). PARENT_SCOPE
# pushes each one up to the root, which tests/ then inherits normally.
set(LUMEN_WITH_MPP_JPEG ${LUMEN_WITH_MPP_JPEG} PARENT_SCOPE)
set(LUMEN_ROCKCHIP_MPP_FOUND ${LUMEN_ROCKCHIP_MPP_FOUND} PARENT_SCOPE)
set(LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS ${LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS} PARENT_SCOPE)
set(LUMEN_ROCKCHIP_MPP_LIBRARIES ${LUMEN_ROCKCHIP_MPP_LIBRARIES} PARENT_SCOPE)

if(WIN32)
    # Manager::EnumerateAvailableCameras' Windows implementation (Media Foundation's
    # MFEnumDeviceSources) - a separate translation unit from Manager.cpp on purpose, see
    # WindowsCameraEnumerator.h's own comment for why (a `using namespace std;`/Windows-SDK-COM-
    # headers ambiguity, not stylistic preference).
    list(APPEND LUMEN_CORE_SOURCES WindowsCameraEnumerator.cpp)
endif()

if(LUMEN_WITH_RKNN)
    # guarded here (not just by its own #ifdef LUMEN_WITH_RKNN) so the file isn't even compiled -
    # and <rknn_api.h> isn't even searched for - on a configure where LumenFeatures.cmake has
    # already refused this flag outright (anything but aarch64).
    list(APPEND LUMEN_CORE_SOURCES RknnDetectionBackend.cpp)
endif()

if(LUMEN_WITH_RGA)
    # same reasoning as LUMEN_WITH_RKNN immediately above - <im2d.h> only exists on the Pi image.
    list(APPEND LUMEN_CORE_SOURCES RgaColorConverter.cpp)
endif()

# --- SWIG source properties -------------------------------------------------------------------
# Generated C# lands directly in Server/Interop/ (gitignored, regenerated every configure/build -
# see .gitignore's swig_wrap.cxx / **/Interop/*.cs rules) so Server.csproj's own glob picks it up
# with no path translation step. The generated C++ wrapper (swig_wrap.cxx) stays inside the build
# tree instead, via OUTFILE_DIR - it has no reason to live next to hand-written sources.
set_property(SOURCE swig.i PROPERTY CPLUSPLUS ON)
# Lets swig.i's own #include chain (Manager.h -> ISink.h -> SourceResult.h ->
# <opencv2/opencv.hpp>, etc.) resolve during SWIG's preprocessing pass by reusing whatever
# target_include_directories the LumenCore target ends up with below - confirmed necessary
# (swig.i's own comments already document this: SWIG follows #include chains fine, but only
# if its own -I list covers them).
set_property(SOURCE swig.i PROPERTY USE_TARGET_INCLUDE_DIRECTORIES ON)
# The single source of truth from cmake/LumenFeatures.cmake - deliberately the FULL flag list
# (LUMEN_SWIG_DEFINES), not just what's enabled for this configure. See that file's own comment
# for why: this is what keeps every preset generating the identical C# API regardless of which
# backends are actually compiled in.
# CMake's SWIG dependency tracking only follows swig.i itself and anything brought in via a
# %include directive - it has no visibility into the plain #include lines inside swig.i's own
# %{ %} preamble block, so editing Manager.h (or any of these) alone did NOT re-trigger "Swig
# compile swig.i for csharp" - confirmed the hard way (a deleted method stayed callable from a
# stale generated wrapper after editing only Manager.h, with ninja reporting nothing to redo).
# Declaring them here explicitly is what actually fixes that, not just documents it.
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

# swig_wrap.cxx (SWIG's generated wrapper) lands in the BUILD tree (OUTFILE_DIR above), not next
# to the hand-written sources the way the old vcxproj's PreBuildEvent generated it directly into
# LumenCore/ itself - its own #include "Manager.h" (from swig.i's %{ %} block) is a quote-include,
# which searches relative to the INCLUDING file's own directory first. Without this, that search
# fails ("Manager.h: No such file or directory") since swig/ and LumenCore/ are now different
# directories. USE_TARGET_INCLUDE_DIRECTORIES ON (set on swig.i above) is what makes SWIG's own
# preprocessing pass pick this up too, for the same reason.
target_include_directories(LumenCore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(LUMEN_WITH_MPP_JPEG)
    target_compile_definitions(LumenCore PRIVATE LUMEN_WITH_MPP_JPEG)
    target_include_directories(LumenCore PRIVATE ${LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS})
    target_link_libraries(LumenCore PRIVATE ${LUMEN_ROCKCHIP_MPP_LIBRARIES})
endif()

# No PREFIX override: the default "lib" prefix on Linux (libLumenCore.so) plus no prefix on
# Windows (LumenCore.dll) is exactly what makes one DllImport("LumenCore") string in the
# generated C# resolve correctly on both platforms under .NET's default native-library probing.
set_target_properties(LumenCore PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
# CXX_STANDARD above only sets how LumenCore's OWN sources are compiled - it does not propagate
# to anything linking against it (ISource.h/ISink.h's std::jthread members are C++20, and those
# are public headers). target_compile_features PUBLIC is what actually makes a consumer (tests/,
# ROADMAP.md Phase 5) inherit the requirement - confirmed the hard way ("'jthread' in namespace
# 'std' does not name a type... only available from C++20 onwards" compiling a test that just
# includes ISource.h, on a compiler whose default standard is older than C++20).
target_compile_features(LumenCore PUBLIC cxx_std_20)

target_compile_definitions(LumenCore PRIVATE ${LUMEN_ENABLED_DEFINES})

# NetworkTablesSink's own ".version" NT4 topic (see NetworkTablesSink.cpp) publishes this
# verbatim - PROJECT_VERSION comes from the top-level project(LumenVision VERSION ...) call.
target_compile_definitions(LumenCore PRIVATE LUMEN_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
    # NOMINMAX/WIN32_LEAN_AND_MEAN: without these, <windows.h> (pulled in transitively by
    # WPILib/ONNX Runtime headers) defines max/min and GetMessage as macros, breaking
    # std::max/std::min call sites (error C2589) and Log::GetMessage() (error C2039 -
    # "GetMessageA is not a member of Log", the macro rewriting the call before the compiler
    # ever sees the real name) throughout this codebase - confirmed the hard way building this
    # target natively on Windows for the first time. /utf-8: WPILib's bundled fmt statically
    # asserts on it (fmt/base.h's Unicode support check).
    target_compile_definitions(LumenCore PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(LumenCore PRIVATE /W3 /permissive- /utf-8)
    # Media Foundation (WindowsCameraEnumerator.cpp's device + mode enumeration -
    # MFEnumDeviceSources/IMFSourceReader) ships as part of the Windows SDK, not a
    # vcpkg/FetchContent dependency - plain .lib names are enough, same as any other Windows SDK
    # import library. mfreadwrite is IMFSourceReader's own lib (MFCreateSourceReaderFromMediaSource) -
    # EnumerateWindowsCameraModes needs it, EnumerateWindowsCameras alone didn't.
    target_link_libraries(LumenCore PRIVATE mfplat mf mfuuid mfreadwrite Ole32)
else()
    target_compile_options(LumenCore PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# --- Required dependencies (every configuration needs these) ---------------------------------
if(NOT DEFINED OpenCV_DIR AND EXISTS "/usr/local/lib/cmake/opencv5")
    # This machine's /usr/local carries BOTH an OpenCV 5.0 install and a stale OpenCV 4.15 one
    # (install-deps.sh's own history explains why) with similarly-named config directories
    # (opencv5/ vs opencv4/) - find_package(OpenCV)'s default search can't be trusted to prefer
    # the right one, so point it explicitly rather than relying on directory-glob order. Any
    # preset (Windows in particular, once OpenCV is built there) overrides OpenCV_DIR itself;
    # this is only a default for this project's own known Linux install-deps.sh layout.
    set(OpenCV_DIR "/usr/local/lib/cmake/opencv5" CACHE PATH "OpenCV 5.x config directory")
endif()
find_package(OpenCV 5.0 REQUIRED CONFIG
    COMPONENTS core imgproc imgcodecs videoio dnn objdetect calib stereo geometry
)
find_package(Threads REQUIRED)
# Declared explicitly rather than left implicit: on Linux this has always resolved silently via
# apt's nlohmann-json3-dev dropping its header under /usr/include (the compiler's own default
# search path, found with no CMake involvement at all), which is why nothing here referenced it
# before. That has no Windows equivalent - confirmed the hard way (SourceResult.h's #include
# <nlohmann/json.hpp> failing outright under MSVC) - so this is a real portability fix, not a
# Windows-only workaround: a package installed via apt's or vcpkg's own CMake config now, on
# every platform.
find_package(nlohmann_json 3 REQUIRED CONFIG)

# PUBLIC, not PRIVATE, for OpenCV and nlohmann_json specifically: SourceResult.h - a header
# every consumer of this target includes, not an implementation detail - itself #includes
# <opencv2/opencv.hpp> and <nlohmann/json.hpp> directly, so their usage requirements (include
# dirs, and for OpenCV, the libraries too - cv::Mat's own methods aren't all header-only inline)
# are genuinely part of this target's public interface. Confirmed the hard way: tests/
# (ROADMAP.md Phase 5) linking LumenCore PRIVATE here got "opencv2/opencv.hpp: No such file or
# directory" the first time it included SourceResult.h. Threads stays PRIVATE - pthread usage is
# purely internal, nothing public leaks it.
target_link_libraries(LumenCore PUBLIC ${OpenCV_LIBS} nlohmann_json::nlohmann_json)
target_link_libraries(LumenCore PRIVATE Threads::Threads)
target_include_directories(LumenCore PUBLIC ${OpenCV_INCLUDE_DIRS})

# SgbmStereoBackend (STEREO_BACKEND_SGBM) is pure OpenCV with no feature flag guarding it at
# all - it is always available, on every platform, regardless of which optional backends below
# are turned on. See ROADMAP.md's note that this is what makes stereo depth work on Windows
# from day one even with LUMEN_WITH_CODEC_STEREO off.

# --- apriltag: exactly one, enforced by the build graph, not by hand discipline ---------------
# install-deps.sh's own long comment documents the old rule at length: the system apriltag and
# vkapriltag's private FetchContent copy produce a shared library with the SAME SONAME
# (libapriltag.so.3) regardless of the 3.3.0-vs-3.4.5 version difference, and only the patched
# 3.4.5 build exports the symbols (quad_decode_index, reconcile_detections) VkApriltagBackend
# needs - so there must be exactly one apriltag in the system, and it must be the patched one.
# That was enforced entirely by convention before (install-deps.sh purging apt's package, a
# human remembering to build the patched version first). CMake can make it structural instead:
#
# when LUMEN_WITH_VULKAN_APRILTAG is ON, vkapriltag's own subdirectory is added FIRST and its
# FetchContent-built (patched) `apriltag` target becomes the ONE apriltag the rest of this file
# links against - including CpuApriltagBackend.cpp, which has no Vulkan dependency of its own
# but has no reason to use a DIFFERENT apriltag build than VkApriltagBackend.cpp when both are
# compiled in. (FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS was tried first, to make this resolve
# to an already-installed apriltag::apriltag instead of a fresh FetchContent build - see
# CMakePresets.json's linux-base preset for why that doesn't work today.)
#
# When LUMEN_WITH_VULKAN_APRILTAG is OFF, vkapriltag's subdirectory is never added, so this
# falls back to find_package(apriltag) against whatever's installed (install-deps.sh's own
# from-source patched build on Linux, or a plain apriltag install anywhere else) - there is
# nothing else in the build that could collide with it in that case.
if(LUMEN_WITH_VULKAN_APRILTAG)
    find_package(Vulkan REQUIRED)
    set(VKAPRILTAG_BUILD_APPS OFF CACHE BOOL "" FORCE)
    set(VKAPRILTAG_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    # vkapriltag (pinned to an exact upstream commit - no local patches in the submodule) runs
    #   target_compile_options(apriltag PRIVATE -std=gnu17)
    # unconditionally right after FetchContent_MakeAvailable(apriltag). With
    # FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS (CMakePresets.json - it's what enforces "exactly
    # one apriltag"), `apriltag` resolves to the installed package, whose apriltagConfig.cmake
    # makes `apriltag` an ALIAS of apriltag::apriltag - and CMake rejects target_compile_options
    # on an ALIAS outright ("can not be used on an ALIAS target"), on every platform, failing the
    # whole configure. The flag only means anything when apriltag is compiled from source anyway.
    # So: a thin override of the builtin that skips ALIAS targets (turning what would be a hard
    # error into a no-op, with a STATUS line) and forwards every other call EXACTLY - each
    # argument bracket-quoted, so generator expressions containing ';' survive. CMake keeps the
    # original reachable as _target_compile_options; the NOT COMMAND guard overrides only once.
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
    # vkapriltag's own library/CMakeLists.txt creates a plain `apriltag` target via
    # FetchContent_MakeAvailable(apriltag), not a namespaced `apriltag::apriltag` alias - link
    # against exactly what it produced, not a name we'd have to guess or re-derive.
    target_link_libraries(LumenCore PRIVATE apriltag vkapriltag::vkapriltag Vulkan::Vulkan)
else()
    find_package(apriltag REQUIRED CONFIG)
    target_link_libraries(LumenCore PRIVATE apriltag::apriltag)
endif()

# --- Optional: codec-stereo (hardware-motion-vector stereo depth submodule) -------------------
if(LUMEN_WITH_CODEC_STEREO)
    set(CS_ENABLE_REF_SAD ON CACHE BOOL "" FORCE)
    # CS_ENABLE_LAVC needs ffmpeg - only turn it on where LumenFFmpeg.cmake actually found it,
    # rather than unconditionally (which would hard-fail codec-stereo's own configure on a
    # machine without ffmpeg dev packages, even though CS_ENABLE_REF_SAD alone works fine).
    include(LumenFFmpeg)
    if(LUMEN_FFMPEG_FOUND)
        set(CS_ENABLE_LAVC ON CACHE BOOL "" FORCE)
    else()
        set(CS_ENABLE_LAVC OFF CACHE BOOL "" FORCE)
    endif()
    # CS_ENABLE_RKMPP (the buggy KEY_MOTION_INFO readback backend) is deliberately never
    # enabled - see StereoDepthBackendKind.h. CS_ENABLE_RKMPP_HWENC just reuses the
    # LUMEN_ROCKCHIP_MPP probe from earlier in this file (the same pkg-config module, already
    # probed unconditionally there for MppJpegDecoder's own benefit - no need to probe it twice).
    set(CS_ENABLE_RKMPP OFF CACHE BOOL "" FORCE)
    if(LUMEN_ROCKCHIP_MPP_FOUND)
        set(CS_ENABLE_RKMPP_HWENC ON CACHE BOOL "" FORCE)
    else()
        set(CS_ENABLE_RKMPP_HWENC OFF CACHE BOOL "" FORCE)
    endif()
    set(CS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(CS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    # Its harness pkg-configs the system opencv4 package, which collides with this project's own
    # OpenCV 5.0 (its own CMakeLists carries a comment about exactly this) - not needed for
    # LumenCore anyway.
    set(CS_BUILD_HARNESS OFF CACHE BOOL "" FORCE)
    set(CS_BUILD_PIPELINE ON CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/codec-stereo" codec-stereo)
    # codec_stereo's own CMakeLists already declares `target_link_libraries(codec_stereo PUBLIC
    # m)` on UNIX and PUBLIC Threads::Threads - ordinary CMake target linking propagates both
    # automatically, unlike the old vcxproj's static-archive link which needed `m` listed by
    # hand (LumenCore.vcxproj's own comment on this explains why: a plain .a link doesn't
    # propagate a dependency's PUBLIC requirements the way a real CMake target does).
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

# ffmpeg (Lumen::ffmpeg) is shared by WebRTC (RTP-only, no muxing) and RECORD (real MP4 muxing via
# libavformat - see RecordSink.cpp) - included once here, ahead of both, rather than duplicated
# inside each block. LumenFFmpeg.cmake's own add_library call is already `if(NOT TARGET
# Lumen::ffmpeg)`-guarded, so this is also safe if something else ever includes it first.
if(LUMEN_WITH_WEBRTC OR LUMEN_WITH_RECORD)
    include(LumenFFmpeg)
    if(NOT LUMEN_FFMPEG_FOUND)
        message(FATAL_ERROR "LUMEN_WITH_WEBRTC or LUMEN_WITH_RECORD is ON but ffmpeg (libavcodec/avformat/avutil/swscale) was not found via pkg-config")
    endif()
endif()

# --- Optional: WebRTC live view -----------------------------------------------------------------
if(LUMEN_WITH_WEBRTC)
    find_package(LibDataChannel REQUIRED)
    target_link_libraries(LumenCore PRIVATE LibDataChannel::LibDataChannel Lumen::ffmpeg)
endif()

# --- Optional: Rockchip NPU object detection (aarch64 only) ----------------------------------
if(LUMEN_WITH_RKNN)
    find_package(RKNN REQUIRED)
    target_link_libraries(LumenCore PRIVATE RKNN::rknn)
endif()

# --- Optional: Rockchip RGA hardware colour conversion for WebRTCSink (aarch64 only) ----------
# librga ships its own .pc file (install-deps.sh --with-mpp writes one - see that script's own
# comment for why airockchip/librga has no upstream .pc of its own), so plain pkg-config is
# enough here, matching LumenFFmpeg.cmake's approach rather than a dedicated Find module.
if(LUMEN_WITH_RGA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LUMEN_RGA REQUIRED IMPORTED_TARGET librga)
    target_link_libraries(LumenCore PRIVATE PkgConfig::LUMEN_RGA)
endif()

# --- Packaging: collect every transitive runtime dependency next to the built library ---------
# Linux has rpath/LD_LIBRARY_PATH; Windows has neither, so every DLL LumenCore depends on
# (OpenCV, ONNX Runtime, ntcore, ffmpeg, libdatachannel, ...) must sit next to it for the .NET
# host process to find it. A POST_BUILD copy rather than install(FILES $<TARGET_RUNTIME_DLLS:...>)
# deliberately: install() only populates CMAKE_INSTALL_PREFIX after an explicit `cmake --install`
# step, which Server.csproj has no reason to know about or trigger - `dotnet build` triggering
# `cmake --build` alone should be enough, matching how the old vcxproj-based build never needed a
# separate install step either. $<TARGET_FILE_DIR:LumenCore> already resolves to the correct
# per-configuration output directory for the multi-config generators every preset uses (Ninja
# Multi-Config, Visual Studio), so this needs no per-config handling of its own. Windows-only:
# on Linux, $<TARGET_RUNTIME_DLLS:...> is simply empty (the .so's own DT_NEEDED entries plus the
# system loader already handle it), and `cmake -E copy_if_different` with no source arguments at
# all is a malformed invocation, not a harmless no-op - confirmed the hard way (it prints
# cmake -E's own usage help instead of copying anything, rather than silently doing nothing).
# Server.csproj globs this directory rather than hardcoding one filename (see its own comments)
# precisely so this list doesn't have to be kept in sync by hand on the C# side.
if(WIN32)
    # LUMEN_FFMPEG_RUNTIME_DLLS (LumenFFmpeg.cmake): pkg-config-discovered targets carry no
    # IMPORTED_LOCATION on Windows, so $<TARGET_RUNTIME_DLLS:...> alone can't find these -
    # folded into the same copy command rather than a second one so Server.csproj's directory
    # glob only has to watch one place.
    add_custom_command(TARGET LumenCore POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_RUNTIME_DLLS:LumenCore>"
                ${LUMEN_FFMPEG_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:LumenCore>"
        COMMAND_EXPAND_LISTS
    )
else()
    # Linux has rpath/LD_LIBRARY_PATH/ldconfig, so a plain `dotnet build`/dev loop against an
    # install-deps.sh'd machine needs no copy at all - the dynamic linker already finds
    # everything under /usr/local and /opt/lumenvision-ffmpeg on its own. This copy exists for
    # PACKAGING (scripts/build-deb.sh): it stages every from-source/vendored runtime dependency
    # next to libLumenCore.so so Server.csproj's existing *.so glob (see its own comment) bundles
    # them into the publish output automatically - the same mechanism that already ships them for
    # a plain deploy, not a packaging-only special case. See CopyLinuxRuntimeDeps.cmake's own
    # comment for why only /usr/local and /opt/lumenvision-ffmpeg are copied, not every resolved
    # dependency (glibc/libstdc++ must never be bundled; apt-managed libs are the target image's
    # own responsibility).
    add_custom_command(TARGET LumenCore POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
                -D "EXECUTABLE=$<TARGET_FILE:LumenCore>"
                -D "DEST_DIR=$<TARGET_FILE_DIR:LumenCore>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CopyLinuxRuntimeDeps.cmake"
    )
endif()

# --- The SWIG/native pairing stamp -------------------------------------------------------------
# Server.csproj's CheckLumenCoreExists target checks this file matches the native library it's
# about to bundle, not just that the .so/.dll merely exists - the case that check alone can't
# catch is C# bindings generated by one configure (one SWIG version, one define set) paired with
# a native library built by a DIFFERENT one, which happens easily in a multi-preset (Windows/
# WSL/Pi) workflow and previously had no detection at all beyond a runtime
# EntryPointNotFoundException.
string(REPLACE ";" "," _lumen_stamp_defines "${LUMEN_SWIG_DEFINES}")
file(WRITE "${CMAKE_SOURCE_DIR}/Server/Interop/.swig-stamp"
    "swig=${SWIG_VERSION}\nmodule=LumenCore\ndefines=${_lumen_stamp_defines}\npreset=${CMAKE_BUILD_TYPE}-${CMAKE_SYSTEM_PROCESSOR}\n"
)
