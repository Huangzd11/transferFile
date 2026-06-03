#include "transfer/mqtt_adapter.hpp"
#include "transfer/runtime_log.hpp"

#include <sstream>
#include <string_view>

namespace transfer {
namespace {

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

std::string makeSummonContentConfirmJson(const std::string& contentPayload) {
    std::string cmdId, segNo;
    if (!extractJsonStringField(contentPayload, "CmdId", cmdId) ||
        !extractJsonStringField(contentPayload, "FileSegNo", segNo)) {
        return {};
    }
    return R"({"Data":{"CmdId":")" + cmdId + R"(","FileSegNo":")" + segNo +
           R"(","Status":"0","ErrorCode":"","Note":""}})";
}

}  // namespace

SimulatedMqttAdapter::SimulatedMqttAdapter(SimulatedMqttBus& bus, const MqttConfig& config)
    : bus_(bus), config_(config) {}

void SimulatedMqttAdapter::setSummonHandler(std::function<void(std::string_view)> handler) {
    onSummon_ = std::move(handler);
}

void SimulatedMqttAdapter::setPushBriefHandler(std::function<void(std::string_view)> handler) {
    onPushBrief_ = std::move(handler);
}

void SimulatedMqttAdapter::setPushContentHandler(std::function<void(std::string_view)> handler) {
    onPushContent_ = std::move(handler);
}

void SimulatedMqttAdapter::setContentConfirmHandler(
    std::function<void(std::string_view)> handler) {
    onContentConfirm_ = std::move(handler);
}

bool SimulatedMqttAdapter::start(std::string& /*errorDetail*/) {
    if (started_) return true;
    bus_.subscribe(config_.topicSummon, [this](const std::string&, const std::string& payload) {
        log::gatewayInfo("模拟总线收到召唤");
        if (onSummon_) onSummon_(payload);
    });
    bus_.subscribe(config_.topicPushBrief, [this](const std::string&, const std::string& payload) {
        log::gatewayInfo("模拟总线收到推送简报");
        if (onPushBrief_) onPushBrief_(payload);
    });
    bus_.subscribe(config_.topicPushContent,
                   [this](const std::string&, const std::string& payload) {
                       log::gatewayInfo("模拟总线收到推送内容");
                       if (onPushContent_) onPushContent_(payload);
                   });
    bus_.subscribe(config_.topicContentConfirm,
                   [this](const std::string&, const std::string& payload) {
                       log::gatewayInfo("模拟总线收到内容确认");
                       if (onContentConfirm_) onContentConfirm_(payload);
                   });
    started_ = true;
    log::gatewayInfo("模拟 MQTT 总线已启动，等待召唤/推送");
    return true;
}

void SimulatedMqttAdapter::stop() {
    if (!started_) return;
    bus_.unsubscribe(config_.topicSummon);
    bus_.unsubscribe(config_.topicPushBrief);
    bus_.unsubscribe(config_.topicPushContent);
    bus_.unsubscribe(config_.topicContentConfirm);
    started_ = false;
}

int SimulatedMqttAdapter::loop(int /*timeoutMs*/) { return 0; }

bool SimulatedMqttAdapter::publishBrief(std::string_view jsonUtf8) {
    bus_.publish(config_.topicBrief, std::string(jsonUtf8));
    return true;
}

bool SimulatedMqttAdapter::publishContent(std::string_view jsonUtf8) {
    bus_.publish(config_.topicContent, std::string(jsonUtf8));
    return true;
}

bool SimulatedMqttAdapter::publishPushBriefConfirm(std::string_view jsonUtf8) {
    bus_.publish(config_.topicPushBriefConfirm, std::string(jsonUtf8));
    return true;
}

bool SimulatedMqttAdapter::publishPushContentConfirm(std::string_view jsonUtf8) {
    bus_.publish(config_.topicPushContentConfirm, std::string(jsonUtf8));
    return true;
}

PlatformMqttSimulator::PlatformMqttSimulator(SimulatedMqttBus& bus, const MqttConfig& config)
    : bus_(bus), config_(config) {
    bus_.subscribe(config_.topicBrief, [this](const std::string& topic, const std::string& p) {
        received_.emplace_back(topic, p);
    });
    bus_.subscribe(config_.topicContent, [this](const std::string& topic, const std::string& p) {
        received_.emplace_back(topic, p);
        const std::string confirmJson = makeSummonContentConfirmJson(p);
        if (!confirmJson.empty()) {
            bus_.publish(config_.topicContentConfirm, confirmJson);
        }
    });
}

void PlatformMqttSimulator::publishSummon(const std::string& jsonUtf8) {
    bus_.publish(config_.topicSummon, jsonUtf8);
}

void PlatformMqttSimulator::publishPushBrief(const std::string& jsonUtf8) {
    bus_.publish(config_.topicPushBrief, jsonUtf8);
}

void PlatformMqttSimulator::publishPushContent(const std::string& jsonUtf8) {
    bus_.publish(config_.topicPushContent, jsonUtf8);
}

void PlatformMqttSimulator::subscribePushConfirms() {
    bus_.subscribe(config_.topicPushBriefConfirm,
                   [this](const std::string& topic, const std::string& p) {
                       received_.emplace_back(topic, p);
                   });
    bus_.subscribe(config_.topicPushContentConfirm,
                   [this](const std::string& topic, const std::string& p) {
                       received_.emplace_back(topic, p);
                   });
}

void PlatformMqttSimulator::clearReceived() { received_.clear(); }

}  // namespace transfer
