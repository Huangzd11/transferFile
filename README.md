# transferFile — 网关侧 MQTT 文件传输

**当前版本：V0.0.3** | C++17 | CMake | 网关 + 开发机平台模拟

## 功能概要

- **召唤上传**：平台召唤 → 网关读本地文件 → 简报 + 内容上传；**断点续传**（`StartByte`）
- **平台推送**（V0.0.3）：平台推简报 + 内容 → 网关写入已知路径；每段确认；**无断点续传**
- 简报失败则不传/不收后续内容；**180s** 超时
- 本仓库实现**网关** `transferFile`；联调工具 **platform_sim** 在开发机模拟平台

## 部署拓扑（推荐）

| 机器 | 程序 | 配置 |
|------|------|------|
| 开发机 | `mosquitto`、`platform_sim` | `config/transferFile.platform.json` |
| 目标机（板子） | `transferFile` | `config/transferFile.gateway.target.json` |

## 快速开始

```bash
# 开发机：编译 + 自动化验收（42 项测试）
./scripts/build-native.sh
./scripts/run-acceptance-tests.sh

# 目标机：交叉编译产物
./scripts/build-openwrt.sh
./scripts/package-target.sh          # 打包部署目录

# 联调说明
./scripts/debug-mqtt-local.sh
```

**目标机运行示例：**

```bash
tar -xzf dist/transferFile-target-aarch64.tar.gz -C /opt/transfer
export LD_LIBRARY_PATH=/opt/transfer/lib:$LD_LIBRARY_PATH
echo "test" > /tmp/platform_test_file.bin
/opt/transfer/bin/transferFile -c /opt/transfer/config/transferFile.gateway.target.json
```

**开发机发召唤：**

```bash
./build/platform_sim -c config/transferFile.platform.json \
  --gateway-file /tmp/platform_test_file.bin
# 断点续传：加 --start-byte 4097

# 开发机推送文件到目标机网关路径：
./build/platform_sim -c config/transferFile.platform.json \
  --push-file ./my.bin --gateway-path /tmp/my.bin
```

## 目录结构

```
transferFile/
├── src/              # 网关与 platform_sim 源码
├── include/transfer/ # 头文件
├── config/           # JSON 配置（平台 / 目标机 / 冒烟）
├── document/         # 设计文档与验收说明（从 00-README 索引）
├── scripts/          # 构建、联调、打包脚本
├── tests/            # TDD 单元/集成测试
├── build/            # 本机编译输出
└── build-openwrt/    # 交叉编译 transferFile
```

## 文档索引

详见 [document/00-README.md](document/00-README.md)。

| 文档 | 说明 |
|------|------|
| [10-MQTT本机联调.md](document/10-MQTT本机联调.md) | 开发机平台 + 目标机网关 |
| [12-V0.0.2-验收说明.md](document/12-V0.0.2-验收说明.md) | 召唤 R3/R4/R5/大文件验收 |
| [15-V0.0.3-验收说明.md](document/15-V0.0.3-验收说明.md) | 推送 P1～P4 验收 |
| [14-V0.0.3-平台推送.md](document/14-V0.0.3-平台推送.md) | 推送协议与联调 |
| [13-项目状态与路线图.md](document/13-项目状态与路线图.md) | 当前状态与下一步建议 |
| [07-构建与交叉编译.md](document/07-构建与交叉编译.md) | 工具链与 OpenWrt 构建 |

## 工具链

交叉编译工具链：`/home/huangzd/openwrt-gcc-8.3.0.tar.gz`（脚本自动解压到 `~/toolchains/openwrt-gcc-8.3.0`）。
