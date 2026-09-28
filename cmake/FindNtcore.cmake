#[[
  Finds WPILib's ntcore, wpiutil and wpinet and exposes them as one interface target,
  Ntcore::ntcore, which links all three on every platform.


  Variables consulted (all optional):
    WPILIB_ROOT   - root of an unpacked WPILib tree (include/ and lib|bin/); on Linux /usr/local
                    is normally enough

  Provides:
    Ntcore::ntcore        - INTERFACE target: include dirs + ntcore, wpiutil, wpinet all linked
    NTCORE_FOUND
]]

find_path(NTCORE_INCLUDE_DIR
    NAMES ntcore_cpp.h
    HINTS "${WPILIB_ROOT}/include"
)

find_path(WPINET_INCLUDE_DIR
    NAMES wpinet/uv/Loop.h
    HINTS "${WPILIB_ROOT}/include"
)

find_path(WPIUTIL_INCLUDE_DIR
    NAMES wpi/json.h
    HINTS "${WPILIB_ROOT}/include"
)

# On Windows find_library returns the import .lib; the runtime .dll is derived from it below
find_library(NTCORE_LIBRARY   NAMES ntcore   HINTS "${WPILIB_ROOT}/lib" "${WPILIB_ROOT}/bin")
find_library(WPIUTIL_LIBRARY  NAMES wpiutil  HINTS "${WPILIB_ROOT}/lib" "${WPILIB_ROOT}/bin")
find_library(WPINET_LIBRARY   NAMES wpinet   HINTS "${WPILIB_ROOT}/lib" "${WPILIB_ROOT}/bin")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Ntcore
    REQUIRED_VARS NTCORE_LIBRARY WPIUTIL_LIBRARY WPINET_LIBRARY
                   NTCORE_INCLUDE_DIR WPINET_INCLUDE_DIR WPIUTIL_INCLUDE_DIR
)

if(NTCORE_FOUND AND NOT TARGET Ntcore::ntcore)
    if(WIN32)
        # Real SHARED IMPORTED targets so $<TARGET_RUNTIME_DLLS:...> can find each .dll
        foreach(_lib NTCORE WPINET WPIUTIL)
            get_filename_component(_dir "${${_lib}_LIBRARY}" DIRECTORY)
            get_filename_component(_name "${${_lib}_LIBRARY}" NAME_WE)
            add_library(Ntcore::${_lib} SHARED IMPORTED)
            set_target_properties(Ntcore::${_lib} PROPERTIES
                IMPORTED_IMPLIB "${${_lib}_LIBRARY}"
                IMPORTED_LOCATION "${_dir}/${_name}.dll"
            )
        endforeach()
        add_library(Ntcore::ntcore INTERFACE IMPORTED)
        target_link_libraries(Ntcore::ntcore INTERFACE Ntcore::NTCORE Ntcore::WPINET Ntcore::WPIUTIL)
    else()
        add_library(Ntcore::ntcore INTERFACE IMPORTED)
        # ntcore depends on wpiutil/wpinet, so they must follow it on the link line
        target_link_libraries(Ntcore::ntcore INTERFACE
            "${NTCORE_LIBRARY}" "${WPINET_LIBRARY}" "${WPIUTIL_LIBRARY}"
        )
    endif()
    target_include_directories(Ntcore::ntcore INTERFACE
        "${NTCORE_INCLUDE_DIR}" "${WPINET_INCLUDE_DIR}" "${WPIUTIL_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(
    NTCORE_INCLUDE_DIR WPINET_INCLUDE_DIR WPIUTIL_INCLUDE_DIR
    NTCORE_LIBRARY WPIUTIL_LIBRARY WPINET_LIBRARY
)
