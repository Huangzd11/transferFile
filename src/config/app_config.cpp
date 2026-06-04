// 应用默认配置与 MQTT Topic 补全

#include "transfer/app_config.hpp"

namespace transfer {

// 与 config/transferFile.json 示例一致的默认值
AppConfig makeDefaultAppConfig() {
    AppConfig app;    // 应用配置
    app.transfer.timeoutSec = 180;    // 超时时间
    app.transfer.chunkSize = 4096;    // 分块大小
    app.transfer.allowedPathRoots = {"/tmp/"};    // 允许的路径根
    app.mqtt = makeDefaultMqttConfig("gw001");    // 生成默认MQTT配置
    return app;
}

// 各 Topic 为空时按 gatewayId 生成 transfer/sim/{id}/... 占位名
void fillMqttTopicDefaults(MqttConfig& mqtt) {
    MqttConfig def = makeDefaultMqttConfig(mqtt.gatewayId.empty() ? "gw001" : mqtt.gatewayId);    // 生成默认MQTT配置
    if (mqtt.topicSummon.empty()) mqtt.topicSummon = def.topicSummon;    // 设置召唤Topic
    if (mqtt.topicBrief.empty()) mqtt.topicBrief = def.topicBrief;    // 设置简报Topic
    if (mqtt.topicContent.empty()) mqtt.topicContent = def.topicContent;    // 设置内容Topic
    if (mqtt.topicContentConfirm.empty()) mqtt.topicContentConfirm = def.topicContentConfirm;    // 设置内容确认Topic
    if (mqtt.topicPushBrief.empty()) mqtt.topicPushBrief = def.topicPushBrief;    // 设置推送简报Topic
    if (mqtt.topicPushBriefConfirm.empty()) mqtt.topicPushBriefConfirm = def.topicPushBriefConfirm;    // 设置推送简报确认Topic
    if (mqtt.topicPushContent.empty()) mqtt.topicPushContent = def.topicPushContent;    // 设置推送内容Topic
    if (mqtt.topicPushContentConfirm.empty()) mqtt.topicPushContentConfirm = def.topicPushContentConfirm;    // 设置推送内容确认Topic
    if (mqtt.clientId.empty()) {        mqtt.clientId = "gateway-transfer-sim-" + mqtt.gatewayId;    // 设置客户端ID
    }
}

}  // namespace transfer
