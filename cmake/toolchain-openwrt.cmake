# OpenWrt aarch64 交叉编译工具链
# 工具链包: /home/huangzd/openwrt-gcc-8.3.0.tar.gz

set(TOOLCHAIN_ROOT "$ENV{HOME}/toolchains/openwrt-gcc-8.3.0" CACHE PATH "OpenWrt GCC 根目录")

if(NOT EXISTS "${TOOLCHAIN_ROOT}/bin/aarch64-openwrt-linux-musl-g++")
    message(FATAL_ERROR
        "未找到交叉编译器: ${TOOLCHAIN_ROOT}/bin/aarch64-openwrt-linux-musl-g++\n"
        "  mkdir -p ~/toolchains && tar -xzf /home/huangzd/openwrt-gcc-8.3.0.tar.gz -C ~/toolchains")
endif()

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-openwrt-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/bin/aarch64-openwrt-linux-musl-g++")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# 交叉编译默认启用并链接 libmosquitto（需先运行 scripts/build-mosquitto-cross.sh）
set(TRANSFER_WITH_MOSQUITTO ON CACHE BOOL "交叉编译链接 libmosquitto")

if(NOT DEFINED MOSQUITTO_ROOT)
    set(MOSQUITTO_ROOT "${CMAKE_SOURCE_DIR}/third_party/mosquitto-aarch64" CACHE PATH
        "交叉编译 libmosquitto 安装目录")
endif()
