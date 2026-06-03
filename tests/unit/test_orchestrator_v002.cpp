/**
 * V0.0.2 验收测试：R3 简报失败无内容、R4 超时后会话清除、R5 断点续传、大文件分段
 */
#include "../test_compat.hpp"

#include "transfer/base64.hpp"
#include "transfer/error_codes.hpp"
#include "transfer/file_store.hpp"
#include "transfer/mqtt_adapter.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/session_store.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/transfer_orchestrator.hpp"
#include "summon_test_helpers.hpp"

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

class FakeClock : public transfer::IClock {
public:
    std::chrono::steady_clock::time_point now() const override { return now_; }
    void advance(std::chrono::seconds s) { now_ += s; }

private:
    std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::now()};
};

bool extractJsonStringField(std::string_view json, std::string_view key, std::string& out) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            out += json[pos++];
            continue;
        }
        if (c == '"') return true;
        out += c;
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> contentOnly(
    const std::vector<std::pair<std::string, std::string>>& hist,
    const std::string& topicContent) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& kv : hist) {
        if (kv.first == topicContent) out.push_back(kv);
    }
    return out;
}

bool decodeContentPayload(const std::string& json, std::vector<uint8_t>& raw, bool& cont) {
    std::string b64, contFlag;
    if (!extractJsonStringField(json, "Content", b64)) return false;
    if (!transfer::base64Decode(b64, raw)) return false;
    extractJsonStringField(json, "Continue", contFlag);
    cont = (contFlag == "1");
    return true;
}

struct V002Fixture {
    V002Fixture() {
        smallPath_ = "/tmp/transfer_v002_small.bin";
        std::ofstream out(smallPath_, std::ios::binary);
        for (int i = 0; i < 12; ++i) out.put(static_cast<char>('A' + i));  // ABCDEFGHIJKL

        largePath_ = "/tmp/transfer_v002_large.bin";
        {
            std::ofstream lg(largePath_, std::ios::binary | std::ios::trunc);
            for (int i = 0; i < 10000; ++i) lg.put(static_cast<char>(i % 256));
            lg.flush();
        }
    }

    std::unique_ptr<transfer::TransferOrchestrator> makeOrch(uint32_t chunkSize,
                                                            uint32_t timeoutSec = 180) {
        transfer::TransferConfig cfg;
        cfg.chunkSize = chunkSize;
        cfg.timeoutSec = timeoutSec;
        cfg.allowedPathRoots = {"/tmp/"};
        auto orch = std::make_unique<transfer::TransferOrchestrator>(
            codec_, fileStore_, sessions_, watchdog_, mqtt_, crc_, cfg);
        watchdog_.setCallback([&](uint32_t id) { orch->onTimeout(id); });
        return orch;
    }

    std::string summonJson(uint32_t cmdId, const std::string& path, uint64_t startByte) {
        return R"({"Data":{"CmdId":")" + std::to_string(cmdId) +
               R"(","FullPathFileName":")" + path + R"(","StartByte":")" +
               std::to_string(startByte) + R"("}})";
    }

    transfer::JsonProtocolCodec codec_;
    transfer::FileStore fileStore_{std::vector<std::string>{"/tmp/"}};
    transfer::MemorySessionStore sessions_;
    FakeClock clock_;
    transfer::TimeoutWatchdog watchdog_{clock_};
    transfer::SimulatedMqttBus bus_;
    transfer::SimulatedMqttAdapter mqtt_{bus_, transfer::makeDefaultMqttConfig("gw001")};
    transfer::Crc32Calculator crc_;
    std::string smallPath_;
    std::string largePath_;
};

}  // namespace

// R3：非法路径 → 仅简报失败，无内容
TEST(OrchestratorV002Test, R3_BriefFailInvalidPathNoContent) {
    V002Fixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(301, "/etc/passwd", 1));
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() == 1u);
    EXPECT_NE(hist[0].second.find("\"Status\":\"1\""), std::string::npos);
    EXPECT_NE(hist[0].second.find(transfer::errc::InvalidPath), std::string::npos);
}

// R3：StartByte 超出文件尾 → INVALID_START_BYTE
TEST(OrchestratorV002Test, R3_BriefFailStartBeyondEnd) {
    V002Fixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(302, f.smallPath_, 14));  // 12 字节文件，StartByte=14 非法
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() == 1u);
    EXPECT_NE(hist[0].second.find(transfer::errc::InvalidStartByte), std::string::npos);
}

// R3：另一会话占用中 → BUSY
TEST(OrchestratorV002Test, R3_BriefFailBusyNoContent) {
    V002Fixture f;
    auto orch = f.makeOrch(4096);
    transfer::SessionRecord active;
    active.cmdId = 1;
    active.state = transfer::SessionState::SendingContent;
    active.fullPathFileName = f.smallPath_;
    f.sessions_.upsert(active);

    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(303, f.smallPath_, 1));
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() == 1u);
    EXPECT_NE(hist[0].second.find(transfer::errc::Busy), std::string::npos);
}

