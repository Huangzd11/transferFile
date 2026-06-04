// JSON 配置文件解析（轻量实现，无第三方 JSON 库）
// 支持 transfer / log / mqtt 对象段及 allowedPathRoots 数组。

#include "transfer/config_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace transfer {
namespace {

bool extractJsonStringField(std::string_view json, std::string_view key, std::string& out) {    // 提取JSON字符串字段
    const std::string pattern = std::string("\"") + std::string(key) + "\"";    // 模式
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;    // 如果找不到，则返回false
    pos = json.find(':', pos + pattern.size());    // 找到冒号
    if (pos == std::string_view::npos) return false;    // 如果找不到，则返回false
    ++pos;    // 增加位置
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;    // 跳过空格
    if (pos >= json.size() || json[pos] != '"') return false;    // 如果找不到，则返回false
    ++pos;    // 增加位置
    out.clear();    // 清空输出
    while (pos < json.size()) {
        char c = json[pos++];    // 字符
        if (c == '\\' && pos < json.size()) {
            out += json[pos++];    // 添加字符
            continue;    // 继续
        }
        if (c == '"') return true;    // 如果找到，则返回true
        out += c;    // 添加字符
    }
    return false;
}

bool parseU64(const std::string& s, uint64_t& out) {    // 解析无符号64位整数
    if (s.empty()) return false;    // 如果为空，则返回false
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;    // 如果不在0-9之间，则返回false
        v = v * 10 + static_cast<uint64_t>(c - '0');    // 转换为整数
    }
    out = v;    // 设置输出
    return true;    // 返回true
}

bool parseU32(const std::string& s, uint32_t& out) {    // 解析无符号32位整数
    uint64_t v = 0;
    if (!parseU64(s, v) || v > 0xFFFFFFFFULL) return false;    // 如果不在0-FFFFFFFF之间，则返回false
    out = static_cast<uint32_t>(v);    // 设置输出
    return true;    // 返回true
}

bool parseBool(const std::string& s, bool& out) {    // 解析布尔值
    if (s == "true" || s == "1") {    // 如果为true或1，则设置输出为true
        out = true;    // 设置输出
        return true;    // 返回true
    }
    if (s == "false" || s == "0") {    // 如果为false或0，则设置输出为false
        out = false;    // 设置输出
        return true;    // 返回true
    }
    return false;    // 返回false
}

bool extractJsonStringArray(std::string_view json, std::string_view key,
                            std::vector<std::string>& out) {    // 提取JSON字符串数组
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;    // 如果找不到，则返回false
    pos = json.find('[', pos);
    if (pos == std::string_view::npos) return false;    // 如果找不到，则返回false
    ++pos;    // 增加位置
    out.clear();    // 清空输出
    while (pos < json.size()) {
        while (pos < json.size() && json[pos] != '"' && json[pos] != ']') ++pos;    // 跳过双引号和右括号
        if (pos >= json.size() || json[pos] == ']') break;
        ++pos;
        std::string item;    // 字符串
        while (pos < json.size()) {
            char c = json[pos++];
            if (c == '\\' && pos < json.size()) {
                item += json[pos++];    // 添加字符
                continue;
            }
            if (c == '"') break;    // 如果找到，则退出
            item += c;
        }
        if (!item.empty()) out.push_back(std::move(item));    // 添加字符串
        while (pos < json.size() && json[pos] != '"' && json[pos] != ']') ++pos;
        if (pos < json.size() && json[pos] == ']') break;    // 如果找到，则退出
    }
    return !out.empty();    // 如果不为空，则返回true
}

void applyTransferSection(std::string_view section, TransferConfig& transfer) {    // 应用传输配置
    std::string val;
    if (extractJsonStringField(section, "timeoutSec", val) && !val.empty()) {
        uint32_t n = 0;
        if (parseU32(val, n)) transfer.timeoutSec = n;    // 设置超时时间
    }
    if (extractJsonStringField(section, "chunkSize", val) && !val.empty()) {
        uint32_t n = 0;
        if (parseU32(val, n)) transfer.chunkSize = n;    // 设置分块大小
    }
    std::vector<std::string> roots;
    if (extractJsonStringArray(section, "allowedPathRoots", roots)) {
        transfer.allowedPathRoots = std::move(roots);    // 设置允许的路径根
    }
}

void applyLogSection(std::string_view section, LogConfig& log) {    // 应用日志配置
    std::string val;
    if (extractJsonStringField(section, "logDir", val) && !val.empty()) {
        log.logDir = val;    // 设置日志目录
    }
    if (extractJsonStringField(section, "maxFileSizeBytes", val) && !val.empty()) {
        uint64_t n = 0;
        if (parseU64(val, n)) log.maxFileSizeBytes = n;    // 设置最大文件大小
    }
    if (extractJsonStringField(section, "retainDays", val) && !val.empty()) {
        uint32_t n = 0;
        if (parseU32(val, n)) log.retainDays = n;    // 设置保留天数
    }
}

