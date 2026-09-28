#!/usr/bin/env bash
#
# LumenVision dependency installer.
#
# Run on the machine that compiles LumenCore: WSL2 "Ubuntu" or the Orange Pi over SSH. Installs
# system packages and from-source/prebuilt third-party dependencies; the build itself is driven by
# the CMakePresets.json presets.
#
# Supported distros (detected via /etc/os-release): Ubuntu 24.04 and Debian 13 "trixie".
# CPU-architecture differences are gated on `uname -m`.
#
# Usage:
#   ./install-deps.sh [--check] [--jobs N] [--skip-opencv] [--with-webrtc] [--with-nt4] [--with-rknn]
#
#   --check         verify what's installed/built and report what's missing; installs nothing
#   --jobs N        parallelism for from-source builds (default: nproc)
#   --skip-opencv   skip the OpenCV 5.0 source build (useful once it's already built and cached)
#   --with-webrtc   also build libdatachannel (slow)
#   --with-nt4      also fetch ntcore/wpiutil
#   --with-rknn     also fetch the RKNN runtime (aarch64 only)
#   --with-mpp      also build the Rockchip MPP (VPU) library and install librga (aarch64 only).
#                   On aarch64 a udev rule is always installed to give /dev/mpp_service,
#                   /dev/dma_heap/* and /dev/rga group access (they are root-only by default).
#   --with-ffmpeg-rockchip
#                   also build nyanmisaka/ffmpeg-rockchip (h264_rkmpp encoder, rkrga filters) into
#                   /opt/lumenvision-ffmpeg, outside ldconfig's path so the apt ffmpeg SONAME does
#                   not shadow it. Requires --with-mpp; aarch64 only.
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
# Package names differ between the supported distros; fail on anything else
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
BUILD_ROOT="${LUMEN_BUILD_ROOT:-$HOME/.lumen-build}"
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
    # try the whole batch first; on failure install one at a time so one bad name does not block the rest
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
# SSH is the only way into the headless Orange Pi image
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
    # `|| true` keeps --check going: a bare failing require_cmd would abort under `set -e`
    require_cmd gcc || true
    require_cmd g++ || true
    require_cmd gdbserver gdbserver || true
    require_cmd swig swig || true
    require_cmd rsync || true

    # mDNS hostname (lumenvision.local)
    apt_install avahi-daemon
    if [[ "$CHECK_ONLY" -eq 0 ]]; then
        sudo systemctl enable --now avahi-daemon || warn "could not enable avahi-daemon"
    fi
}

# ---------------------------------------------------------------------------
# 2. OpenCV 5.0, built from source with opencv_contrib
# ---------------------------------------------------------------------------
# Built from source into /usr/local (the distro ships 4.x); opencv_contrib provides ArUco for ChArUco calibration
OPENCV_VERSION="5.0.0"

