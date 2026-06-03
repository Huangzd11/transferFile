#include "../test_compat.hpp"

#include "transfer/base64.hpp"
#include "transfer/crc32.hpp"
#include "transfer/file_store.hpp"
#include "transfer/mqtt_adapter.hpp"
#include "transfer/mqtt_config.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/push_protocol_codec.hpp"
#include "transfer/push_receive_orchestrator.hpp"
#include "transfer/push_session_store.hpp"
#include "transfer/session_store.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/transfer_orchestrator.hpp"

#include <fstream>

TEST(PushE2eTest, PlatformPushBriefAndContent) {
    const std::string gatewayPath = "/tmp/transfer_push_e2e_dst.bin";
    const std::string body = "push-e2e-payload";
    std::remove(gatewayPath.c_str());

    transfer::Crc32Calculator crcCalc;
    const uint32_t crcVal = crcCalc.computeBuffer(
        reinterpret_cast<const uint8_t*>(body.data()), body.size());
    const std::string crcHex = crcCalc.toHexString(crcVal);

    transfer::MqttConfig mqtt = transfer::makeDefaultMqttConfig("pushe2e");
    transfer::SimulatedMqttBus bus;
    transfer::TransferConfig cfg;
    cfg.chunkSize = 4;
    cfg.allowedPathRoots = {"/tmp/"};

    transfer::JsonProtocolCodec summonCodec;
    transfer::JsonPushProtocolCodec pushCodec;
    transfer::FileStore files({"/tmp/"});
    transfer::MemorySessionStore summonSessions;
    transfer::MemoryPushSessionStore pushSessions;
    transfer::SteadyClock clock;
    transfer::TimeoutWatchdog watchdog(clock);
    transfer::Crc32Calculator crc;

    transfer::SimulatedMqttAdapter gw(bus, mqtt);
    transfer::TransferOrchestrator summonOrch(summonCodec, files, summonSessions, watchdog,
                                            gw, crc, cfg);
    transfer::PushReceiveOrchestrator pushOrch(pushCodec, files, pushSessions, watchdog, gw,
                                               cfg);

    summonOrch.setBusyChecker([&]() { return pushSessions.hasActiveSession(); });
    pushOrch.setBusyChecker([&]() {
        return summonSessions.hasActiveSessionOtherThan(0);
    });

    gw.setSummonHandler([&](std::string_view p) { summonOrch.onSummon(p); });
    gw.setPushBriefHandler([&](std::string_view p) { pushOrch.onPushBrief(p); });
    gw.setPushContentHandler([&](std::string_view p) { pushOrch.onPushContent(p); });

    std::string err;
    ASSERT_TRUE(gw.start(err));

    transfer::PlatformMqttSimulator platform(bus, mqtt);
    platform.subscribePushConfirms();

    const std::string brief = R"({"Data":{"CmdId":"8801","FullPathFileName":")" + gatewayPath +
                              R"(","FileCrc":")" + crcHex + R"(","FileSize":")" +
                              std::to_string(body.size()) +
                              R"(","ModifyTime":"2026-06-01 12:00:00"}})";
    platform.publishPushBrief(brief);

    ASSERT_GE(platform.received().size(), 1u);
    EXPECT_EQ(platform.received()[0].first, mqtt.topicPushBriefConfirm);
    EXPECT_NE(platform.received()[0].second.find("\"Status\":\"0\""), std::string::npos);

    std::vector<uint8_t> raw(body.begin(), body.end());
    const std::string content = R"({"Data":{"CmdId":"8801","FileSegNo":"1","Content":")" +
                                transfer::base64Encode(raw) + R"(","Continue":"0"}})";
    platform.publishPushContent(content);

    ASSERT_GE(platform.received().size(), 2u);
    EXPECT_NE(platform.received()[1].second.find("\"Status\":\"0\""), std::string::npos);

    std::ifstream in(gatewayPath, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(got, body);
}
