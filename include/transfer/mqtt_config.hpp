#pragma once

#include <cstdint>
#include <string>

namespace transfer {

// MQTT 连接与 Topic 占位配置（模拟/联调用，平台确认后修改）
struct MqttConfig {
    std::string brokerHost = "127.0.0.1";
    uint16_t brokerPort = 1883;
    std::string clientId = "gateway-transfer-sim-001";
    std::string gatewayId = "gw001";

    // Topic 模板：transfer/{gatewayId}/...
    std::string topicSummon;
    std::string topicBrief;
    std::string topicContent;
    // V0.0.4 平台对召唤上传内容段的确认（平台 → 网关）
    std::string topicContentConfirm;

    // V0.0.3 平台推送文件至网关
    std::string topicPushBrief;
    std::string topicPushBriefConfirm;
    std::string topicPushContent;
    std::string topicPushContentConfirm;

    // 是否使用内存模拟总线（true=不连真实 Broker）
    bool useSimulatedBus = true;

    std::string username;
    std::string password;
    int qos = 1;
    uint32_t keepAliveSec = 60;
};

// 根据 gatewayId 填充 Topic 占位名
MqttConfig makeDefaultMqttConfig(const std::string& gatewayId = "gw001");

}  // namespace transfer
