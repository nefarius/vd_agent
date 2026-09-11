#!/bin/bash
# Build the WiX/wixl MSI from already-compiled binaries in a build directory.
# Sign vdagent.exe and vdservice.exe in that directory before invoking this
# script so the installer embeds the signed binaries.
#
# Usage:
#   bash msys2/package.sh <build-directory>

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <build-directory>" >&2
    exit 1
fi

builddir=$1
if [[ ! -f "$builddir/Makefile" ]]; then
    echo "no Makefile in $builddir; run msys2/build.sh first" >&2
    exit 1
fi

jobs=$(nproc 2>/dev/null || echo 4)
make -C "$builddir" -j"$jobs" msi

echo "MSI artifacts:"
ls -l "$builddir"/spice-vdagent-*.msi
