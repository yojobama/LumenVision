#!/usr/bin/env bash
#
# LumenVision dependency installer.
#
# Run this ON THE MACHINE THAT COMPILES LumenCore: inside the WSL2 "Ubuntu" distro for the local
# dev inner loop, or over SSH on the Orange Pi for the ARM64 build. This script only prepares
# the machine's system packages and from-source/prebuilt third-party dependencies; the actual
# build is driven by CMakePresets.json's wsl-x64-*/pi-arm64-* presets (`cmake --build --preset
# ...`), not by this script.
#
# Two distros are supported, detected via /etc/os-release rather than assumed, since their
# package sets genuinely differ (Ubuntu's 64-bit-time_t package-name suffixes, Debian's lack of
# PPAs, etc - see the ID-specific branches below):
#   - Ubuntu 24.04 (WSL "Ubuntu")
#   - Debian 13 "trixie" (the Orange Pi 5 Plus's Armbian/PhotonVision image)
# Anything else fails loudly at the top rather than silently hitting a wrong package name deep
# into the run. Behaviour that differs by CPU architecture is separately gated on `uname -m`.
#
# Usage:
#   ./install-deps.sh [--check] [--jobs N] [--skip-opencv] [--with-webrtc] [--with-nt4] [--with-rknn]
#
#   --check         verify what's installed/built and report what's missing; installs nothing
#   --jobs N        parallelism for from-source builds (default: nproc)
#   --skip-opencv   skip the OpenCV 5.0 source build (useful once it's already built and cached)
#   --with-webrtc   also build libdatachannel (phase 6 prerequisite; off by default, it's slow)
#   --with-nt4      also fetch ntcore/wpiutil (phase 3 prerequisite)
#   --with-rknn     also fetch the RKNN runtime (phase 6 prerequisite; aarch64 only, off by
#                   default - LUMEN_WITH_RKNN guards no code yet, so fetching it unconditionally
#                   on every Pi run was pure waste)
#   --with-mpp      also build the Rockchip MPP (VPU) userspace library from source, and install
#                   librga (2D accel - a prebuilt aarch64 binary, not a source build) alongside
#                   it (phase 6 prerequisite for codec-stereo's CS_ENABLE_RKMPP_HWENC and for
#                   --with-ffmpeg-rockchip below; aarch64 only, off by default - MPP is a real
#                   from-source build, not a quick fetch). Also (unconditionally, on aarch64,
#                   independent of this flag): fixes /dev/mpp_service, /dev/dma_heap/*, /dev/rga
#                   group ownership via a udev rule - confirmed the hard way these ship
#                   root-only (crw-------) on this board's image, which fails MPP init with an
#                   opaque "open vcodec_service ... failed" rather than a permissions error a
#                   user would recognise.
#   --with-ffmpeg-rockchip
#                   also build nyanmisaka/ffmpeg-rockchip from source - NOT upstream FFmpeg's own
#                   --enable-rkmpp, which is decode-only (confirmed the hard way: it ships
#                   rkmppdec.c but no encoder). This is what actually provides the h264_rkmpp
#                   ENCODER WebRTCSink can select, plus the rkrga scale/overlay filters. Requires
#                   --with-mpp to have run first (or a prior run's results already installed).
#                   Installed to its own prefix (/opt/lumenvision-ffmpeg), not /usr/local -
#                   confirmed the hard way that Debian's multiarch ldconfig prioritises
#                   /usr/lib/aarch64-linux-gnu (the apt-installed ffmpeg-dev package) over
#                   /usr/local/lib for a duplicate SONAME, so a /usr/local install would silently
#                   lose the race and run the wrong (non-rkmpp) library at runtime with no error.
#                   aarch64 only, off by default - it's the heaviest build in this script.
#
set -euo pipefail

# ---------------------------------------------------------------------------
# argument parsing
# ---------------------------------------------------------------------------
CHECK_ONLY=0
JOBS="$(nproc 2>/dev/null || echo 4)"
SKIP_OPENCV=0
WITH_WEBRTC=0
WITH_NT4=0
WITH_RKNN=0
WITH_MPP=0
WITH_FFMPEG_ROCKCHIP=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check) CHECK_ONLY=1; shift ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --skip-opencv) SKIP_OPENCV=1; shift ;;
        --with-webrtc) WITH_WEBRTC=1; shift ;;
        --with-nt4) WITH_NT4=1; shift ;;
        --with-rknn) WITH_RKNN=1; shift ;;
        --with-mpp) WITH_MPP=1; shift ;;
        --with-ffmpeg-rockchip) WITH_FFMPEG_ROCKCHIP=1; shift ;;
        -h|--help)
            grep '^#' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

ARCH="$(uname -m)"           # x86_64 or aarch64
PREFIX=/usr/local

# ---------------------------------------------------------------------------
# distro detection
# ---------------------------------------------------------------------------
# Package names and availability (t64-suffixed Ubuntu packages, PPAs, ...) genuinely differ
# between the two supported distros - detect explicitly and fail loudly on anything else rather
# than let an unrecognised distro hit a wrong package name deep into a from-source build.
if [[ ! -r /etc/os-release ]]; then
    echo "xx  /etc/os-release not found - can't detect distro; this script supports Ubuntu 24.04 and Debian 13 (trixie) only" >&2
    exit 1
fi
# shellcheck disable=SC1091
. /etc/os-release
case "${ID:-}" in
    ubuntu) DISTRO_ID=ubuntu ;;
    debian) DISTRO_ID=debian ;;
    *)
        echo "xx  Unsupported distro '${ID:-unknown}' (${PRETTY_NAME:-no PRETTY_NAME}) - this script supports Ubuntu 24.04 and Debian 13 (trixie) only" >&2
        exit 1
        ;;
esac
BUILD_ROOT="${LUMEN_BUILD_ROOT:-${FRCV_BUILD_ROOT:-$HOME/.lumen-build}}"
if [[ -n "${FRCV_BUILD_ROOT:-}" ]]; then
    printf '\033[1;33m!!  %s\033[0m\n' "FRCV_BUILD_ROOT is deprecated - use LUMEN_BUILD_ROOT" >&2
fi
# Migrate an existing ~/.frcv-build cache rather than rebuilding everything from scratch under
# the new default name. A plain `mv` is NOT enough on its own: most of what's under here is
# CMake build trees (opencv-5.0.0/build/CMakeCache.txt and friends), and those embed absolute
# source/binary paths - not relocatable. The symlink left behind at the old path is what keeps
# those absolute paths resolving, so a stale build tree doesn't silently trigger a full rebuild
# the next time this script (or anything else still pointed at the old path) runs.
if [[ ! -e "$BUILD_ROOT" && -d "$HOME/.frcv-build" && ! -L "$HOME/.frcv-build" ]]; then
    printf '\n\033[1;36m==> %s\033[0m\n' "migrating dependency cache: $HOME/.frcv-build -> $BUILD_ROOT"
    mv "$HOME/.frcv-build" "$BUILD_ROOT"
    ln -s "$BUILD_ROOT" "$HOME/.frcv-build"
fi
mkdir -p "$BUILD_ROOT"

log()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m!!  %s\033[0m\n' "$*" >&2; }
fail() { printf '\033[1;31mxx  %s\033[0m\n' "$*" >&2; }

MISSING=()
note_missing() { MISSING+=("$1"); warn "missing: $1"; }

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        note_missing "command: $1 (package hint: ${2:-$1})"
        return 1
    fi
    return 0
}

require_pkgconfig() {
    if ! pkg-config --exists "$1" 2>/dev/null; then
        note_missing "pkg-config module: $1"
        return 1
    fi
    return 0
}

require_header() {
    if [[ ! -f "$1" ]]; then
        note_missing "header: $1"
        return 1
    fi
    return 0
}

