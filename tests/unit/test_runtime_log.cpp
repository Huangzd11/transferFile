// 运行日志单测：目录创建、轮转、保留清理

#include "../test_compat.hpp"

#include "transfer/runtime_log.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>

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

TEST(RuntimeLogTest, RotatesWhenFileExceedsMaxSize) {
    const std::string dir = "/tmp/transfer_runtime_log_rotate_test";
    transfer::LogConfig cfg;
    cfg.logDir = dir;
    cfg.maxFileSizeBytes = 80;
    cfg.retainDays = 0;
    transfer::log::init(cfg);

    for (int i = 0; i < 5; ++i) {
        transfer::log::gatewayInfo(std::string(40, 'x') + std::to_string(i));
    }
    transfer::log::shutdown();

    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    std::ostringstream base;
    base << dir << '/' << std::put_time(&tm, "%Y-%m-%d") << ".log";

    struct stat st {};
    EXPECT_EQ(stat((base.str() + ".1").c_str(), &st), 0);
    EXPECT_EQ(stat(base.str().c_str(), &st), 0);
}

TEST(RuntimeLogTest, PurgesLogsOlderThanRetainDays) {
    const std::string dir = "/tmp/transfer_runtime_log_retain_test";
    mkdir(dir.c_str(), 0755);
    std::remove((dir + "/2020-01-01.log").c_str());
    {
        std::ofstream out(dir + "/2020-01-01.log");
        out << "old log\n";
    }

    transfer::LogConfig cfg;
    cfg.logDir = dir;
    cfg.maxFileSizeBytes = 0;
    cfg.retainDays = 7;
    transfer::log::init(cfg);
    transfer::log::shutdown();

    struct stat st {};
    EXPECT_NE(stat((dir + "/2020-01-01.log").c_str(), &st), 0);
}
