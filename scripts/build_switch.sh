#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/cmake-build-switch"
BASE_URL="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
PACKAGES=(
    "switch-ffmpeg-7.1-1-any.pkg.tar.zst"
    "switch-libmpv-0.36.0-3-any.pkg.tar.zst"
)

cd "${PROJECT_ROOT}"
git config --global --add safe.directory "${PROJECT_ROOT}"

DOWNLOAD_DIR="$(mktemp -d)"
trap 'rm -rf "${DOWNLOAD_DIR}"' EXIT
for package in "${PACKAGES[@]}"; do
    curl --fail --location --retry 3 --connect-timeout 15 \
        --output "${DOWNLOAD_DIR}/${package}" "${BASE_URL}/${package}"
done
dkp-pacman -U --noconfirm "${DOWNLOAD_DIR}"/*.pkg.tar.zst

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DPLATFORM_SWITCH=ON \
    -DBUILTIN_NSP=OFF \
    -DBRLS_UNITY_BUILD=ON \
    -DCMAKE_UNITY_BUILD_BATCH_SIZE=16

cmake --build "${BUILD_DIR}" --target wiliwili.nro --parallel 2
test -s "${BUILD_DIR}/switch-tvbox.nro"
