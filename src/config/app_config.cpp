#include "transfer/app_config.hpp"

namespace transfer {

AppConfig makeDefaultAppConfig() {
    AppConfig app;
    app.transfer.timeoutSec = 180;
    app.transfer.chunkSize = 4096;
    app.transfer.allowedPathRoots = {"/tmp/"};
    app.mqtt = makeDefaultMqttConfig("gw001");
    return app;
}

void fillMqttTopicDefaults(MqttConfig& mqtt) {
    if (!mqtt.topicSummon.empty() && !mqtt.topicBrief.empty() &&
        !mqtt.topicContent.empty()) {
        return;
    }
    MqttConfig def = makeDefaultMqttConfig(mqtt.gatewayId.empty() ? "gw001" : mqtt.gatewayId);
    if (mqtt.topicSummon.empty()) mqtt.topicSummon = def.topicSummon;
    if (mqtt.topicBrief.empty()) mqtt.topicBrief = def.topicBrief;
    if (mqtt.topicContent.empty()) mqtt.topicContent = def.topicContent;
    if (mqtt.clientId.empty()) {
        mqtt.clientId = "gateway-transfer-sim-" + mqtt.gatewayId;
    }
}

}  // namespace transfer
