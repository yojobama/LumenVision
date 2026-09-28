#[[
  Script-mode helper (cmake -D EXECUTABLE=... -D DEST_DIR=... -P) that copies the runtime .so files
  libLumenCore transitively needs next to the built library (Linux counterpart of $<TARGET_RUNTIME_DLLS>).

  Copies anything resolved under /usr/local or /opt/lumenvision-ffmpeg (from-source dependencies no
  apt package owns) plus the codec allowlist below. glibc, libstdc++ and other apt libraries are
  never bundled; see scripts/build-deb.sh for the package Depends: list.

  The allowlist covers image/media codec libraries whose SONAMEs differ between Ubuntu and Debian
  (e.g. libjpeg 8 vs 62), so a Depends: on them would not resolve on the target image.
]]

if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "CopyLinuxRuntimeDeps.cmake requires -D EXECUTABLE=<path to libLumenCore.so>")
endif()
if(NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "CopyLinuxRuntimeDeps.cmake requires -D DEST_DIR=<directory to copy resolved deps into>")
endif()

# DIRECTORIES lets the transitive walk find dependencies of from-source libraries that carry no RPATH
# (e.g. libswresample.so needed by libavcodec.so)
file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "${EXECUTABLE}"
    DIRECTORIES "/usr/local/lib" "/opt/lumenvision-ffmpeg/lib"
    RESOLVED_DEPENDENCIES_VAR _resolvedDeps
    UNRESOLVED_DEPENDENCIES_VAR _unresolvedDeps
)

if(_unresolvedDeps)
    message(WARNING "CopyLinuxRuntimeDeps.cmake: could not resolve: ${_unresolvedDeps} (fine if these are optional/dlopen-only - not copied)")
endif()

# Matched by basename; these live in the system multiarch directory, not the prefixes above
set(_allowlistedSystemLibs
    "^libjpeg\\.so"
    "^libpng16\\.so"
    "^libtiff\\.so"
    "^libwebp\\.so"
    "^libwebpdemux\\.so"
    "^libwebpmux\\.so"
    "^libx264\\.so"
    "^libLerc\\.so"
    "^libdeflate\\.so"
    "^libjbig\\.so"
    "^libsharpyuv\\.so"
)

foreach(_dep ${_resolvedDeps})
    get_filename_component(_depName "${_dep}" NAME)
    set(_shouldCopy FALSE)
    if(_dep MATCHES "^/usr/local/" OR _dep MATCHES "^/opt/lumenvision-ffmpeg/")
        set(_shouldCopy TRUE)
    else()
        foreach(_pattern ${_allowlistedSystemLibs})
            if(_depName MATCHES "${_pattern}")
                set(_shouldCopy TRUE)
            endif()
        endforeach()
    endif()
    if(_shouldCopy)
        file(COPY "${_dep}" DESTINATION "${DEST_DIR}" FOLLOW_SYMLINK_CHAIN)
    endif()
endforeach()
