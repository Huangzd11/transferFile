#!/usr/bin/env bash
# OpenWrt aarch64 交叉编译（默认链接 libmosquitto）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_ARCHIVE="/home/huangzd/openwrt-gcc-8.3.0.tar.gz"
TOOLCHAIN_ROOT="${HOME}/toolchains/openwrt-gcc-8.3.0"
BUILD="${ROOT}/build-openwrt"

if [[ ! -f "${TOOLCHAIN_ARCHIVE}" ]]; then
  echo "缺少工具链: ${TOOLCHAIN_ARCHIVE}"
  exit 1
fi

if [[ ! -x "${TOOLCHAIN_ROOT}/bin/aarch64-openwrt-linux-musl-g++" ]]; then
  echo "解压工具链到 ${TOOLCHAIN_ROOT} ..."
  mkdir -p "$(dirname "${TOOLCHAIN_ROOT}")"
  tar -xzf "${TOOLCHAIN_ARCHIVE}" -C "$(dirname "${TOOLCHAIN_ROOT}")"
fi

echo "=== 交叉编译 libmosquitto ==="
"${ROOT}/scripts/build-mosquitto-cross.sh"

export MOSQUITTO_ROOT="${ROOT}/third_party/mosquitto-aarch64"

mkdir -p "${BUILD}"
cd "${BUILD}"
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE="${ROOT}/cmake/toolchain-openwrt.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DTRANSFER_WITH_MOSQUITTO=ON \
  -DMOSQUITTO_ROOT="${MOSQUITTO_ROOT}"

cmake --build . -j"$(nproc)"
file ./transferFile
echo ""
echo "交叉编译完成: ${BUILD}/transferFile"
echo "已链接 libmosquitto（目标板需部署 libmosquitto.so* 到设备，见 third_party/mosquitto-aarch64/lib/）"
echo "打包部署: ${ROOT}/scripts/package-target.sh"
