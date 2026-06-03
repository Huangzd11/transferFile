#!/usr/bin/env bash
# 本机 x86_64 编译 libmosquitto（无系统 dev 包时使用）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INSTALL_PREFIX="${ROOT}/third_party/mosquitto-native"
SRC_DIR="${ROOT}/third_party/mosquitto-src-native"
BUILD_DIR="${SRC_DIR}/build-native"
VERSION="2.0.18"
TARBALL="${ROOT}/third_party/mosquitto-${VERSION}.tar.gz"
URL="https://github.com/eclipse/mosquitto/archive/refs/tags/v${VERSION}.tar.gz"

if pkg-config --exists libmosquitto 2>/dev/null; then
  echo "系统已有 libmosquitto: $(pkg-config --libs libmosquitto)"
  exit 0
fi

if [[ -f "${INSTALL_PREFIX}/lib/libmosquitto.so" ]]; then
  echo "已存在: ${INSTALL_PREFIX}/lib/libmosquitto.so"
  exit 0
fi

mkdir -p "${ROOT}/third_party"
if [[ ! -d "${SRC_DIR}" ]]; then
  if [[ ! -f "${TARBALL}" ]]; then
    echo "下载 mosquitto ${VERSION} ..."
    curl -fsSL -o "${TARBALL}" "${URL}"
  fi
  tar -xzf "${TARBALL}" -C "${ROOT}/third_party"
  cp -a "${ROOT}/third_party/mosquitto-${VERSION}" "${SRC_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
cmake "${SRC_DIR}" \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DWITH_BROKER=OFF \
  -DWITH_APPS=OFF \
  -DWITH_CLIENTS=OFF \
  -DWITH_TLS=OFF \
  -DDOCUMENTATION=OFF
cmake --build . -j"$(nproc)"
cmake --install .
echo "本机 libmosquitto: ${INSTALL_PREFIX}"