apt_install() {
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        for p in "$@"; do
            dpkg -s "$p" >/dev/null 2>&1 || note_missing "apt package: $p"
        done
        return 0
    fi
    # try the whole batch first (fast path); if apt rejects it (e.g. one bad/renamed package
    # name), fall back to installing one at a time so a single typo doesn't sink everything else
    if ! sudo apt-get install -y "$@"; then
        warn "batch install failed, retrying package-by-package: $*"
        local failed=()
        for p in "$@"; do
            sudo apt-get install -y "$p" || failed+=("$p")
        done
        if [[ "${#failed[@]}" -gt 0 ]]; then
            for p in "${failed[@]}"; do note_missing "apt package: $p (install failed)"; done
        fi
    fi
}

# ---------------------------------------------------------------------------
# 1. base packages + Visual Studio remote toolchain requirements
# ---------------------------------------------------------------------------
# The Orange Pi image is a SERVER image with no desktop — SSH is the only way in, so verifying
# it works is step zero, not an afterthought.
install_base() {
    log "Base packages + Visual Studio remote toolchain requirements"
    if [[ "$CHECK_ONLY" -eq 0 ]]; then
        sudo apt-get update
    fi
    apt_install \
        build-essential gcc g++ gdb gdbserver make ninja-build cmake \
        openssh-server rsync zip unzip tar git curl pkg-config \
        swig nlohmann-json3-dev

    if [[ "$CHECK_ONLY" -eq 0 ]]; then
        sudo systemctl enable --now ssh || warn "could not enable sshd — is this a container without systemd?"
    fi
    # `|| true`: these are meant to note-and-continue like every other check in this script, but
    # unlike the ones elsewhere that only ever run inside an `if` (which set -e already excuses),
    # these are bare statements - under `set -e`, require_cmd's own `return 1` on a missing
    # command would otherwise abort the ENTIRE script right here, so --check never got past the
    # first missing command and reported nothing else. Confirmed the hard way running this
    # against a freshly-imaged board with none of these installed yet.
    require_cmd gcc || true
    require_cmd g++ || true
    require_cmd gdbserver gdbserver || true
    require_cmd swig swig || true
    require_cmd rsync || true

    # mDNS hostname (lumenvision.local) so teams don't have to chase the Pi's DHCP-assigned IP -
    # ubuntu-rockchip ships avahi-daemon already, but enable it explicitly rather than assume so
    apt_install avahi-daemon
    if [[ "$CHECK_ONLY" -eq 0 ]]; then
        sudo systemctl enable --now avahi-daemon || warn "could not enable avahi-daemon"
    fi
}

# ---------------------------------------------------------------------------
# 2. OpenCV 5.0, built from source with opencv_contrib
# ---------------------------------------------------------------------------
# Deliberately NOT the distro package (24.04 ships 4.6, and it's the wrong major version
# anyway) — built from source into /usr/local so it matches on both the WSL dev box and the Pi.
# opencv_contrib supplies the ArUco module needed for ChArUco calibration boards later.
OPENCV_VERSION="5.0.0"

build_opencv() {
    if [[ "$SKIP_OPENCV" -eq 1 ]]; then
        log "Skipping OpenCV build (--skip-opencv)"
        return 0
    fi

    log "OpenCV ${OPENCV_VERSION} (from source, with opencv_contrib)"

    # OpenCV 5.0 renamed both its pkg-config module and its header install directory from
    # opencv4 to opencv5 (confirmed by actually building it: headers land under
    # /usr/local/include/opencv5, module is `opencv5`, NOT `opencv4`) - if this ever changes
    # again in a later 5.x release, this is the line to update, along with LumenCore.vcxproj's
    # AdditionalIncludeDirectories.
    if pkg-config --exists opencv5 2>/dev/null; then
        local installed_ver
        installed_ver="$(pkg-config --modversion opencv5)"
        if [[ "$installed_ver" == "$OPENCV_VERSION"* ]]; then
            log "OpenCV ${installed_ver} already installed at the requested version, skipping build"
            return 0
        else
            warn "found OpenCV ${installed_ver} via pkg-config; expected ${OPENCV_VERSION}.x — rebuilding"
        fi
    fi

    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "OpenCV ${OPENCV_VERSION} (opencv5.pc not found or wrong version)"
        return 0
    fi

    # purge the distro's OpenCV -dev packages first: they install headers under /usr/include
    # rather than /usr/local/include, but leaving them in place is still a footgun - CMake's own
    # find_package(OpenCV) search order can't be trusted to always prefer the from-source install
    # over a same-named distro one, and apt could reinstall these as a dependency of something
    # else later. Purging removes the ambiguity entirely instead of relying on search-order
    # discipline. 'libopencv-*t64' is an Ubuntu-only glob (its 64-bit-time_t package-name
    # transition, no Debian equivalent) - only add it on Ubuntu, since passing a glob that
    # matches nothing on Debian is harmless but noisy.
    if dpkg -s libopencv-core-dev >/dev/null 2>&1; then
        warn "purging distro OpenCV -dev packages to avoid a stale /usr/include/opencv4 shadowing this build"
        local opencv_purge_globs=('libopencv-*-dev')
        [[ "$DISTRO_ID" == "ubuntu" ]] && opencv_purge_globs+=('libopencv-*t64')
        sudo apt-get purge -y "${opencv_purge_globs[@]}" || true
    fi

    # No libgtk-3-dev/python3-dev/python3-numpy: both machines this script targets are headless
    # servers (WSL has no display server either), and Python bindings are already off below
    # (-DBUILD_opencv_python3=OFF) - GTK/Python dev headers here would be pure dead weight, not
    # something the build ever uses.
    #
    # FFmpeg: when the dedicated ffmpeg-rockchip prefix exists (fetch_ffmpeg_rockchip runs before
    # this function in main, specifically so this check can see it), point OpenCV's own
    # -DWITH_FFMPEG=ON pkg-config auto-detection at THAT instead of apt's system ffmpeg-dev
    # packages - confirmed the hard way on real hardware: OpenCV linking against the CI runner's
    # apt-installed libavcodec-dev (SONAME 60 on ubuntu-24.04-arm) produced a shipped .deb that
    # crash-looped on a real Orange Pi with "libavcodec.so.60: cannot open shared object file" -
    # Debian 13 trixie ships a DIFFERENT ffmpeg (SONAME 61, matching ffmpeg-rockchip's own, but
    # simply never bundled since apt-resolved system libs are deliberately excluded from
    # CopyLinuxRuntimeDeps.cmake's copy - see that file's own comment on why). Using the same
    # ffmpeg-rockchip build project-wide means there's only ever one ffmpeg to bundle, already
    # handled correctly, with no cross-distro SONAME mismatch possible. Falls back to apt's system
    # ffmpeg-dev packages when the dedicated prefix doesn't exist (non-aarch64 dev/CI machines,
    # or --with-ffmpeg-rockchip wasn't requested) - OpenCV still needs SOME ffmpeg there.
    local opencv_pkg_config_path=""
    if [[ -d "$LUMEN_FFMPEG_PREFIX/lib/pkgconfig" ]]; then
        log "OpenCV: using ffmpeg-rockchip at $LUMEN_FFMPEG_PREFIX for -DWITH_FFMPEG=ON (not apt's system ffmpeg)"
        opencv_pkg_config_path="$LUMEN_FFMPEG_PREFIX/lib/pkgconfig"
        apt_install libv4l-dev libtbb-dev libjpeg-dev libpng-dev libtiff-dev
    else
        apt_install \
            libavcodec-dev libavformat-dev libswscale-dev libv4l-dev \
            libtbb-dev libjpeg-dev libpng-dev libtiff-dev
    fi

    local src="$BUILD_ROOT/opencv-${OPENCV_VERSION}"
    if [[ ! -d "$src" ]]; then
        git clone --branch "${OPENCV_VERSION}" --depth 1 https://github.com/opencv/opencv.git "$src"
        git clone --branch "${OPENCV_VERSION}" --depth 1 https://github.com/opencv/opencv_contrib.git "$src-contrib"
    fi

    mkdir -p "$src/build"
    (
        cd "$src/build"
        PKG_CONFIG_PATH="${opencv_pkg_config_path:+$opencv_pkg_config_path:}${PKG_CONFIG_PATH:-}" \
        cmake -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$PREFIX" \
            -DOPENCV_EXTRA_MODULES_PATH="$src-contrib/modules" \
            -DBUILD_opencv_aruco=ON \
            -DBUILD_TESTS=OFF \
            -DBUILD_PERF_TESTS=OFF \
            -DBUILD_EXAMPLES=OFF \
            -DBUILD_opencv_python3=OFF \
            -DBUILD_opencv_java=OFF \
            -DBUILD_opencv_java_bindings_generator=OFF \
            -DOPENCV_GENERATE_PKGCONFIG=ON \
            -DWITH_FFMPEG=ON \
            -DWITH_GTK=OFF \
            -DBUILD_opencv_highgui=OFF \
            ..
        cmake --build . --parallel "$JOBS"
        sudo cmake --install .
        sudo ldconfig
    )
    log "OpenCV ${OPENCV_VERSION} installed to ${PREFIX}"
}

