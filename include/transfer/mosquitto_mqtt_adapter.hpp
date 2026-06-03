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
    bool start(std::string& errorDetail) override;
    void stop() override;
    int loop(int timeoutMs) override;

    bool publishBrief(std::string_view jsonUtf8) override;
    bool publishContent(std::string_view jsonUtf8) override;

    const MqttConfig& config() const override { return config_; }

    // 供 mosquitto 回调线程调用
    void dispatchSummon(const std::string& payload);

private:
    bool publishTopic(const std::string& topic, std::string_view jsonUtf8);

    MqttConfig config_;
    mosquitto* mosq_ = nullptr;
    std::function<void(std::string_view)> onSummon_;
    std::mutex handlerMutex_;
    std::atomic<bool> connected_{false};
    bool started_ = false;
};

}  // namespace transfer
