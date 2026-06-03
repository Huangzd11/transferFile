#include "transfer/config_loader.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace transfer {
namespace {

bool extractJsonStringField(std::string_view json, std::string_view key, std::string& out) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            out += json[pos++];
            continue;
        }
        if (c == '"') return true;
        out += c;
    }
    return false;
}

bool parseU64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    out = v;
    return true;
}

bool parseU32(const std::string& s, uint32_t& out) {
    uint64_t v = 0;
    if (!parseU64(s, v) || v > 0xFFFFFFFFULL) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseBool(const std::string& s, bool& out) {
    if (s == "true" || s == "1") {
        out = true;
        return true;
    }
    if (s == "false" || s == "0") {
        out = false;
        return true;
    }
    return false;
}

bool extractJsonStringArray(std::string_view json, std::string_view key,
                            std::vector<std::string>& out) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;
    pos = json.find('[', pos);
    if (pos == std::string_view::npos) return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        while (pos < json.size() && json[pos] != '"' && json[pos] != ']') ++pos;
        if (pos >= json.size() || json[pos] == ']') break;
        ++pos;
        std::string item;
        while (pos < json.size()) {
            char c = json[pos++];
            if (c == '\\' && pos < json.size()) {
                item += json[pos++];
                continue;
            }
            if (c == '"') break;
            item += c;
        }
        if (!item.empty()) out.push_back(std::move(item));
        while (pos < json.size() && json[pos] != '"' && json[pos] != ']') ++pos;
        if (pos < json.size() && json[pos] == ']') break;
    }
    return !out.empty();
}

void applyTransferSection(std::string_view section, TransferConfig& transfer) {
    std::string val;
    if (extractJsonStringField(section, "timeoutSec", val) && !val.empty()) {
        uint32_t n = 0;
        if (parseU32(val, n)) transfer.timeoutSec = n;
    }
    if (extractJsonStringField(section, "chunkSize", val) && !val.empty()) {
        uint32_t n = 0;
        if (parseU32(val, n)) transfer.chunkSize = n;
    }
    std::vector<std::string> roots;
    if (extractJsonStringArray(section, "allowedPathRoots", roots)) {
        transfer.allowedPathRoots = std::move(roots);
    }
}

void applyMqttSection(std::string_view section, MqttConfig& mqtt) {
    std::string val;
    if (extractJsonStringField(section, "brokerHost", val)) mqtt.brokerHost = val;
    if (extractJsonStringField(section, "brokerPort", val) && !val.empty()) {
        uint32_t p = 0;
        if (parseU32(val, p) && p <= 65535) mqtt.brokerPort = static_cast<uint16_t>(p);
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
        if (parseU32(val, k) && k > 0) mqtt.keepAliveSec = k;
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

    std::string transferSec, mqttSec;
    if (extractJsonObject(content, "transfer", transferSec)) {
        applyTransferSection(transferSec, out.transfer);
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
