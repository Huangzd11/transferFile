#include "../test_compat.hpp"

#include "transfer/base64.hpp"
#include "transfer/crc32.hpp"
#include "transfer/file_store.hpp"
#include "transfer/mqtt_adapter.hpp"
#include "transfer/mqtt_config.hpp"
#include "transfer/push_protocol_codec.hpp"
#include "transfer/push_receive_orchestrator.hpp"
#include "transfer/push_session_store.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/timeout_watchdog.hpp"

#include <fstream>
#include <memory>

namespace {

struct PushFixture {
    PushFixture() {
        dstPath_ = "/tmp/transfer_push_recv.bin";
        std::remove(dstPath_.c_str());
        srcBody_ = "0123456789";
        transfer::Crc32Calculator crc;
        crcVal_ = crc.computeBuffer(reinterpret_cast<const uint8_t*>(srcBody_.data()),
                                    srcBody_.size());
        crcHex_ = crc.toHexString(crcVal_);
        fileSize_ = srcBody_.size();
    }

    std::unique_ptr<transfer::PushReceiveOrchestrator> makeOrch() {
        transfer::TransferConfig cfg;
        cfg.timeoutSec = 180;
        cfg.allowedPathRoots = {"/tmp/"};
        return std::make_unique<transfer::PushReceiveOrchestrator>(
            pushCodec_, fileStore_, pushSessions_, watchdog_, mqtt_, cfg);
    }

    std::string briefJson(uint32_t cmdId) const {
        return R"({"Data":{"CmdId":")" + std::to_string(cmdId) +
               R"(","FullPathFileName":")" + dstPath_ + R"(","FileCrc":")" + crcHex_ +
               R"(","FileSize":")" + std::to_string(fileSize_) +
               R"(","ModifyTime":"2026-06-01 12:00:00"}})";
    }

    std::string contentJson(uint32_t cmdId, uint32_t segNo, std::string_view chunk,
                            bool cont) const {
        std::vector<uint8_t> raw(chunk.begin(), chunk.end());
        return R"({"Data":{"CmdId":")" + std::to_string(cmdId) + R"(","FileSegNo":")" +
               std::to_string(segNo) + R"(","Content":")" + transfer::base64Encode(raw) +
               R"(","Continue":")" + std::string(cont ? "1" : "0") + R"("}})";
    }

    transfer::JsonPushProtocolCodec pushCodec_;
    transfer::FileStore fileStore_{std::vector<std::string>{"/tmp/"}};
    transfer::MemoryPushSessionStore pushSessions_;
    transfer::SteadyClock clock_;
    transfer::TimeoutWatchdog watchdog_{clock_};
    transfer::SimulatedMqttBus bus_;
    transfer::SimulatedMqttAdapter mqtt_{bus_, transfer::makeDefaultMqttConfig("pushgw")};
    std::string dstPath_;
    std::string srcBody_;
    uint32_t crcVal_ = 0;
    std::string crcHex_;
    size_t fileSize_ = 0;
};

}  // namespace

TEST(PushOrchestratorTest, BriefConfirmThenContentWritten) {
    PushFixture f;
    auto orch = f.makeOrch();
    f.bus_.clearHistory();
    std::string err;
    ASSERT_TRUE(f.mqtt_.start(err));

    orch->onPushBrief(f.briefJson(501));
    const auto& hist = f.bus_.history();
    ASSERT_GE(hist.size(), 1u);
    EXPECT_EQ(hist[0].first, f.mqtt_.config().topicPushBriefConfirm);
    EXPECT_NE(hist[0].second.find("\"Status\":\"0\""), std::string::npos);

    f.bus_.clearHistory();
    orch->onPushContent(f.contentJson(501, 1, f.srcBody_, false));

    ASSERT_GE(f.bus_.history().size(), 1u);
    EXPECT_NE(f.bus_.history().back().second.find("\"Status\":\"0\""), std::string::npos);

    std::ifstream in(f.dstPath_, std::ios::binary);
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(body, f.srcBody_);
}

TEST(PushOrchestratorTest, BriefFailOnInvalidPath) {
    PushFixture f;
    auto orch = f.makeOrch();
    f.bus_.clearHistory();
    const std::string json = R"({"Data":{"CmdId":"502","FullPathFileName":"/etc/passwd",)"
                             R"("FileCrc":"0x0","FileSize":"1","ModifyTime":"2026-06-01 12:00:00"}})";
    orch->onPushBrief(json);
    ASSERT_EQ(f.bus_.history().size(), 1u);
    EXPECT_NE(f.bus_.history()[0].second.find("\"Status\":\"1\""), std::string::npos);
    EXPECT_NE(f.bus_.history()[0].second.find("INVALID_PATH"), std::string::npos);
}

TEST(PushOrchestratorTest, WrongSegNoAborts) {
    PushFixture f;
    auto orch = f.makeOrch();
    std::string err;
    ASSERT_TRUE(f.mqtt_.start(err));
    orch->onPushBrief(f.briefJson(503));
    f.bus_.clearHistory();
    orch->onPushContent(f.contentJson(503, 2, "x", false));
    EXPECT_NE(f.bus_.history().back().second.find("INVALID_SEG_NO"), std::string::npos);
}

TEST(PushOrchestratorTest, MultiSegmentPush) {
    PushFixture f;
    const std::string body = "ABCDEF";
    f.srcBody_ = body;
    f.fileSize_ = body.size();
    transfer::Crc32Calculator crc;
    f.crcVal_ = crc.computeBuffer(reinterpret_cast<const uint8_t*>(body.data()), body.size());
    f.crcHex_ = crc.toHexString(f.crcVal_);

    auto orch = f.makeOrch();
    std::string err;
    ASSERT_TRUE(f.mqtt_.start(err));
    orch->onPushBrief(f.briefJson(504));
    orch->onPushContent(f.contentJson(504, 1, "ABC", true));
    orch->onPushContent(f.contentJson(504, 2, "DEF", false));

    std::ifstream in(f.dstPath_, std::ios::binary);
    std::string got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(got, body);
}
