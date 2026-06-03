#include "../test_compat.hpp"

#include "transfer/version.hpp"

#include <regex>
#include <string>

TEST(BuildInfoTest, VersionIsSet) {
    EXPECT_TRUE(std::string(transfer::kVersionString).size() >= 5u);
}

TEST(BuildInfoTest, BuildDateTimeFormat) {
    const std::string dt(transfer::kBuildDateTime);
    // YYYY-MM-DD HH:MM:SS.mmm
    const std::regex pattern(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}$)");
    EXPECT_TRUE(std::regex_match(dt, pattern));
}
