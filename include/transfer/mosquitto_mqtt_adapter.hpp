#pragma once

#include "transfer/gateway_mqtt.hpp"
#include "transfer/mqtt_config.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

struct mosquitto;

namespace transfer {

// libmosquitto 真实 MQTT 适配（useSimulatedBus=false）
class MosquittoMqttAdapter : public IGatewayMqtt {
public:
    explicit MosquittoMqttAdapter(const MqttConfig& config);
    ~MosquittoMqttAdapter() override;

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

    void dispatchSummon(const std::string& payload);
    void dispatchPushBrief(const std::string& payload);
    void dispatchPushContent(const std::string& payload);
    void dispatchContentConfirm(const std::string& payload);

private:
    bool publishTopic(const std::string& topic, std::string_view jsonUtf8);

    MqttConfig config_;
    mosquitto* mosq_ = nullptr;
    std::function<void(std::string_view)> onSummon_;
    std::function<void(std::string_view)> onPushBrief_;
    std::function<void(std::string_view)> onPushContent_;
    std::function<void(std::string_view)> onContentConfirm_;
    std::mutex handlerMutex_;
    std::atomic<bool> connected_{false};
    bool started_ = false;
};

}  // namespace transfer
