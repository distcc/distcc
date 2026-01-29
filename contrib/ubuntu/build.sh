#!/bin/bash
# Build script for distcc on ubuntu which installs dependencies, builds, and optionally installs.
# Just installs dependencies, builds, and shows where the binaries are.
# If you use --install, it will also install system-wide.
# I made it on/for ubuntu 22.04 
# WTFPLv2 - Hippy<dmxout@gmail.com>
#
# Usage: ./build.sh [--gtk] [--install]
# Will install debian/ubuntu deps, and build distcc in the current directory.
#
# Options:
#   --gtk      Build with GTK graphical monitor (distccmon-gnome)
#   --install  Install after building

set -e  # auto exit on error

# cd to repo root (assuming script is in contrib/ubuntu/)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT"

# show usage
usage() {
    echo "Usage: $0 [--gtk] [--install] [--help]"
    echo "  --gtk      Build with GTK graphical monitor (distccmon-gnome)"
    echo "  --install  Install after building"
    echo "  --help     Show this help message"
    exit "${1:-0}"
}

# --options
WITH_GTK=0
DO_INSTALL=0
for arg in "$@"; do
    case $arg in
        --gtk)     WITH_GTK=1 ;;
        --install) DO_INSTALL=1 ;;
        --help|-h) usage ;;
        *)         echo "Unknown option: $arg"; usage 1 ;;
    esac
done

# dependencies
PACKAGES=(
    build-essential
    autoconf
    libpopt-dev
    python3
    python3-dev
)
if [ "$WITH_GTK" -eq 1 ]; then
    PACKAGES+=(libgtk-3-dev) # for distccmon-gnome
fi

# install dependencies
echo ""
echo "==> Installing build dependencies: ${PACKAGES[*]}"
echo "(Note: needs sudo privileges)"
echo ""
echo "executing: sudo apt-get update && sudo apt-get install -y ${PACKAGES[*]}"
sudo apt-get update || true # ignore errors on update, just try to install
sudo apt-get install -y "${PACKAGES[@]}"

# generate configure script
echo ""
echo "==> Running autogen.sh"
./autogen.sh

# configure the build
CONFIGURE_OPTS=""
if [ "$WITH_GTK" -eq 1 ]; then
    CONFIGURE_OPTS="--with-gtk"
fi
echo ""
echo "==> Configuring build${WITH_GTK:+ (with GTK support)}..."
./configure $CONFIGURE_OPTS

# build using all minus one available CPU cores, unless only one core is available :)
CPU_CORES=$(nproc)
if [ "$CPU_CORES" -gt 1 ]; then
    MAKE_JOBS=$((CPU_CORES - 1))
else
    MAKE_JOBS=1
fi

echo ""
echo "==> Building with $MAKE_JOBS parallel jobs..."
echo "(Note: the include-server build prints a verbose shell block - that's normal)"
make -j "$MAKE_JOBS"  # dies if build fails

# verify include-server was built (needed for pump mode)
if ls _include_server/lib.*/include_server/*.so &>/dev/null; then
    echo ""
    echo "==> Include server built successfully (pump mode available)"
else
    echo ""
    echo "WARNING: Include server was not built - pump mode will not work!"
fi

# install if requested
if [ "$DO_INSTALL" -eq 1 ]; then
    echo ""
    echo "==> Installing distcc system-wide..."
    echo "(Note: needs sudo privileges)"
    sudo make install # bails on fails 
    echo ""
    echo "Build and install complete!"
    BIN_PREFIX="/usr/local/bin/"
else
    echo ""
    echo "Build complete!"
    BIN_PREFIX="./"
fi

echo ""
echo "Binaries:"
echo "  ${BIN_PREFIX}distcc          - distributed compiler client"
echo "  ${BIN_PREFIX}distccd         - distributed compilation daemon"
echo "  ${BIN_PREFIX}distccmon-text  - text-based monitoring tool"
if [ "$WITH_GTK" -eq 1 ]; then
    echo "  ${BIN_PREFIX}distccmon-gnome - GTK graphical monitoring tool"
fi
echo "  ${BIN_PREFIX}lsdistcc        - server discovery tool"
echo ""
echo "To run tests: make maintainer-check"
if [ "$DO_INSTALL" -eq 0 ]; then
    echo "To install:   sudo make install"
fi
