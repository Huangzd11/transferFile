#!/usr/bin/env bash
# 为 OpenWrt aarch64 交叉编译 libmosquitto（静态库），供 transferFile 链接
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TOOLCHAIN_ROOT="${HOME}/toolchains/openwrt-gcc-8.3.0"
INSTALL_PREFIX="${ROOT}/third_party/mosquitto-aarch64"
SRC_DIR="${ROOT}/third_party/mosquitto-src"
BUILD_DIR="${SRC_DIR}/build-aarch64"
VERSION="2.0.18"
TARBALL="${ROOT}/third_party/mosquitto-${VERSION}.tar.gz"
URL_PRIMARY="https://github.com/eclipse/mosquitto/archive/refs/tags/v${VERSION}.tar.gz"
URL_FALLBACK_1="https://github.com/eclipse/mosquitto/archive/v${VERSION}.tar.gz"
URL_FALLBACK_2="https://codeload.github.com/eclipse/mosquitto/tar.gz/refs/tags/v${VERSION}"

download_tarball() {
  local url="$1"
  local tmp="${TARBALL}.part"

  rm -f "${tmp}"

  if command -v curl >/dev/null 2>&1; then
    # 说明：
    # - GitHub 偶发会出现 TLS/网络断流（例如 unexpected eof），这里用重试+降噪+强制跟随重定向兜底
    curl -fsSL --http1.1 \
      --retry 6 --retry-delay 1 --retry-connrefused --retry-all-errors \
      -o "${tmp}" "${url}"
  elif command -v wget >/dev/null 2>&1; then
    wget -q --tries=6 --waitretry=1 -O "${tmp}" "${url}"
  else
    echo "缺少下载工具：curl/wget 均不可用"
    return 1
  fi

  mv -f "${tmp}" "${TARBALL}"
}

ensure_tarball_ok() {
  [[ -f "${TARBALL}" ]] || return 1
  tar -tzf "${TARBALL}" >/dev/null 2>&1
}

if [[ -f "${INSTALL_PREFIX}/lib/libmosquitto.so" ]] || [[ -f "${INSTALL_PREFIX}/lib/libmosquitto.a" ]]; then
  echo "已存在: ${INSTALL_PREFIX}/lib/libmosquitto.so"
  exit 0
fi

if [[ ! -x "${TOOLCHAIN_ROOT}/bin/aarch64-openwrt-linux-musl-gcc" ]]; then
  echo "请先解压 OpenWrt 工具链到 ${TOOLCHAIN_ROOT}"
  exit 1
fi

mkdir -p "${ROOT}/third_party"
if [[ ! -d "${SRC_DIR}" ]]; then
  echo "下载 mosquitto ${VERSION} ..."
  if ! ensure_tarball_ok; then
    rm -f "${TARBALL}"
    download_tarball "${URL_PRIMARY}" || \
    download_tarball "${URL_FALLBACK_1}" || \
    download_tarball "${URL_FALLBACK_2}"
  fi
  tar -xzf "${TARBALL}" -C "${ROOT}/third_party"
  mv "${ROOT}/third_party/mosquitto-${VERSION}" "${SRC_DIR}"
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake "${SRC_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${ROOT}/cmake/toolchain-openwrt.cmake" \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DWITH_BROKER=OFF \
  -DWITH_APPS=OFF \
  -DWITH_CLIENTS=OFF \
  -DWITH_TLS=OFF \
  -DWITH_WEBSOCKETS=OFF \
  -DWITH_TESTS=OFF \
  -DDOCUMENTATION=OFF

cmake --build . -j"$(nproc)"
cmake --install .

echo "libmosquitto 已安装到: ${INSTALL_PREFIX}"
ls -la "${INSTALL_PREFIX}/lib/" || true
