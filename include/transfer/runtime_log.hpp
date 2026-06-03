#pragma once

#include <string>

namespace transfer {
namespace log {

enum class Level { Info, Warn, Error };

/** 初始化日志目录（默认 log/），按日期创建日志文件 */
void init(const std::string& logDir = "log");

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
