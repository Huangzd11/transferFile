// 模拟 MQTT 总线端到端单测

#include "../test_compat.hpp"

#include "transfer/crc32.hpp"
#include "transfer/error_codes.hpp"
#include "transfer/file_store.hpp"
#include "transfer/mqtt_adapter.hpp"
#include "transfer/mqtt_config.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/session_store.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/transfer_orchestrator.hpp"

#include <fstream>
#include <memory>

TEST(MqttSimulationTest, EndToEndSummonBriefContent) {
    const std::string path = "/tmp/transfer_mqtt_e2e.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "ABCDE";
    }

    transfer::MqttConfig mqtt = transfer::makeDefaultMqttConfig("testgw");
    transfer::SimulatedMqttBus bus;
    transfer::JsonProtocolCodec codec;
    transfer::FileStore files({"/tmp/"});
    transfer::MemorySessionStore sessions;
    transfer::SteadyClock clock;
    transfer::TimeoutWatchdog watchdog(clock);
    transfer::Crc32Calculator crc;
    transfer::TransferConfig cfg;
    cfg.chunkSize = 2;
    cfg.allowedPathRoots = {"/tmp/"};

    transfer::SimulatedMqttAdapter adapter(bus, mqtt);
    transfer::TransferOrchestrator orch(codec, files, sessions, watchdog, adapter, crc, cfg);
    adapter.setSummonHandler([&orch](std::string_view p) { orch.onSummon(p); });
    adapter.setContentConfirmHandler(
        [&orch](std::string_view p) { orch.onContentConfirm(p); });
    std::string err;
    ASSERT_TRUE(adapter.start(err));

    transfer::PlatformMqttSimulator platform(bus, mqtt);
    platform.publishSummon(R"({"Data":{"CmdId":"77","FullPathFileName":")" + path +
                             R"(","StartByte":"1"}})");

    ASSERT_TRUE(platform.received().size() >= 2u);
    EXPECT_NE(platform.received()[0].second.find("\"Status\":\"0\""), std::string::npos);
    EXPECT_EQ(platform.received()[0].first, mqtt.topicBrief);
    bool sawContent = false;
    for (const auto& m : platform.received()) {
        if (m.first == mqtt.topicContent) sawContent = true;
    }
    EXPECT_TRUE(sawContent);
}

TEST(MqttSimulationTest, DefaultTopicsContainGatewayId) {
    auto mqtt = transfer::makeDefaultMqttConfig("gwXYZ");
    EXPECT_NE(mqtt.topicSummon.find("gwXYZ"), std::string::npos);
    EXPECT_NE(mqtt.topicBrief.find("gwXYZ"), std::string::npos);
}

TEST(ErrorCodesTest, PlaceholderValues) {
    EXPECT_EQ(std::string(transfer::errc::FileNotFound), "FILE_NOT_FOUND");
    EXPECT_EQ(std::string(transfer::errc::Busy), "BUSY");
}
