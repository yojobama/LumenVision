cmake_minimum_required(VERSION 3.16)
project(opencv_bench CXX)
# Times the OpenCV calls LumenCore makes, so an OpenCV upgrade (or a different board) can be compared with the same source:
#   cmake -S tools/opencv-bench -B build-bench -DOpenCV_DIR=<prefix>/lib/cmake/opencv5 && cmake --build build-bench && build-bench/opencv_bench
set(CMAKE_CXX_STANDARD 17)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
find_package(OpenCV REQUIRED)
add_executable(opencv_bench main.cpp)
target_link_libraries(opencv_bench PRIVATE ${OpenCV_LIBS})
