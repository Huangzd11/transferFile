#!/usr/bin/env bash
# 联调说明：开发机=平台模拟；目标机=网关 transferFile
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PLATFORM_CFG="${ROOT}/config/transferFile.platform.json"
TARGET_CFG="${ROOT}/config/transferFile.gateway.target.json"
LOCAL_CFG="${ROOT}/config/transferFile.mqtt-debug.json"
BUILD="${ROOT}/build"

if [[ ! -x "${BUILD}/platform_sim" ]]; then
  echo "请先编译开发机平台工具: ${ROOT}/scripts/build-native.sh"
  exit 1
fi

echo "=== 联调拓扑 ==="
echo "  开发机(本机): platform_sim  — 模拟平台，发召唤、收文件"
echo "  目标机(板子): transferFile    — 真实网关，读目标机本地文件并上传"
echo "  MQTT Broker:  两边都能访问（常在开发机，目标机 brokerHost 填开发机 IP）"
echo ""
echo "配置文件:"
echo "  平台(开发机): ${PLATFORM_CFG}"
echo "  网关(目标机): ${TARGET_CFG}   ← 修改 brokerHost 为 Broker 地址"
echo "  全在本机冒烟: ${LOCAL_CFG}"
echo ""
echo "目标机步骤:"
echo "  1) 交叉编译: ./scripts/build-openwrt.sh"
echo "  2) 拷贝 transferFile + 配置 + libmosquitto(若动态) 到板子"
echo "  3) 在目标机创建待传文件，例如: echo test > /tmp/platform_test_file.bin"
echo "  4) ./transferFile -c transferFile.gateway.target.json"
echo ""
echo "开发机步骤:"
echo "  1) 确认 Broker 已监听 1883（apt install 后常已由 systemd 启动；勿重复 mosquitto -v）"
echo "     目标机配置 brokerHost=开发机局域网 IP"
echo "  2) ${BUILD}/platform_sim -c ${PLATFORM_CFG} --gateway-file /tmp/platform_test_file.bin"
echo "     (V0.0.4: platform_sim 每收内容段自动发 content_confirm)"
echo ""
echo "验收: ./scripts/run-acceptance-tests.sh  (47 项)"
echo "文档: document/17-V0.0.4-验收说明.md"
echo ""
echo "仅网关与平台都在本机时:"
echo "  ${BUILD}/transferFile -c ${LOCAL_CFG}"
echo "  ${BUILD}/platform_sim -c ${LOCAL_CFG} --demo"
