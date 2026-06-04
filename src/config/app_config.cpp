// 应用默认配置与 MQTT Topic 补全

#include "transfer/app_config.hpp"

namespace transfer {

// 与 config/transferFile.json 示例一致的默认值
AppConfig makeDefaultAppConfig() {
    AppConfig app;
    app.transfer.timeoutSec = 180;
    app.transfer.chunkSize = 4096;
    app.transfer.allowedPathRoots = {"/tmp/"};
    app.mqtt = makeDefaultMqttConfig("gw001");
    return app;
}

// 各 Topic 为空时按 gatewayId 生成 transfer/sim/{id}/... 占位名
void fillMqttTopicDefaults(MqttConfig& mqtt) {
    MqttConfig def = makeDefaultMqttConfig(mqtt.gatewayId.empty() ? "gw001" : mqtt.gatewayId);
    if (mqtt.topicSummon.empty()) mqtt.topicSummon = def.topicSummon;
    if (mqtt.topicBrief.empty()) mqtt.topicBrief = def.topicBrief;
    if (mqtt.topicContent.empty()) mqtt.topicContent = def.topicContent;
    if (mqtt.topicContentConfirm.empty()) mqtt.topicContentConfirm = def.topicContentConfirm;
    if (mqtt.topicPushBrief.empty()) mqtt.topicPushBrief = def.topicPushBrief;
    if (mqtt.topicPushBriefConfirm.empty()) mqtt.topicPushBriefConfirm = def.topicPushBriefConfirm;
    if (mqtt.topicPushContent.empty()) mqtt.topicPushContent = def.topicPushContent;
    if (mqtt.topicPushContentConfirm.empty())
        mqtt.topicPushContentConfirm = def.topicPushContentConfirm;
    if (mqtt.clientId.empty()) {
        mqtt.clientId = "gateway-transfer-sim-" + mqtt.gatewayId;
    }
}

}  // namespace transfer