# ---------------------------------------------------------------------------
# 3. AprilTag (AprilRobotics)
# ---------------------------------------------------------------------------
# Ubuntu 24.04's libapriltag-dev (3.3.0) does ship apriltag_pose.h — verified against a live
# 24.04 install, contrary to what older Ubuntu releases shipped. Prefer the apt package; only
# fall back to building from source if apriltag_pose.h turns out to be missing (e.g. an older
# base image, or a future package that drops it again).
# AprilTag: built from the SAME patched v3.4.5 source vkapriltag's own CMake fetches
# (cmake/patches/apriltag-expose-decode-steps.patch, applied against AprilRobotics/apriltag
# v3.4.5), installed as the system's only apriltag - not apt's package, and not a second,
# separately-built copy. This matters because both this build and vkapriltag's own FetchContent
# build produce a shared library with the SAME SONAME (libapriltag.so.3) regardless of the
# 3.3.0-vs-3.4.5 version difference: confirmed by actually building both and checking. Whichever
# libapriltag.so.3 the dynamic linker resolves at runtime is used by BOTH the CPU AprilTag
# backend and vkapriltag's VkApriltagBackend - and only the patched build exports the two
# symbols (quad_decode_index, reconcile_detections) VkApriltagBackend needs. So there must be
# exactly one apriltag in the system, and it must be this patched one; apt's package and a
# vanilla source build are both wrong for this project once LUMEN_WITH_VULKAN_APRILTAG is in play.
APRILTAG_TAG="${APRILTAG_TAG:-v3.4.5}"

build_apriltag() {
    log "AprilTag ${APRILTAG_TAG} (patched for vkapriltag)"

    local patch_file="$BUILD_ROOT/../third_party/vkapriltag/apriltags_vulkan/cmake/patches/apriltag-expose-decode-steps.patch"
    # resolve relative to this script's location too, in case BUILD_ROOT isn't under the repo
    if [[ ! -f "$patch_file" ]]; then
        patch_file="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/third_party/vkapriltag/apriltags_vulkan/cmake/patches/apriltag-expose-decode-steps.patch"
    fi
    if [[ ! -f "$patch_file" ]]; then
        fail "can't find vkapriltag's apriltag patch file - is the third_party/vkapriltag submodule checked out? (git submodule update --init)"
        return 1
    fi

    # The installed build is stamped with the SHA-256 of the vkapriltag patch it was built with,
    # and rebuilt whenever that patch changes. A header check alone can't tell a current build
    # from a stale one: the patch only adds exported *symbols*, never headers - confirmed the
    # hard way when bumping the vkapriltag submodule, whose newer patch also exports
    # refine_edges(): every machine that had built apriltag with the older patch (and every
    # restored CI dependency cache) kept it, and LumenCore failed to link with "undefined
    # reference to refine_edges".
    # next to apriltag's own installed CMake package files - under /usr/local/lib, which the CI
    # dependency caches (ci.yml/release.yml) persist, so a cache hit keeps the stamp too
    local stamp="$PREFIX/lib/apriltag/lumenvision-patch.sha256"
    local want_sha
    want_sha="$(sha256sum "$patch_file" | cut -d' ' -f1)"
    if require_header /usr/local/include/apriltag/apriltag_pose.h 2>/dev/null \
        && [[ -f "$stamp" && "$(cat "$stamp")" == "$want_sha" ]]; then
        log "apriltag already installed under /usr/local (built with the current vkapriltag patch)"
        return 0
    fi

    # purge only the packages that are actually installed - `apt-get purge` aborts the WHOLE
    # command over one unknown package name (confirmed: an earlier version of this listed
    # libapriltag-utils3t64, which doesn't exist, and that silently left both real packages
    # in place because of the trailing `|| true`). Checking both the t64-suffixed (Ubuntu 24.04's
    # 64-bit-time_t transition) and plain (Debian) names costs nothing - dpkg -s just reports
    # not-installed for whichever one doesn't apply on this distro.
    local installed_apriltag_pkgs=()
    for pkg in libapriltag-dev libapriltag3t64 libapriltag-utils3t64 libapriltag3 libapriltag-utils3; do
        dpkg -s "$pkg" >/dev/null 2>&1 && installed_apriltag_pkgs+=("$pkg")
    done
    if [[ "${#installed_apriltag_pkgs[@]}" -gt 0 ]]; then
        warn "purging apt's apriltag package(s) [${installed_apriltag_pkgs[*]}] - it would collide (same SONAME, older/unpatched) with the patched build this project needs"
        sudo apt-get purge -y "${installed_apriltag_pkgs[@]}"
    fi

    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "AprilTag ${APRILTAG_TAG} (patched) not installed, or built with an older vkapriltag patch"
        return 0
    fi

    local src="$BUILD_ROOT/apriltag-patched"
    if [[ ! -d "$src" ]]; then
        git clone --branch "$APRILTAG_TAG" --depth 1 https://github.com/AprilRobotics/apriltag.git "$src"
    fi
    (
        cd "$src"
        # back to the pristine tag before patching - this is the script's own scratch clone, and
        # an OLDER version of the patch may already be applied in it, which the new one won't
        # apply on top of
        git reset --hard -q
        git clean -fdq -e build
        # CRLF-stripped: a Windows checkout (core.autocrlf, e.g. running this under WSL on a
        # /mnt/c working tree) gives the .patch CRLF endings, which git apply rejects against the
        # LF-only fresh clone ("patch does not apply") - confirmed the hard way
        git apply <(sed 's/\r$//' "$patch_file")
    )
    mkdir -p "$src/build"
    (
        cd "$src/build"
        cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" ..
        cmake --build . --parallel "$JOBS"
        sudo cmake --install .
        sudo ldconfig
    )
    sudo mkdir -p "$(dirname "$stamp")"
    echo "$want_sha" | sudo tee "$stamp" >/dev/null
}

