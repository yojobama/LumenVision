#[[
  Finds the prebuilt ONNX Runtime release (downloaded by install-deps.sh, not built from source).

  Variables consulted (optional):
    ONNXRUNTIME_ROOT - root of an unpacked onnxruntime release (include/ and lib/); on Linux
                       /usr/local is normally enough

  Provides:
    OnnxRuntime::onnxruntime - INTERFACE target: include dir + the runtime lib linked
    ONNXRUNTIME_FOUND
]]

find_path(ONNXRUNTIME_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS "${ONNXRUNTIME_ROOT}/include"
)

find_library(ONNXRUNTIME_LIBRARY
    NAMES onnxruntime
    HINTS "${ONNXRUNTIME_ROOT}/lib"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OnnxRuntime
    REQUIRED_VARS ONNXRUNTIME_LIBRARY ONNXRUNTIME_INCLUDE_DIR
)

if(ONNXRUNTIME_FOUND AND NOT TARGET OnnxRuntime::onnxruntime)
    if(WIN32)
        # Real SHARED IMPORTED target so $<TARGET_RUNTIME_DLLS:...> can find onnxruntime.dll
        get_filename_component(_onnxruntime_lib_dir "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
        add_library(OnnxRuntime::onnxruntime SHARED IMPORTED)
        set_target_properties(OnnxRuntime::onnxruntime PROPERTIES
            IMPORTED_IMPLIB "${ONNXRUNTIME_LIBRARY}"
            IMPORTED_LOCATION "${_onnxruntime_lib_dir}/onnxruntime.dll"
        )
    else()
        add_library(OnnxRuntime::onnxruntime INTERFACE IMPORTED)
        target_link_libraries(OnnxRuntime::onnxruntime INTERFACE "${ONNXRUNTIME_LIBRARY}")
    endif()
    target_include_directories(OnnxRuntime::onnxruntime INTERFACE "${ONNXRUNTIME_INCLUDE_DIR}")
endif()

mark_as_advanced(ONNXRUNTIME_INCLUDE_DIR ONNXRUNTIME_LIBRARY)
