// MQTT 默认 Topic 命名规则

#include "transfer/mqtt_config.hpp"

namespace transfer {

// 生成 transfer/sim/{gatewayId}/platform|gateway/... 系列 Topic
MqttConfig makeDefaultMqttConfig(const std::string& gatewayId) {
    MqttConfig cfg;
    cfg.gatewayId = gatewayId;
    cfg.clientId = "gateway-transfer-sim-" + gatewayId;
    const std::string prefix = "transfer/sim/" + gatewayId;
    cfg.topicSummon = prefix + "/platform/summon";
    cfg.topicBrief = prefix + "/gateway/brief";
    cfg.topicContent = prefix + "/gateway/content";
    cfg.topicContentConfirm = prefix + "/platform/content_confirm";
    cfg.topicPushBrief = prefix + "/platform/push/brief";
    cfg.topicPushBriefConfirm = prefix + "/gateway/push/brief_confirm";
    cfg.topicPushContent = prefix + "/platform/push/content";
    cfg.topicPushContentConfirm = prefix + "/gateway/push/content_confirm";
    return cfg;
}

}  // namespace transfer