# ---------------------------------------------------------------------------
# 4. FFmpeg dev headers (+ rkmpp hardware encoder on aarch64)
# ---------------------------------------------------------------------------
install_ffmpeg() {
    log "FFmpeg development headers"
    apt_install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev

    if [[ "$ARCH" == "aarch64" ]]; then
        log "Checking for RK3588 hardware video (MPP/RGA) support (phase 6)"
        # PPAs are an Ubuntu/Launchpad mechanism with no Debian equivalent - probe capabilities
        # directly (the device nodes, the userspace library's pkg-config file, the ffmpeg
        # encoder list) instead of checking for one specific distro's repo being enabled.
        if [[ -e /dev/mpp_service ]]; then
            log "/dev/mpp_service present"
        else
            note_missing "/dev/mpp_service (RK3588 MPP video codec device node - check the vendor kernel/dtb is in use)"
        fi
        if [[ -e /dev/rga ]]; then
            log "/dev/rga present"
        else
            note_missing "/dev/rga (RK3588 2D scale/crop/colour-convert device node)"
        fi
        if pkg-config --exists rockchip_mpp 2>/dev/null; then
            log "rockchip_mpp userspace library present"
        else
            local mpp_hint="the Armbian/PhotonVision image's own apt repo"
            [[ "$DISTRO_ID" == "ubuntu" ]] && mpp_hint="the rockchip-multimedia PPA (apt-cache policy ffmpeg)"
            note_missing "rockchip_mpp userspace library not found via pkg-config - check $mpp_hint has it, or run install-deps.sh --with-mpp to build it from https://github.com/rockchip-linux/mpp"
        fi
        # the system/apt ffmpeg never has this - upstream FFmpeg's own --enable-rkmpp is
        # decode-only (confirmed the hard way: ships rkmppdec.c, no encoder). Only
        # nyanmisaka/ffmpeg-rockchip (install-deps.sh --with-ffmpeg-rockchip) provides it,
        # installed to its own dedicated prefix - see fetch_ffmpeg_rockchip's own comment for why
        # not $PREFIX.
        # captured into a variable first, not piped straight into `grep -q` - under `set -o
        # pipefail` (active for this whole script), ffmpeg's own exit code (non-zero for an
        # info-only invocation with no actual transcode - or SIGPIPE once grep -q closes its
        # stdin early after matching) fails the WHOLE pipeline even when grep found a real match.
        # Confirmed the hard way: this exact one-liner silently reported h264_rkmpp missing when
        # it was genuinely installed and working.
        local ffmpeg_encoders=""
        if [[ -x "$LUMEN_FFMPEG_PREFIX/bin/ffmpeg" ]]; then
            ffmpeg_encoders="$(LD_LIBRARY_PATH="$LUMEN_FFMPEG_PREFIX/lib" "$LUMEN_FFMPEG_PREFIX/bin/ffmpeg" -hide_banner -encoders 2>/dev/null || true)"
        fi
        if [[ -n "$ffmpeg_encoders" ]] && grep -q h264_rkmpp <<< "$ffmpeg_encoders"; then
            log "h264_rkmpp encoder present ($LUMEN_FFMPEG_PREFIX)"
        else
            note_missing "ffmpeg-rockchip with h264_rkmpp encoder - run install-deps.sh --with-mpp --with-ffmpeg-rockchip"
        fi
        for grp in video render; do
            if ! id -nG "$USER" | grep -qw "$grp"; then
                note_missing "user '$USER' in group '$grp' (needed for /dev/mpp_service access)"
                [[ "$CHECK_ONLY" -eq 0 ]] && sudo usermod -aG "$grp" "$USER" && \
                    warn "added $USER to $grp — log out/in (or reboot) for it to take effect"
            fi
        done
    fi
}

