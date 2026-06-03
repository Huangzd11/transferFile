#include "transfer/mqtt_adapter.hpp"
#include "transfer/runtime_log.hpp"

namespace transfer {

SimulatedMqttAdapter::SimulatedMqttAdapter(SimulatedMqttBus& bus, const MqttConfig& config)
    : bus_(bus), config_(config) {}

void SimulatedMqttAdapter::setSummonHandler(std::function<void(std::string_view)> handler) {
    onSummon_ = std::move(handler);
}

bool SimulatedMqttAdapter::start(std::string& /*errorDetail*/) {
    if (started_) return true;
    bus_.subscribe(config_.topicSummon, [this](const std::string&, const std::string& payload) {
        log::gatewayInfo("模拟总线收到召唤");
        if (onSummon_) onSummon_(payload);
    });
    started_ = true;
    log::gatewayInfo("模拟 MQTT 总线已启动，等待召唤");
    return true;
}

void SimulatedMqttAdapter::stop() {
    if (!started_) return;
    bus_.unsubscribe(config_.topicSummon);
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

PlatformMqttSimulator::PlatformMqttSimulator(SimulatedMqttBus& bus, const MqttConfig& config)
    : bus_(bus), config_(config) {
    bus_.subscribe(config_.topicBrief, [this](const std::string& topic, const std::string& p) {
        received_.emplace_back(topic, p);
    });
    bus_.subscribe(config_.topicContent, [this](const std::string& topic, const std::string& p) {
        received_.emplace_back(topic, p);
    });
}

void PlatformMqttSimulator::publishSummon(const std::string& jsonUtf8) {
    bus_.publish(config_.topicSummon, jsonUtf8);
}

void PlatformMqttSimulator::clearReceived() { received_.clear(); }

}  // namespace transfer
