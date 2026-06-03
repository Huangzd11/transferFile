#!/usr/bin/env bash
# 主机本地编译（可选 libmosquitto）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${ROOT}/build"

"${ROOT}/scripts/build-mosquitto-native.sh" || true
export MOSQUITTO_ROOT="${ROOT}/third_party/mosquitto-native"
if [[ -f "${MOSQUITTO_ROOT}/lib/libmosquitto.so" ]]; then
  export MOSQUITTO_ROOT
fi

mkdir -p "${BUILD}"
cd "${BUILD}"
cmake .. -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DTRANSFER_WITH_MOSQUITTO=ON \
  ${MOSQUITTO_ROOT:+ -DMOSQUITTO_ROOT="${MOSQUITTO_ROOT}"}
cmake --build . -j"$(nproc)"
ctest --output-on-failure
echo "产物:"
echo "  ${BUILD}/transferFile   - 网关"
echo "  ${BUILD}/platform_sim   - 平台模拟下发"
echo "联调: ${ROOT}/scripts/debug-mqtt-local.sh"
