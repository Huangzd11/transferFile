// 模拟 MQTT 适配器与平台侧模拟器
// SimulatedMqttAdapter 对接总线；PlatformMqttSimulator 用于联调/演示。

#pragma once

#include "transfer/gateway_mqtt.hpp"
#include "transfer/mqtt_config.hpp"
#include "transfer/simulated_mqtt_bus.hpp"

#include <string>
#include <vector>

namespace transfer {

// 内存模拟 MQTT 适配（useSimulatedBus=true）
class SimulatedMqttAdapter : public IGatewayMqtt {
public:
    SimulatedMqttAdapter(SimulatedMqttBus& bus, const MqttConfig& config);

    void setSummonHandler(std::function<void(std::string_view)> handler) override;
    void setPushBriefHandler(std::function<void(std::string_view)> handler) override;
    void setPushContentHandler(std::function<void(std::string_view)> handler) override;
    void setContentConfirmHandler(std::function<void(std::string_view)> handler) override;
    bool start(std::string& errorDetail) override;
    void stop() override;
    int loop(int timeoutMs) override;

    bool publishBrief(std::string_view jsonUtf8) override;
    bool publishContent(std::string_view jsonUtf8) override;
    bool publishPushBriefConfirm(std::string_view jsonUtf8) override;
    bool publishPushContentConfirm(std::string_view jsonUtf8) override;

    const MqttConfig& config() const override { return config_; }

private:
    SimulatedMqttBus& bus_;
    MqttConfig config_;
    std::function<void(std::string_view)> onSummon_;
    std::function<void(std::string_view)> onPushBrief_;
    std::function<void(std::string_view)> onPushContent_;
    std::function<void(std::string_view)> onContentConfirm_;
    bool started_ = false;
};

// 模拟平台侧：向召唤 Topic 发布，并收集简报/内容 Topic 上的应答
class PlatformMqttSimulator {
public:
    PlatformMqttSimulator(SimulatedMqttBus& bus, const MqttConfig& config);

    void publishSummon(const std::string& jsonUtf8);
    void publishPushBrief(const std::string& jsonUtf8);
    void publishPushContent(const std::string& jsonUtf8);

    const std::vector<std::pair<std::string, std::string>>& received() const {
        return received_;
    }
    void clearReceived();

    void subscribePushConfirms();

private:
    SimulatedMqttBus& bus_;
    MqttConfig config_;
    std::vector<std::pair<std::string, std::string>> received_;
};

}  // namespace transfer