build_opencv() {
    if [[ "$SKIP_OPENCV" -eq 1 ]]; then
        log "Skipping OpenCV build (--skip-opencv)"
        return 0
    fi

    log "OpenCV ${OPENCV_VERSION} (from source, with opencv_contrib)"

    # OpenCV 5 installs headers under include/opencv5 and the pkg-config module opencv5
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

    # purge distro OpenCV -dev packages so they cannot shadow the from-source install;
    # the libopencv-*t64 glob exists only on Ubuntu
    if dpkg -s libopencv-core-dev >/dev/null 2>&1; then
        warn "purging distro OpenCV -dev packages to avoid a stale /usr/include/opencv4 shadowing this build"
        local opencv_purge_globs=('libopencv-*-dev')
        [[ "$DISTRO_ID" == "ubuntu" ]] && opencv_purge_globs+=('libopencv-*t64')
        sudo apt-get purge -y "${opencv_purge_globs[@]}" || true
    fi

    # No GTK/Python dev packages: headless machines, Python bindings off.
    #
    # FFmpeg: when the ffmpeg-rockchip prefix exists (fetch_ffmpeg_rockchip runs first) point OpenCV at
    # it, so only one ffmpeg SONAME is ever bundled; otherwise use apt's ffmpeg-dev packages.
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
# Built from the patched v3.4.5 source vkapriltag uses and installed as the only apriltag: both the CPU
# backend and VkApriltagBackend load libapriltag.so.3, and only the patch exports the symbols the latter needs.
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

    # Stamp the install with the SHA-256 of the vkapriltag patch and rebuild when it changes (the patch
    # adds symbols, not headers); under /usr/local/lib so the CI dependency caches keep it
    local stamp="$PREFIX/lib/apriltag/lumenvision-patch.sha256"
    local want_sha
    want_sha="$(sha256sum "$patch_file" | cut -d' ' -f1)"
    if require_header /usr/local/include/apriltag/apriltag_pose.h 2>/dev/null \
        && [[ -f "$stamp" && "$(cat "$stamp")" == "$want_sha" ]]; then
        log "apriltag already installed under /usr/local (built with the current vkapriltag patch)"
        return 0
    fi

    # purge only installed packages, since apt-get purge aborts on any unknown name; both the
    # Ubuntu t64 and plain Debian names are checked
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
        # reset to the pristine tag so a previously applied patch version does not conflict
        git reset --hard -q
        git clean -fdq -e build
        # strip CRLF: git apply rejects a CRLF patch against the LF clone
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
        # probe capabilities directly (device nodes, pkg-config file, encoder list), not a distro repo
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
        # Only ffmpeg-rockchip provides the h264_rkmpp encoder (see fetch_ffmpeg_rockchip). Output is
        # captured first: under pipefail, ffmpeg's exit status or SIGPIPE from `grep -q` fails the pipeline.
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
# 5. Vulkan (vkapriltag); on aarch64 this must resolve to the image's libmali driver
# ---------------------------------------------------------------------------
install_vulkan() {
    log "Vulkan development packages"
    # glslc (separate from glslang-tools) is required by vkapriltag's CMake
    apt_install libvulkan-dev vulkan-tools glslang-tools spirv-tools glslc

    if [[ "$ARCH" != "aarch64" ]]; then
        return 0
    fi

    log "Checking Vulkan ICD (expecting the board's bundled libmali, not panfrost/panvk)"

    # The registered ICD filename differs per image, so discover it and use that path below
    local icd_dir=/usr/share/vulkan/icd.d
    local icd_json=""

    if [[ -d "$icd_dir" ]]; then
        icd_json="$(find "$icd_dir" -maxdepth 1 -iname '*.json' 2>/dev/null | head -1)"
    fi

    if [[ -n "$icd_json" ]]; then
        log "Vulkan ICD(s) already registered:"
        ls "$icd_dir"/*.json
    else
        # Nothing registered: use the libmali wayland-gbm variant with Vulkan entry points (works headless)
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
        # A systemd drop-in overrides lumenvision.service's default VK_ICD_FILENAMES for this board
        local dropin_dir=/etc/systemd/system/lumenvision.service.d
        log "Writing systemd drop-in: $dropin_dir/10-vulkan-icd.conf -> VK_ICD_FILENAMES=$icd_json"
        sudo mkdir -p "$dropin_dir"
        sudo tee "$dropin_dir/10-vulkan-icd.conf" >/dev/null <<EOF
# Generated by install-deps.sh: the Vulkan ICD registered on this board (re-run to refresh)
[Service]
Environment=VK_ICD_FILENAMES=$icd_json
EOF
        if systemctl is-enabled lumenvision.service >/dev/null 2>&1 || systemctl list-unit-files lumenvision.service >/dev/null 2>&1; then
            sudo systemctl daemon-reload
        fi
    fi
}

# ---------------------------------------------------------------------------
# 5b. vkapriltag — builds the third_party/vkapriltag submodule's static library.
# ---------------------------------------------------------------------------
# vkapriltag is built with FetchContent's private apriltag copy; only libvkapriltag.a is used, so the
# system apriltag from build_apriltag() is the one loaded at runtime.
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
        # -fPIC: the static library is linked into a shared library
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
# 5c. codec-stereo — builds the third_party/codec-stereo submodule's static library.
# ---------------------------------------------------------------------------
# Runs unconditionally: its dependencies (a C compiler, libav* with libx264) are installed above anyway.
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
        # CS_ENABLE_RKMPP (KEY_MOTION_INFO readback) is never enabled (see StereoDepthBackendKind.h);
        # CS_ENABLE_RKMPP_HWENC (aarch64) reads the encoded bitstream instead.
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
            # -DCMAKE_POSITION_INDEPENDENT_CODE=ON: the static library is linked into libLumenCore.so.
            # -DCS_BUILD_HARNESS=OFF: the harness uses the system opencv4, which conflicts with the OpenCV 5.0 in /usr/local.
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

        # sanity-check the backend on the machine that will run it
        ( cd "$build_dir" && ctest --output-on-failure ) || warn "codec-stereo's own test suite failed - see the log above"
    fi
}

# ---------------------------------------------------------------------------
# 6. WebRTC — libdatachannel, opt-in via --with-webrtc
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
# 7. NT4 — ntcore + wpiutil, opt-in via --with-nt4
# ---------------------------------------------------------------------------
# Prebuilt WPILib C++ artifacts from Maven (no apt package); versions are listed at
# https://frcmaven.wpi.edu/ui/repos/tree/General/release/edu/wpi/first/ntcore/ntcore-cpp
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
    # ntcore links against wpinet
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
    # the libraries are nested inside the zips, so search recursively
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
# librknnrt.so must match the kernel's rknpu driver version, so the driver version is reported below
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
# Built natively on the board. Pinned to 1.1.0: the floating `develop` branch segfaults inside
# mpp_dec_decode.
MPP_REF="${MPP_REF:-1.1.0}"

fetch_mpp() {
    [[ "$ARCH" == "aarch64" ]] || return 0

    # $PREFIX/lib/pkgconfig is written to below and may not exist yet
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

            # copy specific staged files rather than `make install`, which also installs test binaries
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
        # librga ships prebuilt binaries only (no build system); its SONAME is unversioned, so a plain copy suffices
        log "librga (prebuilt aarch64 binary, airockchip/librga)"
        local rga_src="$BUILD_ROOT/librga"
        if [[ ! -d "$rga_src" ]]; then
            git clone --depth 1 https://github.com/airockchip/librga.git "$rga_src"
        fi
        sudo mkdir -p "$PREFIX/include/rga"
        sudo cp "$rga_src/include/"*.h "$rga_src/include/"*.hpp "$PREFIX/include/rga/"
        sudo cp "$rga_src/libs/Linux/gcc-aarch64/librga.so" "$rga_src/libs/Linux/gcc-aarch64/librga.a" "$PREFIX/lib/"
        # no .pc file is shipped; write the "librga" pkg-config module that ffmpeg-rockchip's configure probes
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

    # Independent of WITH_MPP: these device nodes are root-only and recreated each boot, so group
    # access is persisted with a udev rule
    local udev_rule=/etc/udev/rules.d/99-lumenvision-rockchip.rules
    if [[ "$CHECK_ONLY" -eq 0 ]]; then
        log "Writing udev rule: $udev_rule (video group -> mpp_service/dma_heap/rga)"
        sudo tee "$udev_rule" >/dev/null <<'EOF'
# Generated by install-deps.sh: group access to the MPP, RGA and dma_heap device nodes
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
# nyanmisaka/ffmpeg-rockchip provides the h264_rkmpp encoder and rkrga filters (upstream's
# --enable-rkmpp is decode-only). Installed outside ldconfig's path so the apt ffmpeg SONAME cannot
# shadow it; see cmake/LumenFFmpeg.cmake for the rpath.
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
        # Literal $ORIGIN rpath on every .so so avcodec's own dependencies (e.g. libswresample)
        # resolve beside it, including after packaging into /opt/lumenvision
        PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" ./configure \
            --prefix="$LUMEN_FFMPEG_PREFIX" \
            --enable-shared --disable-static \
            --enable-gpl --enable-version3 \
            --enable-libx264 --enable-rkmpp --enable-rkrga --enable-libdrm \
            --extra-ldflags='-Wl,-rpath,$ORIGIN'
        make -j "$JOBS"
    )

    # copy the staged tree rather than `make install` (too many files to list individually)
    local stage="$src/stage"
    rm -rf "$stage"
    DESTDIR="$stage" make -C "$src" install >/dev/null

    sudo mkdir -p "$LUMEN_FFMPEG_PREFIX"
    sudo cp -a "$stage$LUMEN_FFMPEG_PREFIX/." "$LUMEN_FFMPEG_PREFIX/"
    rm -rf "$stage"

    # patchelf sets the $ORIGIN rpath directly on the installed files, in case ffmpeg's LDFLAGS
    # handling missed some
    apt_install patchelf
    local _so
    while IFS= read -r -d '' _so; do
        sudo patchelf --set-rpath '$ORIGIN' "$_so"
    done < <(find "$LUMEN_FFMPEG_PREFIX/lib" -maxdepth 1 -name '*.so*' -print0)
    log "rpath=\$ORIGIN set on every $LUMEN_FFMPEG_PREFIX/lib/*.so* (verifying one): $(patchelf --print-rpath "$LUMEN_FFMPEG_PREFIX/lib/libavcodec.so" 2>&1 || true)"

    log "ffmpeg-rockchip installed to $LUMEN_FFMPEG_PREFIX"
    # captured first; see install_ffmpeg on pipefail
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
# fetch_mpp/fetch_ffmpeg_rockchip run before build_opencv so its -DWITH_FFMPEG=ON detection links
# ffmpeg-rockchip (SONAME 61) rather than apt's ffmpeg (SONAME 60 on the CI runner)
fetch_mpp || warn "MPP setup failed - see the log above; continuing"
fetch_ffmpeg_rockchip || warn "ffmpeg-rockchip setup failed - see the log above; continuing"
build_opencv
build_apriltag
install_ffmpeg
install_vulkan
build_vkapriltag || warn "vkapriltag build failed - see the log above; the CPU AprilTag backend remains the fallback"
build_codec_stereo || warn "codec-stereo build failed - see the log above; stereo depth (STEREO_BACKEND_CODEC_*) will be unavailable, STEREO_BACKEND_SGBM is unaffected"
# optional integrations must not abort the run under `set -e`; ONNX Runtime is unconditional
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
