#include "transfer/base64.hpp"

namespace transfer {
namespace {

const char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

}  // namespace

std::string base64Encode(const std::vector<uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
        out.push_back(kEncodeTable[(n >> 18) & 63]);
        out.push_back(kEncodeTable[(n >> 12) & 63]);
        out.push_back(kEncodeTable[(n >> 6) & 63]);
        out.push_back(kEncodeTable[n & 63]);
        i += 3;
    }
    if (i < data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < data.size()) n |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(kEncodeTable[(n >> 18) & 63]);
        out.push_back(kEncodeTable[(n >> 12) & 63]);
        if (i + 1 < data.size()) {
            out.push_back(kEncodeTable[(n >> 6) & 63]);
            out.push_back('=');
        } else {
            out.push_back(kEncodeTable[(n >> 6) & 63]);
            out.push_back('=');
            out.push_back('=');
        }
    }
    return out;
}

bool base64Decode(const std::string& encoded, std::vector<uint8_t>& out) {
    out.clear();
    std::vector<uint8_t> buf;
    buf.reserve(encoded.size() * 3 / 4);
    int val = 0;
    int valb = -8;
    size_t encLen = encoded.size();
    size_t padCount = 0;
    while (padCount < encLen && encoded[encLen - 1 - padCount] == '=') {
        ++padCount;
    }
    size_t dataLen = encLen - padCount;
    for (size_t i = 0; i < dataLen; ++i) {
        char c = encoded[i];
        if (c == '\n' || c == '\r' || c == ' ') continue;
        int d = decodeChar(c);
        if (d < 0) return false;
        val = (val << 6) + d;
        valb += 6;
        if (valb >= 0) {
            buf.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    const size_t expectedLen = (encLen / 4) * 3 - padCount;
    if (buf.size() > expectedLen) {
        buf.resize(expectedLen);
    }
    out.swap(buf);
    return true;
}

}  // namespace transfer
