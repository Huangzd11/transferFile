#pragma once

#include <cstdint>
#include <string>

namespace transfer {

class ICrc32Calculator {
public:
    virtual ~ICrc32Calculator() = default;
    virtual uint32_t computeFile(const std::string& path) = 0;
    virtual std::string toHexString(uint32_t crc) const = 0;
};

class Crc32Calculator : public ICrc32Calculator {
public:
    uint32_t computeFile(const std::string& path) override;
    std::string toHexString(uint32_t crc) const override;

    // 对缓冲区计算 CRC（用于测试向量）
    static uint32_t computeBuffer(const uint8_t* data, size_t len);
};

}  // namespace transfer
