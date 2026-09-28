#[[
  Finds the Rockchip RKNN runtime (rknn_api.h + librknnrt.so) in the default /usr/local prefix.
  aarch64 only; LumenFeatures.cmake rejects LUMEN_WITH_RKNN=ON elsewhere.
  Finding the library does not check that its version matches the kernel's rknpu driver.

  Provides:
    RKNN::rknn    - INTERFACE target: include dir + librknnrt.so linked
    RKNN_FOUND
]]

find_path(RKNN_INCLUDE_DIR NAMES rknn_api.h)
find_library(RKNN_LIBRARY NAMES rknnrt)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(RKNN
    REQUIRED_VARS RKNN_LIBRARY RKNN_INCLUDE_DIR
)

if(RKNN_FOUND AND NOT TARGET RKNN::rknn)
    add_library(RKNN::rknn INTERFACE IMPORTED)
    target_include_directories(RKNN::rknn INTERFACE "${RKNN_INCLUDE_DIR}")
    target_link_libraries(RKNN::rknn INTERFACE "${RKNN_LIBRARY}")
endif()

mark_as_advanced(RKNN_INCLUDE_DIR RKNN_LIBRARY)
