#include "transfer/mqtt_config.hpp"

namespace transfer {

MqttConfig makeDefaultMqttConfig(const std::string& gatewayId) {
    MqttConfig cfg;
    cfg.gatewayId = gatewayId;
    cfg.clientId = "gateway-transfer-sim-" + gatewayId;
    const std::string prefix = "transfer/sim/" + gatewayId;
    cfg.topicSummon = prefix + "/platform/summon";
    cfg.topicBrief = prefix + "/gateway/brief";
    cfg.topicContent = prefix + "/gateway/content";
    return cfg;
}

}  // namespace transfer
