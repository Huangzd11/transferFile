// V0.0.4 单测：平台逐段内容确认、确认失败中止

#include "../test_compat.hpp"
#include "summon_test_helpers.hpp"

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

namespace {

struct V004Fixture {
    V004Fixture() {
        path_ = "/tmp/transfer_v004_test.bin";
        std::ofstream out(path_, std::ios::binary);
        for (int i = 0; i < 10; ++i) out.put(static_cast<char>('0' + i));
    }

    std::unique_ptr<transfer::TransferOrchestrator> makeOrch(uint32_t chunkSize) {
        transfer::TransferConfig cfg;
        cfg.chunkSize = chunkSize;
        cfg.timeoutSec = 180;
        cfg.allowedPathRoots = {"/tmp/"};
        auto orch = std::make_unique<transfer::TransferOrchestrator>(
            codec_, fileStore_, sessions_, watchdog_, mqtt_, crc_, cfg);
        watchdog_.setCallback([&](uint32_t id) { orch->onTimeout(id); });
        return orch;
    }

    std::string summonJson(uint32_t cmdId) {
        return R"({"Data":{"CmdId":")" + std::to_string(cmdId) +
               R"(","FullPathFileName":")" + path_ + R"(","StartByte":"1"}})";
    }

    transfer::JsonProtocolCodec codec_;
    transfer::FileStore fileStore_{std::vector<std::string>{"/tmp/"}};
    transfer::MemorySessionStore sessions_;
    transfer::SteadyClock clock_;
    transfer::TimeoutWatchdog watchdog_{clock_};
    transfer::SimulatedMqttBus bus_;
    transfer::SimulatedMqttAdapter mqtt_{bus_, transfer::makeDefaultMqttConfig("gw004")};
    transfer::Crc32Calculator crc_;
    std::string path_;
};

}  // namespace

// 未确认前只发第一段
TEST(OrchestratorV004Test, OnlyFirstSegmentBeforeConfirm) {
    V004Fixture f;
    auto orch = f.makeOrch(4);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(701));
    size_t n = 0;
    for (const auto& kv : f.bus_.history()) {
        if (kv.first == f.mqtt_.config().topicContent) ++n;
    }
    EXPECT_EQ(n, 1u);
}

// 逐段确认后传完全部 10 字节（chunk=4 → 3 段）
TEST(OrchestratorV004Test, AllSegmentsAfterConfirms) {
    V004Fixture f;
    auto orch = f.makeOrch(4);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(702));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    size_t n = 0;
    for (const auto& kv : f.bus_.history()) {
        if (kv.first == f.mqtt_.config().topicContent) ++n;
    }
    EXPECT_EQ(n, 3u);
    EXPECT_FALSE(f.sessions_.getByCmdId(702).has_value());
}

// 平台确认失败则中止
TEST(OrchestratorV004Test, AbortOnConfirmFailure) {
    V004Fixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(703));
    orch->onContentConfirm(summon_test::contentConfirmJson(703, 1, false));
    EXPECT_FALSE(f.sessions_.getByCmdId(703).has_value());
    size_t n = 0;
    for (const auto& kv : f.bus_.history()) {
        if (kv.first == f.mqtt_.config().topicContent) ++n;
    }
    EXPECT_EQ(n, 1u);
}
