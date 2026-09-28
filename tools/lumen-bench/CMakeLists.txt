#[[
  Standalone timing tool; built only when LUMEN_BUILD_TOOLS is on.
]]

add_executable(lumen-bench main.cpp)

set_target_properties(lumen-bench PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(lumen-bench PRIVATE LumenCore)

# LumenCore headers are included from the source tree.
target_include_directories(lumen-bench PRIVATE "${CMAKE_SOURCE_DIR}/LumenCore")

# Fixtures shared with the tests.
target_compile_definitions(lumen-bench PRIVATE
    LUMEN_VKAPRILTAG_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/third_party/vkapriltag/apriltags_vulkan"
    LUMEN_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data"
)

if(LUMEN_WITH_VULKAN_APRILTAG)
    # VkApriltagBackend.h includes vkapriltag headers, whose PRIVATE LumenCore linkage does not propagate.
    find_package(Vulkan REQUIRED)
    target_compile_definitions(lumen-bench PRIVATE LUMEN_WITH_VULKAN_APRILTAG)
    target_link_libraries(lumen-bench PRIVATE apriltag vkapriltag::vkapriltag Vulkan::Vulkan)
endif()

if(LUMEN_WITH_CODEC_STEREO)
    target_compile_definitions(lumen-bench PRIVATE LUMEN_WITH_CODEC_STEREO)
endif()

if(LUMEN_WITH_ONNX)
    # onnxruntime C++ wrappers are inline and need a direct link.
    find_package(OnnxRuntime REQUIRED)
    target_compile_definitions(lumen-bench PRIVATE LUMEN_WITH_ONNX)
    target_link_libraries(lumen-bench PRIVATE OnnxRuntime::onnxruntime)
endif()

if(LUMEN_WITH_RKNN)
    target_compile_definitions(lumen-bench PRIVATE LUMEN_WITH_RKNN)
endif()

if(MSVC)
    target_compile_definitions(lumen-bench PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(lumen-bench PRIVATE /utf-8)
endif()
