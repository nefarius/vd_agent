#!/bin/bash
# Configure, compile, and run the Autotools test suite in a build directory.
# Does not produce the MSI; run msys2/package.sh afterwards (after optional signing).
#
# Usage:
#   bash msys2/build.sh <build-directory> [configure-args...]
# Example:
#   bash msys2/build.sh builducrt64

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <build-directory> [configure-args...]" >&2
    exit 1
fi

builddir=$1
shift

srcdir=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$builddir"
cd "$builddir"

if [[ ! -x "$srcdir/configure" ]]; then
    echo "configure is missing; run 'autoreconf -i' from $srcdir first" >&2
    exit 1
fi

"$srcdir/configure" "$@"

jobs=$(nproc 2>/dev/null || echo 4)
make -j"$jobs"
if ! make check; then
    if [[ -f test-suite.log ]]; then
        cat test-suite.log
    fi
    exit 1
fi
