#pragma once

#include "transfer/app_config.hpp"

#include <string>

namespace transfer {
namespace log {

enum class Level { Info, Warn, Error };

/** 按 LogConfig 初始化：目录、按日期命名、可选大小轮转与过期清理 */
void init(const LogConfig& config);

/** 仅指定目录（轮转/保留使用 LogConfig 默认值），单元测试常用 */
void init(const std::string& logDir);

/** 关闭日志文件 */
void shutdown();

void writeGateway(Level level, const char* file, int line, const std::string& msg);
void writePlatform(Level level, const char* file, int line, const std::string& msg);

inline void gatewayInfo(const std::string& msg,
                        const char* file = __builtin_FILE(),
                        int line = __builtin_LINE()) {
    writeGateway(Level::Info, file, line, msg);
}

inline void gatewayWarn(const std::string& msg,
                        const char* file = __builtin_FILE(),
                        int line = __builtin_LINE()) {
    writeGateway(Level::Warn, file, line, msg);
}

inline void gatewayError(const std::string& msg,
                         const char* file = __builtin_FILE(),
                         int line = __builtin_LINE()) {
    writeGateway(Level::Error, file, line, msg);
}

inline void platformInfo(const std::string& msg,
                         const char* file = __builtin_FILE(),
                         int line = __builtin_LINE()) {
    writePlatform(Level::Info, file, line, msg);
}

}  // namespace log
}  // namespace transfer
