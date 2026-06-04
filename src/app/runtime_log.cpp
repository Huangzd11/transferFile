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
namespace {

struct LogState {
    std::mutex mu;
    std::string logDir = "log";
    uint64_t maxFileSizeBytes = 10 * 1024 * 1024;
    uint32_t retainDays = 30;
    std::string currentDate;
    std::ofstream file;
    bool initialized = false;
};

LogState& state() {
    static LogState s;
    return s;
}

const char* levelName(Level level) {
    switch (level) {
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
    }
    return "INFO";
}

const char* shortenPath(const char* path) {
    if (!path || !*path) return "?";
    const char* src = std::strstr(path, "/src/");
    if (src) return src + 1;  // 保留 src/ 前缀
    if (std::strncmp(path, "src/", 4) == 0) return path;
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

std::string formatDate(const std::tm& tm) {
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d");
    return os.str();
}

std::string formatDateTime(const std::tm& tm, int ms) {
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << '.' << std::setw(3) << std::setfill('0')
       << ms;
    return os.str();
}

std::string joinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + '/' + name;
}

bool mkdirRecursive(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    size_t i = 0;
    if (path[0] == '/') {
        cur = "/";
        i = 1;
    }
    while (i <= path.size()) {
        const size_t next = path.find('/', i);
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

bool localNow(std::tm& outTm, int& outMs) {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) %
                    1000;
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
#if defined(_WIN32)
    if (localtime_s(&outTm, &t) != 0) return false;
#else
    if (!localtime_r(&t, &outTm)) return false;
#endif
    outMs = static_cast<int>(ms.count());
    return true;
}

bool parseDatePrefix(const std::string& name, std::tm& outTm) {
    if (name.size() < 10 || name[4] != '-' || name[7] != '-') return false;
    outTm = {};
    outTm.tm_year = std::atoi(name.substr(0, 4).c_str()) - 1900;
    outTm.tm_mon = std::atoi(name.substr(5, 2).c_str()) - 1;
    outTm.tm_mday = std::atoi(name.substr(8, 2).c_str());
    if (outTm.tm_year < 0 || outTm.tm_mon < 0 || outTm.tm_mon > 11 || outTm.tm_mday < 1 ||
        outTm.tm_mday > 31) {
        return false;
    }
    return true;
}

int daysBetweenDates(const std::tm& older, const std::tm& newer) {
    auto toDays = [](const std::tm& tm) -> int {
        const int y = tm.tm_year + 1900;
        const int m = tm.tm_mon + 1;
        const int d = tm.tm_mday;
        int a = (14 - m) / 12;
        int y2 = y + 4800 - a;
        int m2 = m + 12 * a - 3;
        return d + (153 * m2 + 2) / 5 + 365 * y2 + y2 / 4 - y2 / 100 + y2 / 400 - 32045;
    };
    return toDays(newer) - toDays(older);
}

void purgeOldLogs(LogState& s) {
    if (s.retainDays == 0 || s.logDir.empty()) return;

    std::tm nowTm{};
    int ms = 0;
    if (!localNow(nowTm, ms)) return;

    DIR* dir = opendir(s.logDir.c_str());
    if (!dir) return;

    while (dirent* ent = readdir(dir)) {
        const char* name = ent->d_name;
        if (name[0] == '.') continue;
        std::tm fileTm{};
        if (!parseDatePrefix(name, fileTm)) continue;
        if (daysBetweenDates(fileTm, nowTm) <= static_cast<int>(s.retainDays)) continue;

        const std::string path = joinPath(s.logDir, name);
        std::remove(path.c_str());
    }
    closedir(dir);
}

int nextRotateSuffix(const std::string& basePath) {
    int maxN = 0;
    for (int i = 1; i < 10000; ++i) {
        struct stat st {};
        if (stat((basePath + '.' + std::to_string(i)).c_str(), &st) == 0) {
            maxN = i;
        }
    }
    return maxN + 1;
}

void rotateCurrentFile(LogState& s) {
    if (!s.file.is_open() || s.currentDate.empty()) return;

    s.file.flush();
    s.file.close();

    const std::string basePath = joinPath(s.logDir, s.currentDate + ".log");
    const int suffix = nextRotateSuffix(basePath);
    const std::string rotated = basePath + '.' + std::to_string(suffix);
    std::rename(basePath.c_str(), rotated.c_str());

    s.file.open(basePath, std::ios::out | std::ios::app);
}

bool ensureFileForDate(LogState& s, const std::string& date) {
    if (s.currentDate == date && s.file.is_open()) return true;

    if (s.file.is_open()) s.file.close();
    s.currentDate = date;

    const std::string path = joinPath(s.logDir, date + ".log");
    s.file.open(path, std::ios::out | std::ios::app);
    return s.file.is_open();
}

void rotateIfNeeded(LogState& s) {
    if (s.maxFileSizeBytes == 0 || !s.file.is_open()) return;

    s.file.flush();
    const auto pos = s.file.tellp();
    if (pos < 0 || static_cast<uint64_t>(pos) < s.maxFileSizeBytes) return;
    rotateCurrentFile(s);
}

void applyConfig(LogState& s, const LogConfig& config) {
    s.logDir = config.logDir.empty() ? "log" : config.logDir;
    s.maxFileSizeBytes = config.maxFileSizeBytes;
    s.retainDays = config.retainDays;
}

void writeLineImpl(Level level, const char* file, int line, const std::string& msg) {
    std::tm tm{};
    int ms = 0;
    if (!localNow(tm, ms)) return;

    const std::string date = formatDate(tm);
    const std::string ts = formatDateTime(tm, ms);
    const char* loc = shortenPath(file);

    std::ostringstream os;
    os << '[' << ts << "] [" << loc << ':' << line << "] [" << levelName(level) << "] " << msg;
    const std::string lineStr = os.str();

    std::ostream* console = (level == Level::Info) ? &std::cout : &std::cerr;
    *console << lineStr << std::endl;

    auto& s = state();
    if (!s.initialized) return;

    std::lock_guard<std::mutex> lock(s.mu);
    if (ensureFileForDate(s, date)) {
        s.file << lineStr << '\n';
        s.file.flush();
        rotateIfNeeded(s);
    }
}

}  // namespace

void init(const LogConfig& config) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mu);
    applyConfig(s, config);
    mkdirRecursive(s.logDir);
    purgeOldLogs(s);
    s.initialized = true;
    s.currentDate.clear();
    if (s.file.is_open()) s.file.close();
}

void init(const std::string& logDir) {
    LogConfig config;
    config.logDir = logDir;
    init(config);
}

void shutdown() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.file.is_open()) s.file.close();
    s.currentDate.clear();
    s.initialized = false;
}

void writeGateway(Level level, const char* file, int line, const std::string& msg) {
    writeLineImpl(level, file, line, msg);
}

void writePlatform(Level level, const char* file, int line, const std::string& msg) {
    writeLineImpl(level, file, line, msg);
}

}  // namespace log
}  // namespace transfer
