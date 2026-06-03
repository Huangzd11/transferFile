#include "transfer/runtime_log.hpp"

#include <chrono>
#include <cerrno>
#include <cstring>
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

bool ensureFileForDate(LogState& s, const std::string& date) {
    if (s.currentDate == date && s.file.is_open()) return true;

    if (s.file.is_open()) s.file.close();
    s.currentDate = date;

    const std::string path = joinPath(s.logDir, date + ".log");
    s.file.open(path, std::ios::out | std::ios::app);
    return s.file.is_open();
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
    }
}

}  // namespace

void init(const std::string& logDir) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mu);
    s.logDir = logDir.empty() ? "log" : logDir;
    mkdirRecursive(s.logDir);
    s.initialized = true;
    s.currentDate.clear();
    if (s.file.is_open()) s.file.close();
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
