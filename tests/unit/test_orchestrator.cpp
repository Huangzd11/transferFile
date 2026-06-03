#include "../test_compat.hpp"

#include "transfer/crc32.hpp"
#include "transfer/file_store.hpp"
#include "transfer/mqtt_adapter.hpp"
#include "transfer/mqtt_config.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/session_store.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/transfer_orchestrator.hpp"
#include "summon_test_helpers.hpp"

#include <fstream>
#include <memory>
#include <string>

namespace {

struct OrchFixture {
    OrchFixture() {
        path_ = "/tmp/transfer_orch_test.bin";
        std::ofstream out(path_, std::ios::binary);
        for (int i = 0; i < 10; ++i) out.put(static_cast<char>('0' + i));
        bus_.clearHistory();
    }

    std::unique_ptr<transfer::TransferOrchestrator> makeOrch(uint32_t chunkSize) {
        transfer::TransferConfig cfg;
        cfg.chunkSize = chunkSize;
        cfg.timeoutSec = 180;
        cfg.allowedPathRoots = {"/tmp/"};
        return std::make_unique<transfer::TransferOrchestrator>(
            codec_, fileStore_, sessions_, watchdog_, mqtt_, crc_, cfg);
    }

    std::string summonJson(uint32_t cmdId, const std::string& path,
                           uint64_t startByte) {
        return R"({"Data":{"CmdId":")" + std::to_string(cmdId) +
               R"(","FullPathFileName":")" + path + R"(","StartByte":")" +
               std::to_string(startByte) + R"("}})";
    }

    transfer::JsonProtocolCodec codec_;
    transfer::FileStore fileStore_{std::vector<std::string>{"/tmp/"}};
    transfer::MemorySessionStore sessions_;
    transfer::SteadyClock clock_;
    transfer::TimeoutWatchdog watchdog_{clock_};
    transfer::SimulatedMqttBus bus_;
    transfer::SimulatedMqttAdapter mqtt_{bus_, transfer::makeDefaultMqttConfig("gw001")};
    transfer::Crc32Calculator crc_;
    std::string path_;
};

}  // namespace

TEST(OrchestratorTest, BriefBeforeContent) {
    OrchFixture f;
    auto orch = f.makeOrch(4);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(1, f.path_, 1));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() >= 2u);
    EXPECT_EQ(hist[0].first, f.mqtt_.config().topicBrief);
    for (size_t i = 1; i < hist.size(); ++i) {
        EXPECT_EQ(hist[i].first, f.mqtt_.config().topicContent);
    }
}

TEST(OrchestratorTest, BriefFailNoContentForMissingFile) {
    OrchFixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(2, "/tmp/nonexistent_transfer_xyz.bin", 1));
    const auto& hist = f.bus_.history();
    EXPECT_EQ(hist.size(), 1u);
    EXPECT_EQ(hist[0].first, f.mqtt_.config().topicBrief);
    EXPECT_NE(hist[0].second.find("\"Status\":\"1\""), std::string::npos);
}

TEST(OrchestratorTest, ResumeFromStartByte) {
    OrchFixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(3, f.path_, 6));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() >= 2u);
    EXPECT_NE(hist[0].second.find("\"Status\":\"0\""), std::string::npos);
}

TEST(OrchestratorTest, LastSegmentContinueZero) {
    OrchFixture f;
    auto orch = f.makeOrch(4);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(4, f.path_, 1));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(!hist.empty());
    EXPECT_NE(hist.back().second.find("\"Continue\":\"0\""), std::string::npos);
}
