#pragma once

#include "transfer/mqtt_config.hpp"
#include "transfer/types.hpp"

#include <string>

namespace transfer {

// 应用总配置：传输参数 + MQTT 参数
struct AppConfig {
    TransferConfig transfer;
    MqttConfig mqtt;
    std::string configFilePath;  // 实际加载的路径（空表示仅用默认值）
};

// 默认配置（与 config/transferFile.json 示例一致）
AppConfig makeDefaultAppConfig();

// 若 Topic 为空则按 gatewayId / topicPrefix 自动生成
void fillMqttTopicDefaults(MqttConfig& mqtt);

}  // namespace transfer
