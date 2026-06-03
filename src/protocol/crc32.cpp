#include "transfer/crc32.hpp"

#include <cstdio>
#include <fstream>
#include <vector>

namespace transfer {

namespace {

uint32_t crc32Update(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1U) {
            crc = (crc >> 1) ^ 0xEDB88320U;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

}  // namespace

uint32_t Crc32Calculator::computeBuffer(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; ++i) {
        crc = crc32Update(crc, data[i]);
    }
    return crc ^ 0xFFFFFFFFU;
}

uint32_t Crc32Calculator::computeFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0;
    uint32_t crc = 0xFFFFFFFFU;
    std::vector<char> buf(65536);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            crc = crc32Update(crc, static_cast<uint8_t>(buf[static_cast<size_t>(i)]));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

std::string Crc32Calculator::toHexString(uint32_t crc) const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", crc);
    return std::string(buf);
}

}  // namespace transfer
