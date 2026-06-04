// Base64 编解码（RFC 4648 标准字母表）
// 用于协议 JSON 中 Content 字段：二进制分段经 Base64 编码后嵌入字符串。

#include "transfer/base64.hpp"

namespace transfer {
namespace {

// RFC 4648 标准 64 字符编码表：每 6 位数值 0..63 映射为一个可打印字符
const char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 将单个 Base64 字符解码为 0..63 的 6 位数值；非合法字符返回 -1
int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';           // A-Z → 0..25
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;      // a-z → 26..51
    if (c >= '0' && c <= '9') return c - '0' + 52;      // 0-9 → 52..61
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

// 将二进制数据编码为 Base64 字符串。
// 每 3 字节（24 bit）拆成 4 个 6 bit 索引，查 kEncodeTable 输出 4 字符；
// 尾部不足 3 字节时用 '=' 填充至 4 字符一组。
std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    // 输出长度恒为 ceil(n/3)*4
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    // 主循环：每次处理完整的 3 字节组
    while (i + 3 <= data.size()) {
        // 将 3 字节拼成 24 位整数 n（高位在前）
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        // 依次取出 4 个 6 bit 段并编码
        out.push_back(kEncodeTable[(n >> 18) & 63]);
        out.push_back(kEncodeTable[(n >> 12) & 63]);
        out.push_back(kEncodeTable[(n >> 6) & 63]);
        out.push_back(kEncodeTable[n & 63]);
        i += 3;
    }
    // 尾部：剩余 1 或 2 字节
    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(kEncodeTable[(n >> 18) & 63]);
        out.push_back(kEncodeTable[(n >> 12) & 63]);
        if (i + 1 < data.size()) {
            // 剩余 2 字节：输出 3 个数据字符 + 1 个 '='
            out.push_back(kEncodeTable[(n >> 6) & 63]);
            out.push_back('=');
        } else {
            // 剩余 1 字节：输出 2 个数据字符 + 2 个 '='
            out.push_back(kEncodeTable[(n >> 6) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

// 将 Base64 字符串解码为二进制数据。
// 成功时写入 out 并返回 true；含非法字符时返回 false（out 可能已被 clear）。
// 支持忽略换行、回车、空格；末尾 '=' 按填充规则参与长度计算。
bool base64Decode(const std::string& encoded, std::vector<uint8_t>& out) {
    out.clear();
    std::vector<uint8_t> buf;
    buf.reserve(encoded.size() * 3 / 4);
    int val = 0;    // 位累加器：尚未输出的位保存在 val 中
    int valb = -8;  // 累加器内“下一个输出字节”的起始位偏移（每读 1 字符 +6）
    size_t encLen = encoded.size();
    // 统计末尾连续 '=' 的个数（0、1 或 2）
    size_t padCount = 0;
    while (padCount < encLen && encoded[encLen - 1 - padCount] == '=') {
        ++padCount; // 统计末尾连续 '=' 的个数
    }
    // 参与 6 bit 解码的字符数（不含尾部 '='）
    size_t dataLen = encLen - padCount;     
    for (size_t i = 0; i < dataLen; ++i) { // 遍历Base64字符串
        char c = encoded[i];
        if (c == '\n' || c == '\r' || c == ' ') continue;       
        int d = decodeChar(c);          
        if (d < 0) return false;            
        val = (val << 6) + d;
        valb += 6;
        // 已累积至少 8 位，可输出 1 字节
        if (valb >= 0) {
            buf.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;  
        }
    }
    // 按 4 字符一组及填充数计算期望字节数，截掉流式解码可能多出的尾部字节
    const size_t expectedLen = (encLen / 4) * 3 - padCount;
    if (buf.size() > expectedLen) { 
        buf.resize(expectedLen);    
    }   
    out.swap(buf);
    return true;
}

}  // namespace transfer
