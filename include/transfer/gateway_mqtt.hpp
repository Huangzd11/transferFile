#pragma once

#include "transfer/mqtt_config.hpp"
#include "transfer/mqtt_publisher.hpp"
#include "transfer/simulated_mqtt_bus.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace transfer {

// 网关 MQTT 传输抽象：订阅召唤、发布简报/内容
class IGatewayMqtt : public IMqttPublisher {
public:
    ~IGatewayMqtt() override = default;

    virtual void setSummonHandler(std::function<void(std::string_view)> handler) = 0;
    virtual bool start(std::string& errorDetail) = 0;
    virtual void stop() = 0;

    // 处理网络事件；timeoutMs 为 mosquitto_loop 等待毫秒数
    virtual int loop(int timeoutMs) = 0;

    virtual const MqttConfig& config() const = 0;
};

// useSimulatedBus=true 时 bus 不可为空；否则忽略 bus
std::unique_ptr<IGatewayMqtt> createGatewayMqtt(SimulatedMqttBus* bus, const MqttConfig& config,
                                                std::string& errorDetail);

bool isMosquittoSupported();

}  // namespace transfer
