// 运行日志：控制台 + 按日文件 YYYY-MM-DD.log
// 超 maxFileSizeBytes 时轮转；超 retainDays 的日期文件删除。

#include "transfer/runtime_log.hpp"

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace transfer {
namespace log {
namespace {    // 定义日志状态

struct LogState {       
    std::mutex mu;    // 互斥锁 
    std::string logDir = "log";    // 日志目录
    uint64_t maxFileSizeBytes = 10 * 1024 * 1024;    // 最大文件大小
    uint32_t retainDays = 30;    // 保留天数
    std::string currentDate;    // 当前日期
    std::ofstream file;    // 文件流
    bool initialized = false;    // 初始化状态
};

LogState& state() {
    static LogState s;    // 获取日志状态
    return s;
}

const char* levelName(Level level) {    // 获取日志级别名称
    switch (level) {
        case Level::Info:
            return "INFO";    // 信息
        case Level::Warn:
            return "WARN";    // 警告
        case Level::Error:
            return "ERROR";    // 错误
    }
    return "INFO";    // 默认信息
}

const char* shortenPath(const char* path) {
    if (!path || !*path) return "?";    // 短路径   
    const char* src = std::strstr(path, "/src/");
    if (src) return src + 1;  // 保留 src/ 前缀
    if (std::strncmp(path, "src/", 4) == 0) return path;    // 保留 src/ 前缀
    const char* slash = std::strrchr(path, '/');    // 获取最后一个斜杠
    return slash ? slash + 1 : path;    // 返回短路径
}

std::string formatDate(const std::tm& tm) {     // 格式化日期
    std::ostringstream os;    // 字符串流       
    os << std::put_time(&tm, "%Y-%m-%d");    // 格式化日期
    return os.str();
}

std::string formatDateTime(const std::tm& tm, int ms) {    // 格式化日期时间
    std::ostringstream os;    // 字符串流       
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
       << ms;    // 格式化日期时间
    return os.str();
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;    // 如果目录为空，则返回文件名
    if (dir.back() == '/') return dir + name;    // 如果目录末尾为斜杠，则返回拼接路径
    return dir + '/' + name;    // 返回拼接路径
}

bool mkdirRecursive(const std::string& path) {   // 递归创建目录
    if (path.empty()) return false;    // 如果路径为空，则返回false 
    std::string cur;    // 当前路径
    size_t i = 0;    // 索引
    if (path[0] == '/') {    // 如果路径开头为斜杠
        cur = "/";    // 当前路径为斜杠
        i = 1;    // 索引为1
    }
    while (i <= path.size()) {    // 如果索引小于等于路径长度
        const size_t next = path.find('/', i);    // 查找下一个斜杠
        const std::string part = path.substr(i, next - i);
        if (!part.empty()) {
            if (!cur.empty() && cur.back() != '/') cur += '/';
            cur += part;
            struct stat st {};
            if (stat(cur.c_str(), &st) != 0) {
                if (mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
            }
        }
        if (next == std::string::npos) break;
        i = next + 1;
    }
    return true;
}

bool localNow(std::tm& outTm, int& outMs) {    // 获取本地时间
    const auto now = std::chrono::system_clock::now();    // 获取当前时间
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
                    1000;    // 获取毫秒
    const std::time_t t = std::chrono::system_clock::to_time_t(now);    // 转换为时间戳
#if defined(_WIN32)
    if (localtime_s(&outTm, &t) != 0) return false;    // 转换为本地时间
#else
    if (!localtime_r(&t, &outTm)) return false;    // 转换为本地时间
#endif
    outMs = static_cast<int>(ms.count());    // 设置毫秒
    return true;
}

bool parseDatePrefix(const std::string& name, std::tm& outTm) {    // 解析日期前缀
    if (name.size() < 10 || name[4] != '-' || name[7] != '-') return false;
    outTm = {};    // 设置时间
    outTm.tm_year = std::atoi(name.substr(0, 4).c_str()) - 1900;
    outTm.tm_mon = std::atoi(name.substr(5, 2).c_str()) - 1;    // 设置月份
    outTm.tm_mday = std::atoi(name.substr(8, 2).c_str());    // 设置日期
    if (outTm.tm_year < 0 || outTm.tm_mon < 0 || outTm.tm_mon > 11 || outTm.tm_mday < 1 ||
        outTm.tm_mday > 31) {
        return false;    // 返回false
    }
    return true;
}

int daysBetweenDates(const std::tm& older, const std::tm& newer) {    // 计算日期差
    auto toDays = [](const std::tm& tm) -> int {
        const int y = tm.tm_year + 1900;
        const int m = tm.tm_mon + 1;
        const int d = tm.tm_mday;
        int a = (14 - m) / 12;
        int y2 = y + 4800 - a;
        int m2 = m + 12 * a - 3;
        return d + (153 * m2 + 2) / 5 + 365 * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045;
    };
    return toDays(newer) - toDays(older);    // 返回日期差
}

void purgeOldLogs(LogState& s) {    // 清理旧日志       
    if (s.retainDays == 0 || s.logDir.empty()) return;    // 如果保留天数为0或日志目录为空，则返回

    std::tm nowTm{};
    int ms = 0;
    if (!localNow(nowTm, ms)) return;    // 获取本地时间

    DIR* dir = opendir(s.logDir.c_str());
    if (!dir) return;    // 打开日志目录

    while (dirent* ent = readdir(dir)) {
        const char* name = ent->d_name;
        if (name[0] == '.') continue;    // 如果文件名以.开头，则跳过
        std::tm fileTm{};
        if (!parseDatePrefix(name, fileTm)) continue;    // 解析日期前缀
        if (daysBetweenDates(fileTm, nowTm) <= static_cast<int>(s.retainDays)) continue;

        const std::string path = joinPath(s.logDir, name);    // 拼接路径
        std::remove(path.c_str());    // 删除文件
    }
    closedir(dir);    // 关闭目录
}

int nextRotateSuffix(const std::string& basePath) {    // 获取下一个轮转后缀
    int maxN = 0;
    for (int i = 1; i < 10000; ++i) {
        struct stat st {};
        if (stat((basePath + '.' + std::to_string(i)).c_str(), &st) == 0) {
            maxN = i;    // 设置最大后缀
        }
    }
    return maxN + 1;    // 返回下一个轮转后缀
}

void rotateCurrentFile(LogState& s) {    // 轮转当前文件
    if (!s.file.is_open() || s.currentDate.empty()) return;

    s.file.flush();    // 刷新文件
    s.file.close();    // 关闭文件  

    const std::string basePath = joinPath(s.logDir, s.currentDate + ".log");    // 拼接路径
    const int suffix = nextRotateSuffix(basePath);    // 获取下一个轮转后缀
    const std::string rotated = basePath + '.' + std::to_string(suffix);
    std::rename(basePath.c_str(), rotated.c_str());    // 重命名文件

    s.file.open(basePath, std::ios::out | std::ios::app);
}

bool ensureFileForDate(LogState& s, const std::string& date) {    // 确保文件存在
    if (s.currentDate == date && s.file.is_open()) return true;    // 如果当前日期和文件存在，则返回true

    if (s.file.is_open()) s.file.close();
    s.currentDate = date;    // 设置当前日期

    const std::string path = joinPath(s.logDir, date + ".log");    // 拼接路径
    s.file.open(path, std::ios::out | std::ios::app);
    return s.file.is_open();    // 返回文件是否打开
}

void rotateIfNeeded(LogState& s) {    // 如果需要轮转，则轮转
    if (s.maxFileSizeBytes == 0 || !s.file.is_open()) return;

    s.file.flush();    // 刷新文件
    const auto pos = s.file.tellp();
    if (pos < 0 || static_cast<uint64_t>(pos) < s.maxFileSizeBytes) return;
    rotateCurrentFile(s);    // 轮转当前文件
}

void applyConfig(LogState& s, const LogConfig& config) {    // 应用配置     
    s.logDir = config.logDir.empty() ? "log" : config.logDir;    // 设置日志目录
    s.maxFileSizeBytes = config.maxFileSizeBytes;    // 设置最大文件大小
    s.retainDays = config.retainDays;    // 设置保留天数            
}

void writeLineImpl(Level level, const char* file, int line, const std::string& msg) {    // 写入行
    std::tm tm{};
    int ms = 0;
    if (!localNow(tm, ms)) return;    // 获取本地时间

    const std::string date = formatDate(tm);
    const std::string ts = formatDateTime(tm, ms);    // 格式化日期时间
    const char* loc = shortenPath(file);    // 短路径

    std::ostringstream os;
    os << '[' << ts << "] [" << loc << ':' << line << "] [" << levelName(level) << "] " << msg; 
    const std::string lineStr = os.str();    // 行字符串

    std::ostream* console = (level == Level::Info) ? &std::cout : &std::cerr;
    *console << lineStr << std::endl;    // 写入控制台

    auto& s = state();
    if (!s.initialized) return;    // 如果未初始化，则返回

    std::lock_guard<std::mutex> lock(s.mu);
    if (ensureFileForDate(s, date)) {    // 确保文件存在
        s.file << lineStr << '\n';    // 写入文件
        s.file.flush();    // 刷新文件
        rotateIfNeeded(s);    // 如果需要轮转，则轮转
    }
}

}  // namespace

void init(const LogConfig& config) {    // 初始化
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mu);
    applyConfig(s, config);    // 应用配置
    mkdirRecursive(s.logDir);    // 创建目录
    purgeOldLogs(s);    // 清理旧日志
    s.initialized = true;
    s.currentDate.clear();    // 清空当前日期   
    if (s.file.is_open()) s.file.close();    // 关闭文件
}

void init(const std::string& logDir) {    // 初始化
    LogConfig config;
    config.logDir = logDir;    // 设置日志目录
    init(config);
}

void shutdown() {    // 关闭日志
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.file.is_open()) s.file.close();    // 关闭文件
    s.currentDate.clear();    // 清空当前日期
    s.initialized = false;
}

void writeGateway(Level level, const char* file, int line, const std::string& msg) {    // 写入网关日志
    writeLineImpl(level, file, line, msg);    // 写入行
}

void writePlatform(Level level, const char* file, int line, const std::string& msg) {    // 写入平台日志
    writeLineImpl(level, file, line, msg);    // 写入行
}

}  // namespace log
}  // namespace transfer