// R5：续传首段内容与文件偏移一致，FileSegNo 从 1 重新计数
TEST(OrchestratorV002Test, R5_ResumeContentMatchesOffsetAndSegNoReset) {
    V002Fixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    // StartByte=7 → 0-based 偏移 6，首字节应为 'G'
    orch->onSummon(f.summonJson(501, f.smallPath_, 7));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() >= 2u);
    auto contents = contentOnly(hist, f.mqtt_.config().topicContent);
    ASSERT_TRUE(contents.size() == 1u);
    EXPECT_NE(contents[0].second.find("\"FileSegNo\":\"1\""), std::string::npos);

    std::vector<uint8_t> raw;
    bool cont = true;
    ASSERT_TRUE(decodeContentPayload(contents[0].second, raw, cont));
    ASSERT_TRUE(raw.size() == 6u);  // GHIJKL
    ASSERT_TRUE(raw[0] == static_cast<uint8_t>('G'));
    EXPECT_FALSE(cont);
}

// R5：模拟传一半后超时，再冷续传
TEST(OrchestratorV002Test, R5_ColdResumeAfterTimeout) {
    V002Fixture f;
    auto orch = f.makeOrch(4, 60);
    // 先传 4 字节（ABCD）
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(502, f.smallPath_, 1));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    auto hist1 = contentOnly(f.bus_.history(), f.mqtt_.config().topicContent);
    ASSERT_TRUE(hist1.size() == 3u);

    // 模拟传输中断：注入未完成的会话并触发超时
    transfer::SessionRecord stalled;
    stalled.cmdId = 503;
    stalled.state = transfer::SessionState::SendingContent;
    stalled.fullPathFileName = f.smallPath_;
    stalled.fileSize = 12;
    stalled.nextFileOffset = 4;
    f.sessions_.upsert(stalled);
    f.watchdog_.arm(503, 5);
    f.clock_.advance(std::chrono::seconds(6));
    f.watchdog_.tick();
    EXPECT_FALSE(f.sessions_.getByCmdId(503).has_value());

    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(503, f.smallPath_, 5));  // 从第 5 字节续传 → 'E'
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    auto hist2 = contentOnly(f.bus_.history(), f.mqtt_.config().topicContent);
    ASSERT_TRUE(hist2.size() >= 1u);
    std::vector<uint8_t> raw;
    bool cont = false;
    ASSERT_TRUE(decodeContentPayload(hist2[0].second, raw, cont));
    ASSERT_TRUE(raw[0] == static_cast<uint8_t>('E'));
}

// R4：超时回调清除会话（与 R5 冷续传配合）
TEST(OrchestratorV002Test, R4_TimeoutRemovesActiveSession) {
    V002Fixture f;
    auto orch = f.makeOrch(4096, 30);
    transfer::SessionRecord stalled;
    stalled.cmdId = 404;
    stalled.state = transfer::SessionState::SendingContent;
    stalled.fileSize = 100;
    f.sessions_.upsert(stalled);
    f.watchdog_.arm(404, 10);
    f.clock_.advance(std::chrono::seconds(11));
    f.watchdog_.tick();
    EXPECT_FALSE(f.sessions_.getByCmdId(404).has_value());
    (void)orch;
}

// R5：StartByte == fileSize+1 → 简报成功、0 内容段
TEST(OrchestratorV002Test, R5_StartByteAtEndBriefOnly) {
    V002Fixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(505, f.smallPath_, 13));  // 12 字节 + 1
    const auto& hist = f.bus_.history();
    ASSERT_TRUE(hist.size() == 1u);
    EXPECT_NE(hist[0].second.find("\"Status\":\"0\""), std::string::npos);
}

// 大文件：10000 字节、chunk 4096 → 3 段，末段 Continue=0
TEST(OrchestratorV002Test, LargeFile_MultiSegmentContinue) {
    V002Fixture f;
    auto orch = f.makeOrch(4096);
    f.bus_.clearHistory();
    orch->onSummon(f.summonJson(601, f.largePath_, 1));
    summon_test::driveSummonWithContentConfirms(*orch, f.bus_, f.mqtt_.config());
    auto contents = contentOnly(f.bus_.history(), f.mqtt_.config().topicContent);
    ASSERT_TRUE(contents.size() == 3u);

    size_t total = 0;
    for (size_t i = 0; i < contents.size(); ++i) {
        std::vector<uint8_t> raw;
        bool cont = false;
        ASSERT_TRUE(decodeContentPayload(contents[i].second, raw, cont));
        total += raw.size();
        if (i + 1 < contents.size()) {
            EXPECT_TRUE(cont);
        } else {
            EXPECT_FALSE(cont);
            ASSERT_TRUE(raw.size() == 10000u - 4096u * 2u);
        }
    }
    ASSERT_TRUE(total == 10000u);
}
