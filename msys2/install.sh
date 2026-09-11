#!/bin/bash
# Install UCRT64 (x64) build dependencies for vdagent-win.
# Requires an existing MSYS2 installation. Run from the repository root:
#   bash msys2/install.sh

set -euo pipefail

if [[ "${MSYSTEM:-}" != "UCRT64" && "${MSYSTEM:-}" != "MSYS" && "${MSYSTEM:-}" != "" ]]; then
    echo "warning: expected MSYS or UCRT64, got MSYSTEM=${MSYSTEM}" >&2
fi

packages=(
    autotools
    autoconf-archive
    git
    make
    mingw-w64-ucrt-x86_64-toolchain
    mingw-w64-ucrt-x86_64-libpng
    mingw-w64-ucrt-x86_64-zlib
    mingw-w64-ucrt-x86_64-msitools
    mingw-w64-ucrt-x86_64-imagemagick
)

exec pacman --noconfirm --needed -S "${packages[@]}"
