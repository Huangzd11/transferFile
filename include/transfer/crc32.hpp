// CRC32 计算接口
// 用于召唤上传简报中的 FileCrc 及推送完成后的文件校验。

#pragma once

#include <cstdint>
#include <string>

namespace transfer {

// CRC32 计算器抽象（便于测试注入）
class ICrc32Calculator {
public:
    virtual ~ICrc32Calculator() = default;
    virtual uint32_t computeFile(const std::string& path) = 0;
    virtual std::string toHexString(uint32_t crc) const = 0;
};

// 基于 IEEE 多项式 0xEDB88320 的 CRC32 实现
class Crc32Calculator : public ICrc32Calculator {
public:
    uint32_t computeFile(const std::string& path) override;
    std::string toHexString(uint32_t crc) const override;

    // 对内存缓冲区计算 CRC（单测向量用）
    static uint32_t computeBuffer(const uint8_t* data, size_t len);
};

}  // namespace transfer
