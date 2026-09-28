#[[
  ffmpeg (libavcodec/libavformat/libavutil/libswscale) for WebRTCSink, RecordSink and CodecStereoBackend's
  CS_ENABLE_LAVC path, found via pkg-config on every platform (vcpkg's toolchain sets PKG_CONFIG_PATH on Windows).

  The default encoder (libx264) needs an ffmpeg built with the gpl and x264 features; libx264 is GPL,
  so binaries linking it are GPL-encumbered.

  Provides:
    Lumen::ffmpeg - INTERFACE target aggregating avcodec/avformat/avutil/swscale
    LUMEN_FFMPEG_FOUND
]]

find_package(PkgConfig REQUIRED)

# Prefer the dedicated ffmpeg-rockchip prefix (h264_rkmpp encoder, rkrga filters) when present; it is
# outside ldconfig's path so the apt libavcodec SONAME cannot shadow it, hence the rpath below
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/opt/lumenvision-ffmpeg/lib/pkgconfig")
    set(LUMEN_FFMPEG_DEDICATED_PREFIX "/opt/lumenvision-ffmpeg")
    set(ENV{PKG_CONFIG_PATH} "${LUMEN_FFMPEG_DEDICATED_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    message(STATUS "Using ffmpeg-rockchip (h264_rkmpp encoder + rkrga filters) from ${LUMEN_FFMPEG_DEDICATED_PREFIX}")
endif()

pkg_check_modules(LUMEN_AVCODEC  IMPORTED_TARGET libavcodec)
pkg_check_modules(LUMEN_AVFORMAT IMPORTED_TARGET libavformat)
pkg_check_modules(LUMEN_AVUTIL   IMPORTED_TARGET libavutil)
pkg_check_modules(LUMEN_SWSCALE  IMPORTED_TARGET libswscale)

# pkg-config carries no runtime .dll location on Windows; glob the DLLs from the bin/ sibling of lib/
set(LUMEN_FFMPEG_RUNTIME_DLLS "")
if(WIN32 AND LUMEN_AVCODEC_LIBRARY_DIRS)
    list(GET LUMEN_AVCODEC_LIBRARY_DIRS 0 _lumen_ffmpeg_lib_dir)
    get_filename_component(_lumen_ffmpeg_root "${_lumen_ffmpeg_lib_dir}" DIRECTORY)
    file(GLOB LUMEN_FFMPEG_RUNTIME_DLLS "${_lumen_ffmpeg_root}/bin/av*.dll" "${_lumen_ffmpeg_root}/bin/sw*.dll")
endif()

set(LUMEN_FFMPEG_FOUND FALSE)
if(LUMEN_AVCODEC_FOUND AND LUMEN_AVFORMAT_FOUND AND LUMEN_AVUTIL_FOUND AND LUMEN_SWSCALE_FOUND)
    set(LUMEN_FFMPEG_FOUND TRUE)
    if(NOT TARGET Lumen::ffmpeg)
        add_library(Lumen::ffmpeg INTERFACE IMPORTED)
        target_link_libraries(Lumen::ffmpeg INTERFACE
            PkgConfig::LUMEN_AVCODEC PkgConfig::LUMEN_AVFORMAT
            PkgConfig::LUMEN_AVUTIL PkgConfig::LUMEN_SWSCALE
        )
        if(LUMEN_FFMPEG_DEDICATED_PREFIX)
            # Runtime search path for the dedicated prefix, which is outside ldconfig's default path
            target_link_options(Lumen::ffmpeg INTERFACE "-Wl,-rpath,${LUMEN_FFMPEG_DEDICATED_PREFIX}/lib")
        endif()
    endif()
elseif(LUMEN_WITH_WEBRTC OR LUMEN_WITH_RECORD OR LUMEN_WITH_CODEC_STEREO)
    message(WARNING
        "ffmpeg (libavcodec/libavformat/libavutil/libswscale) not found via pkg-config, but "
        "LUMEN_WITH_WEBRTC, LUMEN_WITH_RECORD, or LUMEN_WITH_CODEC_STEREO's CS_ENABLE_LAVC path needs it.")
endif()
