// MQTT 默认 Topic 命名规则

#include "transfer/mqtt_config.hpp"

namespace transfer {

// 生成 transfer/sim/{gatewayId}/platform|gateway/... 系列 Topic
MqttConfig makeDefaultMqttConfig(const std::string& gatewayId) {
    MqttConfig cfg;
    cfg.gatewayId = gatewayId;
    cfg.clientId = "gateway-transfer-sim-" + gatewayId;    // 生成客户端ID
    const std::string prefix = "transfer/sim/" + gatewayId;    // 生成前缀
    cfg.topicSummon = prefix + "/platform/summon";
    cfg.topicBrief = prefix + "/gateway/brief";    // 生成简报Topic
    cfg.topicContent = prefix + "/gateway/content";    // 生成内容Topic
    cfg.topicContentConfirm = prefix + "/platform/content_confirm";    // 生成内容确认Topic
    cfg.topicPushBrief = prefix + "/platform/push/brief";
    cfg.topicPushBriefConfirm = prefix + "/gateway/push/brief_confirm";    // 生成简报确认Topic
    cfg.topicPushContent = prefix + "/platform/push/content";
    cfg.topicPushContentConfirm = prefix + "/gateway/push/content_confirm";    // 生成内容确认Topic
    return cfg;    // 返回配置
}

}  // namespace transfer