# ---------------------------------------------------------------------------
# 5. Vulkan (phase 5, vkapriltag) — on aarch64 this must resolve to the image's libmali blob
# ---------------------------------------------------------------------------
install_vulkan() {
    log "Vulkan development packages"
    # glslc (from the separate `glslc` package, not glslang-tools) is required by vkapriltag's
    # own CMakeLists.txt (find_package(Vulkan COMPONENTS glslangValidator glslc)) - without it,
    # configuring the submodule fails outright ("missing components: glslc"). Confirmed the hard
    # way: glslang-tools alone (which provides glslangValidator) is not enough.
    apt_install libvulkan-dev vulkan-tools glslang-tools spirv-tools glslc

    if [[ "$ARCH" != "aarch64" ]]; then
        return 0
    fi

    log "Checking Vulkan ICD (expecting the board's bundled libmali, not panfrost/panvk)"

    # The registered ICD filename is genuinely per-image: the ubuntu-rockchip image (this
    # script's original target) registers none at all, so one gets written here as
    # libmali-gbm.json; the Armbian/PhotonVision image (the real bench board) already ships one
    # at a different name (mali.json), pointing at a libmali variant this glob wouldn't even
    # find under the same search path. Discover whichever is actually registered and use THAT
    # path everywhere below, rather than assuming this script's own guessed filename - confirmed
    # the hard way (lumenvision.service hardcoding libmali-gbm.json, which doesn't exist on the
    # real board, so Vulkan AprilTag worked run by hand and silently fell back to CPU under
    # systemd).
    local icd_dir=/usr/share/vulkan/icd.d
    local icd_json=""

    if [[ -d "$icd_dir" ]]; then
        icd_json="$(find "$icd_dir" -maxdepth 1 -iname '*.json' 2>/dev/null | head -1)"
    fi

    if [[ -n "$icd_json" ]]; then
        log "Vulkan ICD(s) already registered:"
        ls "$icd_dir"/*.json
    else
        # Nothing registered yet (the ubuntu-rockchip case): several libmali*.so variants exist
        # (x11, wayland-gbm, with/without vulkan) but none are wired into an ICD file. On a
        # headless SERVER image the only variant that can plausibly init without a display
        # server is "wayland-gbm" (GBM talks to the kernel DRM/GBM API directly, no compositor
        # needed) - and of those, only the one with "-vulkan" in its name implements the Vulkan
        # ICD entry points; plain "-wayland-gbm" ones are OpenGL/EGL only.
        local mali_lib
        mali_lib="$(find /usr/lib/aarch64-linux-gnu -maxdepth 1 -iname 'libmali-*wayland-gbm*vulkan*.so' 2>/dev/null | head -1)"
        local candidate_json="$icd_dir/libmali-gbm.json"

        if [[ -n "$mali_lib" ]]; then
            if [[ "$CHECK_ONLY" -eq 1 ]]; then
                note_missing "Vulkan ICD not registered (would write $candidate_json -> $mali_lib)"
            else
                log "No Vulkan ICD registered yet; writing one for $mali_lib"
                sudo mkdir -p "$icd_dir"
                sudo tee "$candidate_json" >/dev/null <<EOF
{
    "file_format_version": "1.0.0",
    "ICD": {
        "library_path": "$mali_lib",
        "api_version": "1.2.0"
    }
}
EOF
                icd_json="$candidate_json"
            fi
        else
            note_missing "libmali wayland-gbm+vulkan .so not found under /usr/lib/aarch64-linux-gnu — is libmali actually installed on this image?"
        fi
    fi

    if command -v vulkaninfo >/dev/null 2>&1 && [[ "$CHECK_ONLY" -eq 0 && -n "$icd_json" ]]; then
        log "Running vulkaninfo --summary (expect a Mali device with a VK_QUEUE_COMPUTE_BIT queue family)"
        VK_ICD_FILENAMES="$icd_json" vulkaninfo --summary 2>&1 | tee "$BUILD_ROOT/vulkaninfo-summary.log" || \
            warn "vulkaninfo failed even headless with the discovered ICD ($icd_json) — see docs/history/IMPLEMENTATION_PLAN.md phase 5 item 0; the CPU AprilTag backend remains the fallback"
    elif ! command -v vulkaninfo >/dev/null 2>&1; then
        note_missing "vulkaninfo (from vulkan-tools)"
    fi

    if [[ -n "$icd_json" && "$CHECK_ONLY" -eq 0 ]]; then
        # A systemd drop-in, not a hand-edit of the tracked lumenvision.service: this makes the
        # unit correct on THIS board regardless of what's hardcoded in the tracked file (which
        # can now only ever be a same-image-as-last-time default, never a cross-image guarantee)
        # - a later Environment= wins over an earlier one for the same key, so this always takes
        # precedence over lumenvision.service's own line once deploy.ps1 installs the unit.
        local dropin_dir=/etc/systemd/system/lumenvision.service.d
        log "Writing systemd drop-in: $dropin_dir/10-vulkan-icd.conf -> VK_ICD_FILENAMES=$icd_json"
        sudo mkdir -p "$dropin_dir"
        sudo tee "$dropin_dir/10-vulkan-icd.conf" >/dev/null <<EOF
# Generated by install-deps.sh - discovers the real registered Vulkan ICD on THIS board rather
# than trusting lumenvision.service's own hardcoded default, which is only ever correct for
# whichever image that file was last hand-verified against. Re-run install-deps.sh to refresh
# this if the board's image changes.
[Service]
Environment=VK_ICD_FILENAMES=$icd_json
EOF
        if systemctl is-enabled lumenvision.service >/dev/null 2>&1 || systemctl list-unit-files lumenvision.service >/dev/null 2>&1; then
            sudo systemctl daemon-reload
        fi
    fi
}

# ---------------------------------------------------------------------------
# 5b. vkapriltag (phase 5) — builds the third_party/vkapriltag submodule's static library.
# ---------------------------------------------------------------------------
# vkapriltag statically links its own patched AprilRobotics/apriltag v3.4.5 fetch, which
# produces a shared library with the SAME SONAME (libapriltag.so.3) as any other apriltag
# build - confirmed by actually building both. build_apriltag() above installs exactly that
# patched build as the system's only /usr/local apriltag for this reason, so this function
# builds vkapriltag itself, letting FetchContent grab its own private copy for the build only
# (that private copy is never installed or linked into LumenCore - only libvkapriltag.a is).
build_vkapriltag() {
    log "vkapriltag (Vulkan AprilTag detection submodule)"

    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    local src="$repo_root/third_party/vkapriltag/apriltags_vulkan"
    local build_dir="$src/build"
    local lib_path="$build_dir/library/libvkapriltag.a"

    if [[ ! -d "$src" ]]; then
        fail "third_party/vkapriltag submodule not found - run: git submodule update --init"
        return 1
    fi

    if [[ -f "$lib_path" ]]; then
        log "libvkapriltag.a already built"
        return 0
    fi
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "libvkapriltag.a not built yet ($lib_path)"
        return 0
    fi

    mkdir -p "$build_dir"
    (
        cd "$build_dir"
        # -fPIC: vkapriltag's own CMakeLists doesn't set POSITION_INDEPENDENT_CODE, but
        # libvkapriltag.a must go into LumenCore's shared library - confirmed the hard way
        # (`recompile with -fPIC` at final link time) before adding this.
        cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DVKAPRILTAG_BUILD_APPS=OFF -DVKAPRILTAG_BUILD_TOOLS=OFF ..
        cmake --build . --target vkapriltag --parallel "$JOBS"
    )

    if [[ ! -f "$lib_path" ]]; then
        fail "vkapriltag build finished but $lib_path is missing - something changed in its CMakeLists"
        return 1
    fi
}

# ---------------------------------------------------------------------------
# 5c. codec-stereo (phase 10) — builds the third_party/codec-stereo submodule's static library.
# ---------------------------------------------------------------------------
# Unlike --with-webrtc/--with-nt4, this runs unconditionally: its dependencies (a C compiler,
# libavcodec/libavutil/libavformat with libx264 encode support) are already required/installed
# for other reasons in this script, so there's no separate opt-in cost to gate behind a flag.
# See STEREO_IMPLEMENTATION_PLAN.md ss10.1 for the full rationale behind each cmake flag below.
build_codec_stereo() {
    log "codec-stereo (stereo depth via hardware video-encoder motion vectors)"

    local repo_root
    repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
    local src="$repo_root/third_party/codec-stereo"
    local build_dir="$src/build"
    local lib_path="$build_dir/libcodec_stereo.a"

    if [[ ! -d "$src" ]]; then
        fail "third_party/codec-stereo submodule not found - run: git submodule update --init"
        return 1
    fi

    if [[ -f "$lib_path" ]]; then
        log "libcodec_stereo.a already built"
    elif [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "libcodec_stereo.a not built yet ($lib_path)"
    else
        # CS_ENABLE_RKMPP (the buggy KEY_MOTION_INFO readback backend) is deliberately never
        # enabled here - see StereoDepthBackendKind.h. CS_ENABLE_RKMPP_HWENC (aarch64 only) reads
        # the same real bitstream lavc_sw already validates, sidestepping those defects entirely.
        local extra_flags=()
        if [[ "$ARCH" == "aarch64" ]]; then
            if pkg-config --exists rockchip_mpp 2>/dev/null; then
                extra_flags+=(-DCS_ENABLE_RKMPP_HWENC=ON)
            else
                warn "rockchip_mpp not found - building codec-stereo without CS_ENABLE_RKMPP_HWENC; the Pi's hardware backend will be unavailable until it's installed and this is rerun"
            fi
        fi

        mkdir -p "$build_dir"
        (
            cd "$build_dir"
            # -DCMAKE_POSITION_INDEPENDENT_CODE=ON: codec-stereo's own CMakeLists doesn't set
            # this (it's a default-STATIC add_library), and libcodec_stereo.a goes into
            # libLumenCore.so - confirmed the hard way (relocation R_X86_64_32S ... can not be
            # used when making a shared object) before adding this flag.
            #
            # -DCS_BUILD_HARNESS=OFF: its harness pkg-configs the system opencv4 package, which
            # collides with this project's own OpenCV 5.0 build under /usr/local (its own
            # CMakeLists carries a comment about exactly this) - not needed for LumenCore anyway.
            cmake -S .. -B . -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
                -DCS_BUILD_PIPELINE=ON -DCS_BUILD_TOOLS=ON -DCS_BUILD_TESTS=ON \
                -DCS_BUILD_HARNESS=OFF -DCS_ENABLE_REF_SAD=ON -DCS_ENABLE_LAVC=ON \
                "${extra_flags[@]}"
            cmake --build . --parallel "$JOBS"
        )

        if [[ ! -f "$lib_path" ]]; then
            fail "codec-stereo build finished but $lib_path is missing - something changed in its CMakeLists"
            return 1
        fi

        # cheap, genuine correctness check of the backend on the machine that will actually run
        # it - not a substitute for STEREO_IMPLEMENTATION_PLAN.md ss10.6's own verification plan,
        # but catches a broken build immediately rather than at first real use.
        ( cd "$build_dir" && ctest --output-on-failure ) || warn "codec-stereo's own test suite failed - see the log above"
    fi
}

# ---------------------------------------------------------------------------
# 6. WebRTC (phase 6) — libdatachannel, opt-in via --with-webrtc
# ---------------------------------------------------------------------------
build_webrtc() {
    [[ "$WITH_WEBRTC" -eq 1 ]] || return 0
    log "libdatachannel (WebRTC)"

    apt_install libssl-dev libsrtp2-dev

    if require_pkgconfig libdatachannel 2>/dev/null; then
        log "libdatachannel already installed"
        return 0
    fi
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "libdatachannel"
        return 0
    fi

    local src="$BUILD_ROOT/libdatachannel"
    if [[ ! -d "$src" ]]; then
        git clone --recursive --depth 1 https://github.com/paullouisageneau/libdatachannel.git "$src"
    fi
    mkdir -p "$src/build"
    (
        cd "$src/build"
        cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
            -DUSE_GNUTLS=OFF -DUSE_NICE=OFF ..
        cmake --build . --parallel "$JOBS"
        sudo cmake --install .
        sudo ldconfig
    )
}

# ---------------------------------------------------------------------------
# 7. NT4 (phase 3) — ntcore + wpiutil, opt-in via --with-nt4
# ---------------------------------------------------------------------------
# WPILib publishes prebuilt C++ artifacts to Maven; there's no apt package. Classifier naming
# below is best-effort against current WPILib conventions — verify against
# https://frcmaven.wpi.edu/ui/repos/tree/General/release/edu/wpi/first/ntcore/ntcore-cpp before
# relying on it; the maven-metadata.xml under that path lists the actual current version.
NTCORE_VERSION="${NTCORE_VERSION:-2025.3.2}"

fetch_ntcore() {
    [[ "$WITH_NT4" -eq 1 ]] || return 0
    log "ntcore + wpiutil ${NTCORE_VERSION}"

    if require_header /usr/local/include/ntcore_cpp.h 2>/dev/null; then
        log "ntcore already installed"
        return 0
    fi
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "ntcore (ntcore_cpp.h not found)"
        return 0
    fi

    local classifier
    case "$ARCH" in
        x86_64) classifier="linuxx86-64" ;;
        aarch64) classifier="linuxarm64" ;;
        *) fail "unsupported arch for ntcore: $ARCH"; return 1 ;;
    esac

    local base="https://frcmaven.wpi.edu/artifactory/release/edu/wpi/first"
    local dest="$BUILD_ROOT/ntcore"
    mkdir -p "$dest"
    # ntcore links against wpinet (confirmed via ldd - it's not just a wpiutil/ntcore pair)
    for artifact in ntcore/ntcore-cpp wpinet/wpinet-cpp wpiutil/wpiutil-cpp; do
        local name="${artifact#*/}"
        for kind in headers "${classifier}"; do
            local url="${base}/${artifact}/${NTCORE_VERSION}/${name}-${NTCORE_VERSION}-${kind}.zip"
            curl -fsSL "$url" -o "$dest/${name}-${kind}.zip" || {
                warn "download failed: $url — check the version/classifier against frcmaven.wpi.edu"
                continue
            }
            unzip -oq "$dest/${name}-${kind}.zip" -d "$dest/${name}-${kind}"
        done
    done
    sudo cp -r "$dest"/*headers*/* "$PREFIX/include/" 2>/dev/null || true
    # the shared libraries are nested (e.g. linux/x86-64/shared/libntcore.so), not at the zip
    # root, so a shallow glob here finds nothing - search recursively instead
    find "$dest" -path "*${classifier}*" \( -name '*.so' -o -name '*.so.*' \) -print0 | \
        xargs -0 -r sudo cp -t "$PREFIX/lib/"
    sudo ldconfig
}

