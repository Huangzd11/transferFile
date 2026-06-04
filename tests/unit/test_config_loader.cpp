// 配置加载单测

#include "../test_compat.hpp"

#include "transfer/config_loader.hpp"

#include <fstream>

TEST(ConfigLoaderTest, LoadFromFile) {
    const std::string path = "/tmp/transfer_test_config.json";
    {
        std::ofstream out(path);
        out << R"({
          "transfer": {
            "timeoutSec": "60",
            "chunkSize": "1024",
            "allowedPathRoots": ["/tmp/"]
          },
          "mqtt": {
            "brokerHost": "10.0.0.1",
            "brokerPort": "8883",
            "gatewayId": "gwTest",
            "useSimulatedBus": "true"
          }
        })";
    }

    transfer::AppConfig app;
    std::string err;
    ASSERT_TRUE(transfer::loadAppConfigFromFile(path, app, err));
    EXPECT_EQ(app.transfer.timeoutSec, 60u);
    EXPECT_EQ(app.transfer.chunkSize, 1024u);
    EXPECT_EQ(app.mqtt.brokerHost, "10.0.0.1");
    EXPECT_EQ(app.mqtt.brokerPort, static_cast<uint16_t>(8883));
    EXPECT_NE(app.mqtt.topicSummon.find("gwTest"), std::string::npos);
}

TEST(ConfigLoaderTest, MissingFileUsesDefaults) {
    transfer::AppConfig app;
    std::string err;
    ASSERT_TRUE(transfer::loadAppConfig("/tmp/nonexistent_transfer_config_xyz.json", app, err));
    EXPECT_TRUE(app.configFilePath.empty());
    EXPECT_EQ(app.transfer.timeoutSec, 180u);
    EXPECT_EQ(app.log.logDir, "log");
    EXPECT_EQ(app.log.maxFileSizeBytes, 10u * 1024 * 1024);
    EXPECT_EQ(app.log.retainDays, 30u);
}

TEST(ConfigLoaderTest, LoadLogSection) {
    const std::string path = "/tmp/transfer_test_log_config.json";
    {
        std::ofstream out(path);
        out << R"({
          "log": {
            "logDir": "/data/transfer/logs",
            "maxFileSizeBytes": "1024",
            "retainDays": "7"
          }
        })";
    }

    transfer::AppConfig app;
    std::string err;
    ASSERT_TRUE(transfer::loadAppConfigFromFile(path, app, err));
    EXPECT_EQ(app.log.logDir, "/data/transfer/logs");
    EXPECT_EQ(app.log.maxFileSizeBytes, 1024u);
    EXPECT_EQ(app.log.retainDays, 7u);
}
