#!/usr/bin/env bash
set -e

# Switch to project root
cd "$(dirname "$0")/.."

DRIVER="${1:-opengl}"

if [ "$DRIVER" = "deko3d" ]; then
    SCRIPT="/data/scripts/build_switch_deko3d.sh"
    echo "=== Building switch-tvbox (deko3d) via Docker ==="
else
    SCRIPT="/data/scripts/build_switch.sh"
    echo "=== Building switch-tvbox (OpenGL) via Docker ==="
fi

docker run --rm -v "$(pwd):/data" -w /data devkitpro/devkita64:20251117 bash -c "$SCRIPT"

echo "=== Build finished. Output in cmake-build-switch/ ==="
