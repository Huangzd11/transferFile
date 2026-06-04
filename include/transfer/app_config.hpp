// 应用配置模型
// 聚合传输参数、MQTT 参数、日志参数及默认值/Topic 补全逻辑。

#pragma once

#include "transfer/mqtt_config.hpp"
#include "transfer/types.hpp"

#include <string>

namespace transfer {

// 运行日志配置（V0.0.5：logDir 可配置，按大小轮转与保留天数）
struct LogConfig {
    std::string logDir = "log";
    uint64_t maxFileSizeBytes = 10 * 1024 * 1024;  // 10MB；0 表示不按大小轮转
    uint32_t retainDays = 30;                      // 0 表示不自动清理
};

// 应用总配置：传输参数 + MQTT 参数 + 日志参数
struct AppConfig {
    TransferConfig transfer;
    MqttConfig mqtt;
    LogConfig log;
    std::string configFilePath;  // 实际加载的路径（空表示仅用默认值）
};

// 默认配置（与 config/transferFile.json 示例一致）
AppConfig makeDefaultAppConfig();

// 若 Topic 为空则按 gatewayId / topicPrefix 自动生成
void fillMqttTopicDefaults(MqttConfig& mqtt);

}  // namespace transfer
