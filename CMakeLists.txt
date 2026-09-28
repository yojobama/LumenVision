cmake_minimum_required(VERSION 3.25)

# 3.25 covers FETCHCONTENT_TRY_FIND_PACKAGE_MODE (3.24) and $<TARGET_RUNTIME_DLLS:...> (3.21)
project(LumenVision
    VERSION 0.1.0
    DESCRIPTION "PhotonVision-style FRC vision coprocessor - AprilTag/object detection, calibration, WebRTC, NT4, stereo depth"
    LANGUAGES CXX
)
# Published as NetworkTablesSink's ".version" NT4 topic

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Stage all outputs in one directory so transitive DLLs sit next to Server.exe on Windows
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stage")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stage")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/stage")

include(LumenFeatures)

# Static archives (vkapriltag, codec_stereo) are linked into LumenCore's shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(LumenCore)

# Catch2/ctest suite; the ci-* presets enable it
option(LUMEN_BUILD_TESTS "Build the ctest suite (tests/)" OFF)
if(LUMEN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Standalone per-backend timing tool
option(LUMEN_BUILD_TOOLS "Build standalone tools (tools/lumen-bench)" OFF)
if(LUMEN_BUILD_TOOLS)
    add_subdirectory(tools/lumen-bench)
endif()
