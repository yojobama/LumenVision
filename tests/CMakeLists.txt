#[[
  Catch2 test suite for LumenCore, fetched via FetchContent.
]]

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(LumenCoreTests
    test_source_result.cpp
    test_source_sink_lifecycle.cpp
    test_yolo_postprocess.cpp
    test_camera_v4l2_hitl.cpp
    test_pixel_unpack.cpp
    test_roi_source.cpp
    test_driver_mode.cpp
    test_snapshot.cpp
    test_multitag_pnp.cpp
    test_sgbm_synthetic_disparity.cpp
    test_camera_calibrator.cpp
    test_apriltag_tuning.cpp
    test_result_packet.cpp
    test_coordinate_frames.cpp
    test_constrained_pnp.cpp
    test_frame_transform.cpp
    test_logger.cpp
    test_mjpeg_settings.cpp
    test_stereo_calibrator.cpp
)

if(LUMEN_WITH_ONNX)
    # The header is guarded by LUMEN_WITH_ONNX, and its inline onnxruntime C++ wrappers need a
    # direct link (LumenCore links onnxruntime PRIVATE).
    find_package(OnnxRuntime REQUIRED)
    target_sources(LumenCoreTests PRIVATE test_onnx_detection.cpp)
    target_compile_definitions(LumenCoreTests PRIVATE LUMEN_WITH_ONNX)
    target_link_libraries(LumenCoreTests PRIVATE OnnxRuntime::onnxruntime)
endif()

if(LUMEN_WITH_RKNN)
    # LumenCore defines LUMEN_WITH_RKNN PRIVATE; the test needs it to see the guarded header.
    target_sources(LumenCoreTests PRIVATE test_rknn_detection_hitl.cpp)
    target_compile_definitions(LumenCoreTests PRIVATE LUMEN_WITH_RKNN)
endif()

if(LUMEN_WITH_VULKAN_APRILTAG)
    # VkApriltagBackend.h includes vkapriltag headers, whose PRIVATE LumenCore linkage does not propagate.
    find_package(Vulkan REQUIRED)
    target_sources(LumenCoreTests PRIVATE test_apriltag_backend_agreement_hitl.cpp)
    target_compile_definitions(LumenCoreTests PRIVATE LUMEN_WITH_VULKAN_APRILTAG)
    target_link_libraries(LumenCoreTests PRIVATE apriltag vkapriltag::vkapriltag Vulkan::Vulkan)
endif()

if(LUMEN_WITH_NT4)
    # NetworkTablesSink.h is guarded by LUMEN_WITH_NT4 and uses ntcore types directly.
    find_package(Ntcore REQUIRED)
    target_sources(LumenCoreTests PRIVATE test_networktables_nt4_e2e.cpp)
    target_compile_definitions(LumenCoreTests PRIVATE LUMEN_WITH_NT4 LUMEN_VERSION_STRING="${PROJECT_VERSION}")
    target_link_libraries(LumenCoreTests PRIVATE Ntcore::ntcore)
endif()

if(LUMEN_WITH_MPP_JPEG)
    # MppJpegDecoder.h is guarded by LUMEN_WITH_MPP_JPEG and needs the rockchip_mpp headers and libraries.
    target_sources(LumenCoreTests PRIVATE test_mpp_jpeg_decoder_hitl.cpp)
    target_compile_definitions(LumenCoreTests PRIVATE LUMEN_WITH_MPP_JPEG)
    target_include_directories(LumenCoreTests PRIVATE ${LUMEN_ROCKCHIP_MPP_INCLUDE_DIRS})
    target_link_libraries(LumenCoreTests PRIVATE ${LUMEN_ROCKCHIP_MPP_LIBRARIES})
endif()

if(LUMEN_WITH_CODEC_STEREO)
    # CodecStereoBackend.h is guarded by LUMEN_WITH_CODEC_STEREO; no extra include or link needed.
    target_sources(LumenCoreTests PRIVATE
        test_codec_stereo_rkmpp_hitl.cpp
        test_codec_stereo_lavc_synthetic_disparity.cpp
    )
    target_compile_definitions(LumenCoreTests PRIVATE LUMEN_WITH_CODEC_STEREO)
endif()

target_link_libraries(LumenCoreTests PRIVATE
    LumenCore
    Catch2::Catch2WithMain
)

# LumenCore headers are included from the source tree.
target_include_directories(LumenCoreTests PRIVATE "${CMAKE_SOURCE_DIR}/LumenCore")

# Absolute path: catch_discover_tests runs tests from the executable's output directory.
target_compile_definitions(LumenCoreTests PRIVATE LUMEN_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

# Sample images from the vkapriltag submodule, defined regardless of LUMEN_WITH_VULKAN_APRILTAG.
target_compile_definitions(LumenCoreTests PRIVATE LUMEN_VKAPRILTAG_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/third_party/vkapriltag/apriltags_vulkan")

if(MSVC)
    target_compile_definitions(LumenCoreTests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(LumenCoreTests PRIVATE /utf-8)
endif()

# Catch.cmake (catch_discover_tests) lives in the fetched Catch2 extras directory.
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(LumenCoreTests)
