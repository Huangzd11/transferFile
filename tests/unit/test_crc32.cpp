#include "transfer/crc32.hpp"

#include "../test_compat.hpp"

TEST(Crc32Test, StandardVector123456789) {
    const char* s = "123456789";
    uint32_t crc = transfer::Crc32Calculator::computeBuffer(
        reinterpret_cast<const uint8_t*>(s), 9);
    EXPECT_EQ(crc, 0xCBF43926U);
}

TEST(Crc32Test, HexFormat) {
    transfer::Crc32Calculator calc;
    EXPECT_EQ(calc.toHexString(0xCBF43926U), "0xCBF43926");
}
