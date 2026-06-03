#!/usr/bin/env bash
# 打包目标机部署物：transferFile + libmosquitto + 配置
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${ROOT}/build-openwrt/transferFile"
LIB_DIR="${ROOT}/third_party/mosquitto-aarch64/lib"
CFG="${ROOT}/config/transferFile.gateway.target.json"
STAGE="${ROOT}/dist/transferFile-target-aarch64"
ARCHIVE="${ROOT}/dist/transferFile-target-aarch64.tar.gz"

if [[ ! -x "${BIN}" ]]; then
  echo "缺少交叉编译产物，请先执行: ${ROOT}/scripts/build-openwrt.sh"
  exit 1
fi

if [[ ! -f "${LIB_DIR}/libmosquitto.so.2.0.18" ]]; then
  echo "缺少 libmosquitto，请先执行: ${ROOT}/scripts/build-mosquitto-cross.sh"
  exit 1
fi

rm -rf "${STAGE}"
mkdir -p "${STAGE}/bin" "${STAGE}/lib" "${STAGE}/config"

cp -a "${BIN}" "${STAGE}/bin/"
cp -a "${LIB_DIR}"/libmosquitto.so* "${STAGE}/lib/"
cp -a "${CFG}" "${STAGE}/config/"

cat > "${STAGE}/README-deploy.txt" <<'EOF'
目标机部署说明
1. 解压到 /opt/transfer（或任意目录）
2. export LD_LIBRARY_PATH=/opt/transfer/lib:$LD_LIBRARY_PATH
3. 编辑 config/transferFile.gateway.target.json 中的 brokerHost（开发机/Broker IP）
4. 在目标机创建待传文件，例如: echo test > /tmp/platform_test_file.bin
5. 运行: ./bin/transferFile -c ./config/transferFile.gateway.target.json
EOF

mkdir -p "${ROOT}/dist"
tar -czf "${ARCHIVE}" -C "${ROOT}/dist" "$(basename "${STAGE}")"

echo "已打包: ${ARCHIVE}"
echo "内容:"
tar -tzf "${ARCHIVE}" | head -20
echo "..."
echo "拷贝示例: scp ${ARCHIVE} root@<目标IP>:/tmp/ && ssh root@<目标IP> 'cd /opt && tar -xzf /tmp/$(basename "${ARCHIVE}")'"
