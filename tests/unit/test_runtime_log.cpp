#include "../test_compat.hpp"

#include "transfer/runtime_log.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

TEST(RuntimeLogTest, WritesFormattedLineToFile) {
    const std::string dir = "/tmp/transfer_runtime_log_test";
    transfer::log::init(dir);
    transfer::log::gatewayInfo("unit_test_marker");
    transfer::log::shutdown();

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream name;
    name << dir << '/' << std::put_time(&tm, "%Y-%m-%d") << ".log";

    std::ifstream in(name.str());
    ASSERT_TRUE(in.good());
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    EXPECT_TRUE(content.find("[INFO]") != std::string::npos);
    EXPECT_TRUE(content.find("unit_test_marker") != std::string::npos);
    EXPECT_TRUE(content.find("] [") != std::string::npos);
    EXPECT_TRUE(content.find(':') != std::string::npos);
}
