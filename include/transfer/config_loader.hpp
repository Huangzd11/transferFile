#pragma once

#include "transfer/app_config.hpp"

#include <string>

namespace transfer {

// 从 JSON 配置文件加载；成功返回 true
bool loadAppConfigFromFile(const std::string& filePath, AppConfig& out,
                           std::string& errorDetail);

// 先取默认值，再尝试加载文件（文件不存在时仅用默认值并返回 true）
bool loadAppConfig(const std::string& filePath, AppConfig& out, std::string& errorDetail);

}  // namespace transfer