# ---------------------------------------------------------------------------
# 8. ONNX Runtime — official prebuilt release, CPU execution provider only
# ---------------------------------------------------------------------------
ORT_VERSION="${ORT_VERSION:-1.20.0}"

fetch_onnxruntime() {
    log "ONNX Runtime ${ORT_VERSION} (official prebuilt, CPU EP)"

    if require_header /usr/local/include/onnxruntime_cxx_api.h 2>/dev/null; then
        log "onnxruntime already installed"
        return 0
    fi
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "onnxruntime (onnxruntime_cxx_api.h not found)"
        return 0
    fi

    local ort_arch
    case "$ARCH" in
        x86_64) ort_arch="x64" ;;
        aarch64) ort_arch="aarch64" ;;
        *) fail "unsupported arch for onnxruntime: $ARCH"; return 1 ;;
    esac

    local tarname="onnxruntime-linux-${ort_arch}-${ORT_VERSION}"
    local url="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${tarname}.tgz"
    local dest="$BUILD_ROOT/onnxruntime"
    mkdir -p "$dest"
    curl -fsSL "$url" -o "$dest/$tarname.tgz"
    tar -xzf "$dest/$tarname.tgz" -C "$dest"
    sudo cp -r "$dest/$tarname/include/"* "$PREFIX/include/"
    sudo cp -r "$dest/$tarname/lib/"* "$PREFIX/lib/"
    sudo ldconfig
}

# ---------------------------------------------------------------------------
# 9. aarch64-only: RKNN runtime (RK3588 NPU)
# ---------------------------------------------------------------------------
# librknnrt.so must match the kernel's rknpu driver version or rknn_init() fails with an opaque
# error — check both explicitly rather than assuming a fresh checkout is compatible.
RKNN_TOOLKIT2_REF="${RKNN_TOOLKIT2_REF:-master}"

fetch_rknn() {
    [[ "$WITH_RKNN" -eq 1 ]] || return 0
    [[ "$ARCH" == "aarch64" ]] || return 0
    log "RKNN runtime (rknn-toolkit2, ref=${RKNN_TOOLKIT2_REF})"

    local driver_version=""
    if [[ -r /sys/kernel/debug/rknpu/version ]]; then
        driver_version="$(sudo cat /sys/kernel/debug/rknpu/version 2>/dev/null || true)"
    fi
    if [[ -z "$driver_version" ]]; then
        driver_version="$(dmesg 2>/dev/null | grep -i rknpu | grep -oP 'version:\s*\K[0-9.]+' | tail -1 || true)"
    fi
    [[ -n "$driver_version" ]] && log "kernel rknpu driver version: $driver_version" \
        || warn "could not determine kernel rknpu driver version — check /sys/kernel/debug/rknpu/version manually and compare against the librknnrt.so you install"

    if require_header /usr/include/rknn_api.h 2>/dev/null; then
        log "rknn_api.h already installed"
        return 0
    fi
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "RKNN runtime (rknn_api.h not found)"
        return 0
    fi

    local src="$BUILD_ROOT/rknn-toolkit2"
    if [[ ! -d "$src" ]]; then
        git clone --branch "$RKNN_TOOLKIT2_REF" --depth 1 https://github.com/airockchip/rknn-toolkit2.git "$src"
    fi
    local rt_dir="$src/rknpu2/runtime/Linux/librknn_api"
    if [[ ! -d "$rt_dir" ]]; then
        fail "expected runtime layout not found at $rt_dir — rknn-toolkit2's repo layout may have changed; locate librknnrt.so and rknn_api.h manually"
        return 1
    fi
    sudo cp "$rt_dir/include/rknn_api.h" "$PREFIX/include/"
    sudo cp "$rt_dir/aarch64/librknnrt.so" "$PREFIX/lib/"
    sudo ldconfig
    warn "verify the copied librknnrt.so version matches the kernel driver reported above"
}

# ---------------------------------------------------------------------------
# 10. aarch64-only: Rockchip MPP (RK3588 VPU) userspace library
# ---------------------------------------------------------------------------
# The real repo is rockchip-linux/mpp - airockchip/mpp (an easy name to guess) 404s. Built
# natively on the board itself (this project's aarch64 target IS the build host - no cross
# toolchain file needed, unlike build/linux/aarch64/arm.linux.cross.cmake in the repo, which is
# for cross-compiling FROM x86).
#
# Pinned to the latest tagged release (1.1.0), not floating `develop` - `develop` crashed the
# whole process the moment MppJpegDecoder called into it (segfault confirmed deep inside
# mpp_dec_decode itself, on real hardware, unrelated to any caller-side buffer setup - see
# LumenCore/MppJpegDecoder.cpp's own comment). Matches this project's existing convention of
# pinning third-party dependencies to exact, known points (e.g. the vkapriltag submodule) rather
# than a moving branch.
MPP_REF="${MPP_REF:-1.1.0}"

