// Base64 编解码接口（RFC 4648）
// 供协议 JSON 的 Content 字段在二进制与可打印字符串间转换。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace transfer {

// 将二进制缓冲区编码为 Base64 字符串
std::string base64Encode(const std::vector<uint8_t>& data);
// 将 Base64 字符串解码为二进制；成功返回 true 并写入 out
bool base64Decode(const std::string& encoded, std::vector<uint8_t>& out);

}  // namespace transfer