void applyMqttSection(std::string_view section, MqttConfig& mqtt) {    // 应用MQTT配置
    std::string val;
    if (extractJsonStringField(section, "brokerHost", val)) mqtt.brokerHost = val;
    if (extractJsonStringField(section, "brokerPort", val) && !val.empty()) {
        uint32_t p = 0;
        if (parseU32(val, p) && p <= 65535) mqtt.brokerPort = static_cast<uint16_t>(p);    // 设置端口
    }
    if (extractJsonStringField(section, "clientId", val)) mqtt.clientId = val;
    if (extractJsonStringField(section, "gatewayId", val)) mqtt.gatewayId = val;
    if (extractJsonStringField(section, "topicSummon", val)) mqtt.topicSummon = val;
    if (extractJsonStringField(section, "topicBrief", val)) mqtt.topicBrief = val;
    if (extractJsonStringField(section, "topicContent", val)) mqtt.topicContent = val;
    if (extractJsonStringField(section, "topicContentConfirm", val))
        mqtt.topicContentConfirm = val;
    if (extractJsonStringField(section, "topicPushBrief", val)) mqtt.topicPushBrief = val;
    if (extractJsonStringField(section, "topicPushBriefConfirm", val))
        mqtt.topicPushBriefConfirm = val;
    if (extractJsonStringField(section, "topicPushContent", val)) mqtt.topicPushContent = val;
    if (extractJsonStringField(section, "topicPushContentConfirm", val))
        mqtt.topicPushContentConfirm = val;
    if (extractJsonStringField(section, "useSimulatedBus", val) && !val.empty()) {
        bool b = true;
        if (parseBool(val, b)) mqtt.useSimulatedBus = b;
    }
    if (extractJsonStringField(section, "username", val)) mqtt.username = val;
    if (extractJsonStringField(section, "password", val)) mqtt.password = val;
    if (extractJsonStringField(section, "qos", val) && !val.empty()) {
        uint32_t q = 0;
        if (parseU32(val, q) && q <= 2) mqtt.qos = static_cast<int>(q);
    }
    if (extractJsonStringField(section, "keepAliveSec", val) && !val.empty()) {
        uint32_t k = 0;
        if (parseU32(val, k) && k > 0) mqtt.keepAliveSec = k;    // 设置保持活跃时间
    }
}

// 提取 "key": { ... } 子对象文本（括号匹配）
bool extractJsonObject(std::string_view json, std::string_view key, std::string& out) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;
    pos = json.find('{', pos);
    if (pos == std::string_view::npos) return false;
    size_t start = pos;
    int depth = 0;
    for (; pos < json.size(); ++pos) {
        if (json[pos] == '{')
            ++depth;
        else if (json[pos] == '}') {
            --depth;
            if (depth == 0) {
                out.assign(json.substr(start, pos - start + 1));
                return true;
            }
        }
    }
    return false;
}

}  // namespace

bool loadAppConfigFromFile(const std::string& filePath, AppConfig& out,
                           std::string& errorDetail) {
    std::ifstream in(filePath);
    if (!in) {
        errorDetail = "cannot open config file: " + filePath;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string content = ss.str();
    if (content.empty()) {
        errorDetail = "config file is empty";
        return false;
    }

    out = makeDefaultAppConfig();
    out.configFilePath = filePath;

    // 清空 Topic，避免默认值与文件中 gatewayId 不一致
    out.mqtt.topicSummon.clear();
    out.mqtt.topicBrief.clear();
    out.mqtt.topicContent.clear();
    out.mqtt.topicContentConfirm.clear();
    out.mqtt.topicPushBrief.clear();
    out.mqtt.topicPushBriefConfirm.clear();
    out.mqtt.topicPushContent.clear();
    out.mqtt.topicPushContentConfirm.clear();

    std::string transferSec, mqttSec, logSec;
    if (extractJsonObject(content, "transfer", transferSec)) {
        applyTransferSection(transferSec, out.transfer);
    }
    if (extractJsonObject(content, "log", logSec)) {
        applyLogSection(logSec, out.log);
    }
    if (extractJsonObject(content, "mqtt", mqttSec)) {
        applyMqttSection(mqttSec, out.mqtt);
    }

    fillMqttTopicDefaults(out.mqtt);
    return true;
}

bool loadAppConfig(const std::string& filePath, AppConfig& out, std::string& errorDetail) {
    out = makeDefaultAppConfig();
    std::ifstream probe(filePath);
    if (!probe.good()) {
        out.configFilePath.clear();
        fillMqttTopicDefaults(out.mqtt);
        return true;  // 无配置文件时使用默认值
    }
    return loadAppConfigFromFile(filePath, out, errorDetail);
}

}  // namespace transfer
