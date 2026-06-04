// 网关 MQTT 工厂：模拟总线 vs libmosquitto

#include "transfer/gateway_mqtt.hpp"

#include "transfer/mosquitto_mqtt_adapter.hpp"
#include "transfer/mqtt_adapter.hpp"

namespace transfer {

bool isMosquittoSupported() {
#ifdef TRANSFER_WITH_MOSQUITTO
    return true;
#else
    return false;
#endif
}

std::unique_ptr<IGatewayMqtt> createGatewayMqtt(SimulatedMqttBus* bus,
                                                const MqttConfig& config,
                                                std::string& errorDetail) {
    if (config.useSimulatedBus) {
        if (!bus) {
            errorDetail = "模拟模式需要 SimulatedMqttBus 实例";
            return nullptr;
        }
        return std::make_unique<SimulatedMqttAdapter>(*bus, config);
    }

    if (!isMosquittoSupported()) {
        errorDetail =
            "useSimulatedBus=false 但未编译 libmosquitto；请安装 libmosquitto-dev 后重新 cmake "
            "或设 useSimulatedBus=true";
        return nullptr;
    }

    return std::make_unique<MosquittoMqttAdapter>(config);
}

}  // namespace transfer
