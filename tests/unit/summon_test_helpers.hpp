#pragma once

#include "transfer/mqtt_config.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/transfer_orchestrator.hpp"

#include <string>
#include <string_view>

namespace summon_test {

inline bool extractJsonStringField(std::string_view json, std::string_view key,
                                   std::string& out) {
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

inline std::string contentConfirmJson(uint32_t cmdId, uint32_t segNo, bool success = true) {
    transfer::ContentConfirmRequest req;
    req.cmdId = cmdId;
    req.fileSegNo = segNo;
    req.status = success ? transfer::BriefStatus::Success : transfer::BriefStatus::Failure;
    transfer::JsonProtocolCodec codec;
    return codec.encodeContentConfirm(req);
}

// V0.0.4：对 bus 上已发布的内容段逐条发送平台确认，直至传完或无法继续
inline void driveSummonWithContentConfirms(transfer::TransferOrchestrator& orch,
                                           transfer::SimulatedMqttBus& bus,
                                           const transfer::MqttConfig& mqtt) {
    size_t confirmed = 0;
    for (int guard = 0; guard < 10000; ++guard) {
        size_t contentCount = 0;
        for (const auto& kv : bus.history()) {
            if (kv.first == mqtt.topicContent) ++contentCount;
        }
        if (contentCount <= confirmed) return;

        std::string payload;
        size_t idx = 0;
        for (const auto& kv : bus.history()) {
            if (kv.first == mqtt.topicContent) {
                if (idx == confirmed) {
                    payload = kv.second;
                    break;
                }
                ++idx;
            }
        }
        std::string cmdStr, segStr;
        if (!extractJsonStringField(payload, "CmdId", cmdStr) ||
            !extractJsonStringField(payload, "FileSegNo", segStr)) {
            return;
        }
        orch.onContentConfirm(
            contentConfirmJson(static_cast<uint32_t>(std::stoul(cmdStr)),
                               static_cast<uint32_t>(std::stoul(segStr))));
        ++confirmed;
    }
}

}  // namespace summon_test