fetch_mpp() {
    [[ "$ARCH" == "aarch64" ]] || return 0

    # Both the rockchip_mpp .pc copy below and librga's own hand-written .pc write into
    # $PREFIX/lib/pkgconfig - this function must not assume that directory already exists.
    # Confirmed the hard way: it always silently relied on something ELSE (build_opencv's own
    # `cmake --install`, which happened to run first in the old call order in main) having
    # created it already - moving fetch_mpp earlier (so ffmpeg-rockchip, and in turn OpenCV's
    # own -DWITH_FFMPEG=ON detection, can see it before build_opencv runs) exposed that this
    # function was never actually self-sufficient.
    [[ "$CHECK_ONLY" -eq 0 ]] && sudo mkdir -p "$PREFIX/lib/pkgconfig"

    if pkg-config --exists rockchip_mpp 2>/dev/null; then
        log "rockchip_mpp already installed ($(pkg-config --modversion rockchip_mpp))"
    elif [[ "$WITH_MPP" -eq 1 ]]; then
        if [[ "$CHECK_ONLY" -eq 1 ]]; then
            note_missing "rockchip_mpp (would build from https://github.com/rockchip-linux/mpp)"
        else
            log "Rockchip MPP (rockchip-linux/mpp, ref=${MPP_REF}) - this is a real from-source build, not a quick fetch"
            local src="$BUILD_ROOT/rockchip-mpp"
            if [[ ! -d "$src" ]]; then
                git clone --branch "$MPP_REF" --depth 1 https://github.com/rockchip-linux/mpp.git "$src"
            fi
            local build_dir="$src/build-native"
            cmake -S "$src" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
            cmake --build "$build_dir" -j "$JOBS"

            # explicit `sudo cp` of the specific files this project actually needs, not
            # `sudo make install` (which would also install a dozen mpi_*_test binaries and
            # delegates the exact file list to a third-party CMakeLists.txt's install() rules
            # rather than something reviewable here) - staged first via DESTDIR so the cp source
            # paths are concrete, matching how fetch_rknn does an explicit two-file cp already.
            local stage="$build_dir/stage"
            rm -rf "$stage"
            DESTDIR="$stage" cmake --install "$build_dir" >/dev/null

            sudo mkdir -p "$PREFIX/include/rockchip"
            sudo cp "$stage$PREFIX/include/rockchip/"*.h "$PREFIX/include/rockchip/"
            sudo cp -P "$stage$PREFIX/lib/librockchip_mpp.so"* "$stage$PREFIX/lib/librockchip_mpp.a" "$PREFIX/lib/"
            sudo cp -P "$stage$PREFIX/lib/librockchip_vpu.so"* "$PREFIX/lib/"
            sudo cp "$stage$PREFIX/lib/pkgconfig/rockchip_mpp.pc" "$stage$PREFIX/lib/pkgconfig/rockchip_vpu.pc" "$PREFIX/lib/pkgconfig/"
            sudo ldconfig
            rm -rf "$stage"
            log "rockchip_mpp installed: $(pkg-config --modversion rockchip_mpp)"
        fi
    else
        note_missing "rockchip_mpp (not found; re-run with --with-mpp to build it from source)"
        return 0
    fi

    if pkg-config --exists librga 2>/dev/null; then
        log "librga already installed ($(pkg-config --modversion librga))"
    elif [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "librga (would install the prebuilt aarch64 binary from airockchip/librga)"
    else
        # airockchip/librga ships only prebuilt binaries for Linux/aarch64 - no CMakeLists.txt/
        # meson.build at the repo root to build from source, confirmed by checking. A plain
        # SONAME (no version suffix), so a bare copy is enough - no versioned symlinks needed.
        log "librga (prebuilt aarch64 binary, airockchip/librga)"
        local rga_src="$BUILD_ROOT/librga"
        if [[ ! -d "$rga_src" ]]; then
            git clone --depth 1 https://github.com/airockchip/librga.git "$rga_src"
        fi
        sudo mkdir -p "$PREFIX/include/rga"
        sudo cp "$rga_src/include/"*.h "$rga_src/include/"*.hpp "$PREFIX/include/rga/"
        sudo cp "$rga_src/libs/Linux/gcc-aarch64/librga.so" "$rga_src/libs/Linux/gcc-aarch64/librga.a" "$PREFIX/lib/"
        # no .pc file is shipped - write one matching what ffmpeg-rockchip's own configure
        # script actually probes for (pkg-config module "librga", headers under rga/*.h)
        sudo tee "$PREFIX/lib/pkgconfig/librga.pc" >/dev/null <<EOF
prefix=$PREFIX
exec_prefix=\${prefix}
libdir=\${exec_prefix}/lib
includedir=\${prefix}/include/rga

Name: librga
Description: Rockchip 2D Raster Graphic Acceleration library (prebuilt aarch64 binary from airockchip/librga)
Version: 1.10.0
Libs: -L\${libdir} -lrga
Cflags: -I\${includedir}
EOF
        sudo ldconfig
        log "librga installed: $(pkg-config --modversion librga)"
    fi

    # Device node permissions, independent of WITH_MPP: /dev/mpp_service, /dev/dma_heap/*, and
    # /dev/rga all ship root-only (crw-------) on this board's image - confirmed the hard way, a
    # real hardware encode test failed with "open vcodec_service /dev/mpp_service failed" and
    # "os_allocator_dma_heap_open ... failed" until these were group-owned. chmod/chgrp alone
    # don't survive a reboot (these nodes are recreated fresh by the kernel each boot); a udev
    # rule is what actually persists this.
    local udev_rule=/etc/udev/rules.d/99-lumenvision-rockchip.rules
    if [[ "$CHECK_ONLY" -eq 0 ]]; then
        log "Writing udev rule: $udev_rule (video group -> mpp_service/dma_heap/rga)"
        sudo tee "$udev_rule" >/dev/null <<'EOF'
# Generated by install-deps.sh --with-mpp. Without this, these device nodes ship root-only
# (crw-------), and MPP/RGA hardware init fails with an opaque error rather than a permissions
# one - see install-deps.sh's fetch_mpp() for how this was actually diagnosed.
KERNEL=="mpp_service", GROUP="video", MODE="0660"
SUBSYSTEM=="dma_heap", GROUP="video", MODE="0660"
KERNEL=="rga", GROUP="video", MODE="0660"
EOF
        sudo udevadm control --reload-rules
        sudo udevadm trigger --subsystem-match=dma_heap 2>/dev/null || true
        sudo udevadm trigger --sysname-match=mpp_service 2>/dev/null || true
        sudo udevadm trigger --sysname-match=rga 2>/dev/null || true
        for node in /dev/mpp_service /dev/rga /dev/dma_heap/system /dev/dma_heap/system-uncached /dev/dma_heap/reserved; do
            [[ -e "$node" ]] || continue
            sudo chgrp video "$node" 2>/dev/null || true
            sudo chmod 660 "$node" 2>/dev/null || true
        done
    fi
}

# ---------------------------------------------------------------------------
# 11. aarch64-only: ffmpeg-rockchip (h264_rkmpp encoder + rkrga filters)
# ---------------------------------------------------------------------------
# NOT upstream FFmpeg's own --enable-rkmpp, which is decode-only - confirmed the hard way, it
# ships rkmppdec.c but no encoder. nyanmisaka/ffmpeg-rockchip is the community fork that actually
# implements the h264_rkmpp ENCODER and the rkrga filters (scale_rkrga/vpp_rkrga/overlay_rkrga).
# Installed to its own dedicated prefix, not $PREFIX (/usr/local) - confirmed the hard way that
# Debian's multiarch ldconfig prioritises /usr/lib/aarch64-linux-gnu (the apt ffmpeg-dev package
# already on this image) over /usr/local/lib for a duplicate SONAME, so a /usr/local install
# would silently lose the runtime-resolution race and run the wrong (non-rkmpp) library with no
# error - only ffmpeg's own "library configuration mismatch" warning, easy to miss. A dedicated
# prefix outside ldconfig's default search path sidesteps the ambiguity entirely, and leaves the
# system's own ffmpeg/apt packages completely untouched for anything else on the board. See
# cmake/LumenFFmpeg.cmake for how LumenCore finds this prefix and resolves it at runtime via an
# explicit rpath (the same reason plain -L doesn't help here - it only affects link-time lookup).
FFMPEG_ROCKCHIP_REF="${FFMPEG_ROCKCHIP_REF:-7.1}"
LUMEN_FFMPEG_PREFIX=/opt/lumenvision-ffmpeg

fetch_ffmpeg_rockchip() {
    [[ "$WITH_FFMPEG_ROCKCHIP" -eq 1 ]] || return 0
    [[ "$ARCH" == "aarch64" ]] || return 0

    if [[ -x "$LUMEN_FFMPEG_PREFIX/bin/ffmpeg" ]] && \
       PKG_CONFIG_PATH="$LUMEN_FFMPEG_PREFIX/lib/pkgconfig" pkg-config --exists libavcodec 2>/dev/null; then
        log "ffmpeg-rockchip already installed at $LUMEN_FFMPEG_PREFIX"
        return 0
    fi
    if [[ "$CHECK_ONLY" -eq 1 ]]; then
        note_missing "ffmpeg-rockchip (would build nyanmisaka/ffmpeg-rockchip, ref=${FFMPEG_ROCKCHIP_REF})"
        return 0
    fi

    if ! pkg-config --exists rockchip_mpp 2>/dev/null || ! pkg-config --exists librga 2>/dev/null; then
        fail "ffmpeg-rockchip needs rockchip_mpp and librga installed first - re-run with --with-mpp --with-ffmpeg-rockchip together"
        return 1
    fi

    log "ffmpeg-rockchip (nyanmisaka/ffmpeg-rockchip, ref=${FFMPEG_ROCKCHIP_REF}) - the heaviest build in this script, budget real wall-clock time"
    apt_install nasm libdrm-dev libx264-dev

    local src="$BUILD_ROOT/ffmpeg-rockchip"
    if [[ ! -d "$src" ]]; then
        git clone --branch "$FFMPEG_ROCKCHIP_REF" --depth 1 https://github.com/nyanmisaka/ffmpeg-rockchip.git "$src"
    fi

    (
        cd "$src"
        # --extra-ldflags='-Wl,-rpath,$ORIGIN' (literal $ORIGIN, single-quoted so THIS shell
        # doesn't expand it) bakes a self-referential rpath into every ffmpeg-rockchip .so it
        # builds - confirmed the hard way: libavcodec.so's own need for libswresample.so (pulled
        # in transitively by ffmpeg's built-in opus decoder) otherwise can't be resolved at
        # runtime by ANYTHING that links avcodec, because modern ld emits non-transitive
        # DT_RUNPATH by default - a consumer's own rpath only covers ITS direct deps, not
        # avcodec's further deps, and avcodec itself ships with no rpath from a plain `make
        # install`. Registering $LUMEN_FFMPEG_PREFIX/lib in ldconfig globally would fix that too,
        # but was deliberately rejected above (see this function's own header comment) - it would
        # reopen the exact SONAME-collision-with-the-apt-ffmpeg risk the dedicated prefix exists
        # to avoid. $ORIGIN is resolved per-.so at load time to wherever THAT FILE actually sits,
        # so this keeps working correctly even after CopyLinuxRuntimeDeps.cmake relocates the
        # whole sibling set together into the packaged /opt/lumenvision on a shipped board.
        PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" ./configure \
            --prefix="$LUMEN_FFMPEG_PREFIX" \
            --enable-shared --disable-static \
            --enable-gpl --enable-version3 \
            --enable-libx264 --enable-rkmpp --enable-rkrga --enable-libdrm \
            --extra-ldflags='-Wl,-rpath,$ORIGIN'
        make -j "$JOBS"
    )

    # explicit `sudo cp -a` of the staged tree, not `sudo make install` (same reviewability
    # reasoning as fetch_mpp above) - ffmpeg installs enough files that listing them individually
    # isn't practical, so this copies the whole staged include/lib/bin trees as bounded,
    # side-effect-free directory copies rather than delegating to the Makefile's install rules.
    local stage="$src/stage"
    rm -rf "$stage"
    DESTDIR="$stage" make -C "$src" install >/dev/null

    sudo mkdir -p "$LUMEN_FFMPEG_PREFIX"
    sudo cp -a "$stage$LUMEN_FFMPEG_PREFIX/." "$LUMEN_FFMPEG_PREFIX/"
    rm -rf "$stage"

    # Belt-and-suspenders on top of --extra-ldflags above: ffmpeg's own Makefiles echo only a
    # terse "LD <target>" per link step with no way to confirm $ORIGIN actually made it into
    # every .so's own link command (their build system routes LDFLAGS through several
    # Makefile-generation layers for shared libs specifically, not just the ffmpeg/ffprobe
    # programs, and that path isn't something this script controls or can easily verify).
    # patchelf sets the rpath directly and deterministically on the files that actually exist on
    # disk, which is both simpler to verify (the log line below proves it) and doesn't depend on
    # ffmpeg's internal LDFLAGS plumbing being trusted at all.
    apt_install patchelf
    local _so
    while IFS= read -r -d '' _so; do
        sudo patchelf --set-rpath '$ORIGIN' "$_so"
    done < <(find "$LUMEN_FFMPEG_PREFIX/lib" -maxdepth 1 -name '*.so*' -print0)
    log "rpath=\$ORIGIN set on every $LUMEN_FFMPEG_PREFIX/lib/*.so* (verifying one): $(patchelf --print-rpath "$LUMEN_FFMPEG_PREFIX/lib/libavcodec.so" 2>&1 || true)"

    log "ffmpeg-rockchip installed to $LUMEN_FFMPEG_PREFIX"
    # captured first, not piped into `grep -q` directly - see install_ffmpeg's own comment on why
    # (pipefail + ffmpeg's own exit code/SIGPIPE would silently misreport this as missing).
    local built_encoders
    built_encoders="$(LD_LIBRARY_PATH="$LUMEN_FFMPEG_PREFIX/lib" "$LUMEN_FFMPEG_PREFIX/bin/ffmpeg" -hide_banner -encoders 2>/dev/null || true)"
    if grep -q h264_rkmpp <<< "$built_encoders"; then
        log "h264_rkmpp encoder confirmed present"
    else
        warn "h264_rkmpp encoder not found in the freshly-built ffmpeg - check the configure/build log above"
    fi
}

# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------
log "LumenVision dependency check/install — arch=$ARCH, check-only=$CHECK_ONLY, jobs=$JOBS"

install_base
# fetch_mpp/fetch_ffmpeg_rockchip run BEFORE build_opencv now - build_opencv's own -DWITH_FFMPEG=ON
# auto-detection needs the dedicated ffmpeg-rockchip prefix to already exist so it links against
# THAT (SONAME 61) instead of falling back to apt's system ffmpeg-dev packages (SONAME 60 on the
# ubuntu-24.04-arm CI runner this .deb is built on). Confirmed the hard way on real hardware: the
# old order shipped a .deb whose bundled libopencv_videoio.so needed libavcodec.so.60, which
# doesn't exist anywhere on the Debian 13 target image (trixie ships SONAME 61 too, just not
# bundled - a real cross-distro mismatch, not just a missing apt Depends: away from working).
fetch_mpp || warn "MPP setup failed - see the log above; continuing"
fetch_ffmpeg_rockchip || warn "ffmpeg-rockchip setup failed - see the log above; continuing"
build_opencv
build_apriltag
install_ffmpeg
install_vulkan
build_vkapriltag || warn "vkapriltag build failed - see the log above; the CPU AprilTag backend remains the fallback"
build_codec_stereo || warn "codec-stereo build failed - see the log above; stereo depth (STEREO_BACKEND_CODEC_*) will be unavailable, STEREO_BACKEND_SGBM is unaffected"
# these are optional/best-effort integrations (WebRTC, NT4, RKNN) - a failure partway through one
# of them (a bad ref, a flaky download) should not, under `set -e`, take down a run that otherwise
# succeeded; ONNX Runtime stays unconditional since --with-* doesn't gate it
build_webrtc || warn "WebRTC setup (libdatachannel) failed - see the log above; continuing"
fetch_ntcore || warn "NT4 setup (ntcore/wpiutil/wpinet) failed - see the log above; continuing"
fetch_onnxruntime
fetch_rknn || warn "RKNN setup failed - see the log above; continuing"

if [[ "$CHECK_ONLY" -eq 1 ]]; then
    echo
    if [[ "${#MISSING[@]}" -eq 0 ]]; then
        log "All checked dependencies are present."
    else
        fail "${#MISSING[@]} dependency issue(s) found:"
        for m in "${MISSING[@]}"; do echo "  - $m"; done
        exit 1
    fi
else
    log "Done. Re-run with --check at any time to verify the environment."
fi
