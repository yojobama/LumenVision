#!/usr/bin/env bash
# Builds a self-contained lumenvision-backend .deb for the host architecture (arm64 or amd64).
# Requires a prior scripts/install-deps.sh run: the LumenCore POST_BUILD step bundles the runtime
# dependencies from /usr/local and /opt/lumenvision-ffmpeg into the package.
#
# Usage: VERSION=1.2.3 scripts/build-deb.sh
#   VERSION            - package version (digits/dots, no leading "v"); required
#   LUMEN_CORE_PRESET  - CMake preset; defaults to pi-arm64-release on aarch64, ci-linux-x64 on x86_64
# Produces: lumenvision-backend_<VERSION>_<arm64|amd64>.deb in the repo root.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="${VERSION:?set VERSION=x.y.z (no leading v) before running this script}"
PKG_NAME="lumenvision-backend"
STAGE_DIR="$REPO_ROOT/out/deb-stage"

# Debian architecture name and dotnet RID for the host
HOST_ARCH="$(uname -m)"
case "$HOST_ARCH" in
    aarch64)
        DEB_ARCH="arm64"
        DOTNET_RID="linux-arm64"
        LUMEN_CORE_PRESET="${LUMEN_CORE_PRESET:-pi-arm64-release}"
        ;;
    x86_64)
        DEB_ARCH="amd64"
        DOTNET_RID="linux-x64"
        LUMEN_CORE_PRESET="${LUMEN_CORE_PRESET:-ci-linux-x64}"
        ;;
    *)
        echo "build-deb.sh only knows how to package aarch64 or x86_64 - got $HOST_ARCH" >&2
        exit 1
        ;;
esac
DEB_FILE="$REPO_ROOT/${PKG_NAME}_${VERSION}_${DEB_ARCH}.deb"

echo "==> Cleaning previous stage"
rm -rf "$STAGE_DIR" "$DEB_FILE"
mkdir -p "$STAGE_DIR/DEBIAN" "$STAGE_DIR/opt/lumenvision" \
         "$STAGE_DIR/etc/systemd/system" "$STAGE_DIR/etc/udev/rules.d" "$STAGE_DIR/etc/systemd/journald.conf.d"

echo "==> Building webui"
(cd webui && npm ci && npm run build)

# Configure and build LumenCore explicitly; the dotnet publish hook only rebuilds an already-configured preset
echo "==> Configuring/building LumenCore ($LUMEN_CORE_PRESET)"
cmake --preset "$LUMEN_CORE_PRESET"
cmake --build --preset "$LUMEN_CORE_PRESET"

echo "==> Publishing Server (self-contained, $DOTNET_RID, Release)"
dotnet publish "$REPO_ROOT/Server/Server.csproj" -c Release -r "$DOTNET_RID" --self-contained true \
    -p:LumenCorePreset="$LUMEN_CORE_PRESET" -p:Version="$VERSION"

PUBLISH_DIR="$REPO_ROOT/Server/bin/Release/net10.0/$DOTNET_RID/publish"
if [[ ! -f "$PUBLISH_DIR/Server" ]]; then
    echo "publish output not found at $PUBLISH_DIR - dotnet publish must have failed" >&2
    exit 1
fi

echo "==> Assembling package tree"
cp -r "$PUBLISH_DIR/." "$STAGE_DIR/opt/lumenvision/"
mkdir -p "$STAGE_DIR/opt/lumenvision/wwwroot"
cp -r webui/dist/. "$STAGE_DIR/opt/lumenvision/wwwroot/"
chmod +x "$STAGE_DIR/opt/lumenvision/Server"

cp scripts/lumenvision.service "$STAGE_DIR/etc/systemd/system/lumenvision.service"
cp scripts/journald-lumenvision.conf "$STAGE_DIR/etc/systemd/journald.conf.d/lumenvision.conf"

# Same device permissions as install-deps.sh --with-mpp
cat > "$STAGE_DIR/etc/udev/rules.d/99-lumenvision-rockchip.rules" <<'EOF'
# Installed by the lumenvision-backend .deb; device nodes are otherwise root-only
KERNEL=="mpp_service", GROUP="video", MODE="0660"
SUBSYSTEM=="dma_heap", GROUP="video", MODE="0660"
KERNEL=="rga", GROUP="video", MODE="0660"
# RK3588 GPU (fb000000.gpu), memory controller (dmc) and NPU (fdab0000.npu): hold the top clock instead of the on-demand governors
SUBSYSTEM=="devfreq", KERNEL=="fb000000.gpu|dmc|fdab0000.npu", ATTR{governor}="performance"
EOF

echo "==> Writing DEBIAN control files"
INSTALLED_SIZE_KB=$(du -sk "$STAGE_DIR/opt" "$STAGE_DIR/etc" | awk '{sum+=$1} END {print sum}')
# Fall back to a fixed identity when git user.name/email are unset
GIT_MAINTAINER_NAME="$(git config user.name 2>/dev/null || true)"
GIT_MAINTAINER_EMAIL="$(git config user.email 2>/dev/null || true)"
if [[ -n "$GIT_MAINTAINER_NAME" && -n "$GIT_MAINTAINER_EMAIL" ]]; then
    MAINTAINER="$GIT_MAINTAINER_NAME <$GIT_MAINTAINER_EMAIL>"
else
    MAINTAINER="LumenVision <noreply@example.invalid>"
fi

cat > "$STAGE_DIR/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Section: misc
Priority: optional
Architecture: $DEB_ARCH
Installed-Size: $INSTALLED_SIZE_KB
Depends: avahi-daemon, libdrm2, libvulkan1, libssl3
Maintainer: $MAINTAINER
Description: LumenVision vision coprocessor backend
 Self-contained FRC vision coprocessor server (camera capture, AprilTag/object detection,
 WebRTC live preview, NetworkTables publishing). Every from-source runtime dependency
 (OpenCV, ffmpeg-rockchip, librga, etc.) is bundled privately under /opt/lumenvision - the
 only external requirements are the board's own Vulkan/Mali GPU driver and the RGA/MPP kernel
 device nodes, neither of which a .deb can provide.
EOF

cp scripts/deb/postinst "$STAGE_DIR/DEBIAN/postinst"
cp scripts/deb/prerm "$STAGE_DIR/DEBIAN/prerm"
cp scripts/deb/postrm "$STAGE_DIR/DEBIAN/postrm"
chmod 755 "$STAGE_DIR/DEBIAN/postinst" "$STAGE_DIR/DEBIAN/prerm" "$STAGE_DIR/DEBIAN/postrm"

echo "==> Building $DEB_FILE"
dpkg-deb --build --root-owner-group "$STAGE_DIR" "$DEB_FILE"
echo "==> Done: $DEB_FILE"
