// CRC32 实现（反射多项式 0xEDB88320）
// 初值 0xFFFFFFFF，按字节/块更新，最终异或 0xFFFFFFFF。

#include "transfer/crc32.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

namespace transfer {

namespace {

// 单字节 CRC 递推
uint32_t crc32Update(uint32_t crc, uint8_t byte) {    // 更新CRC
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1U) {
            crc = (crc >> 1) ^ 0xEDB88320U;    // 0xEDB88320 是反射多项式
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

}  // namespace

uint32_t Crc32Calculator::computeBuffer(const uint8_t* data, size_t len) {    // 计算缓冲区CRC
    uint32_t crc = 0xFFFFFFFFU;    // 初始值
    for (size_t i = 0; i < len; ++i) {
        crc = crc32Update(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFFU;    // 最终异或 0xFFFFFFFF
}

uint32_t Crc32Calculator::computeFile(const std::string& path) {    // 计算文件CRC
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;    // 如果文件不存在，则返回0
    uint32_t crc = 0xFFFFFFFFU;    // 初始值
    std::vector<char> buf(65536);    // 缓冲区
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));    // 读取文件
        std::streamsize n = in.gcount();    // 读取字节数   
        for (std::streamsize i = 0; i < n; ++i) {
            crc = crc32Update(crc, static_cast<uint8_t>(buf[static_cast<size_t>(i)]));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

std::string Crc32Calculator::toHexString(uint32_t crc) const {    // 转换为十六进制字符串
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", crc);    // 格式化
    return std::string(buf);
}

}  // namespace transfer
