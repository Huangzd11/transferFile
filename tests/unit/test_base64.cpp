#include "transfer/base64.hpp"

#include "../test_compat.hpp"

#include <string>

TEST(Base64Test, RoundTrip) {
    std::vector<uint8_t> raw = {0x01, 0x02, 0x03, 0x04, 0xFF};
    std::string enc = transfer::base64Encode(raw);
    std::vector<uint8_t> dec;
    ASSERT_TRUE(transfer::base64Decode(enc, dec));
    EXPECT_TRUE(raw == dec);
}

TEST(Base64Test, Hello) {
    std::vector<uint8_t> raw = {'H', 'e', 'l', 'l', 'o'};
    EXPECT_EQ(transfer::base64Encode(raw), "SGVsbG8=");
}

// 与 chunkSize=4096 对齐，防止解码多出填充字节
TEST(Base64Test, RoundTrip4096Bytes) {
    std::vector<uint8_t> raw(4096);
    for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>(i % 256);
    std::string enc = transfer::base64Encode(raw);
    std::vector<uint8_t> dec;
    ASSERT_TRUE(transfer::base64Decode(enc, dec));
    ASSERT_TRUE(dec.size() == raw.size());
    EXPECT_TRUE(raw == dec);
}
