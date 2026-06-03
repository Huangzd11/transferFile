/**
 * 平台侧模拟程序：通过 MQTT 向网关下发召唤，并接收简报/内容应答。
 * 用于本机与 transferFile 网关进程联调（需 Broker + libmosquitto）。
 */
#include "transfer/base64.hpp"
#include "transfer/config_loader.hpp"
#include "transfer/mqtt_config.hpp"
#include "transfer/runtime_log.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef TRANSFER_WITH_MOSQUITTO
#include <mosquitto.h>
#endif

namespace {

const char* kDefaultConfig = "config/transferFile.platform.json";
std::atomic<bool> g_running{true};

void onSignal(int) { g_running = false; }

// 从协议 JSON 中提取 Data 下某 string 字段（仅联调摘要用）
bool extractJsonStringField(std::string_view json, std::string_view key, std::string& out) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
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

void summarizeGatewayBrief(const std::string& payload) {
    std::string cmdId, status, fileSize, fileCrc, modifyTime, errorCode, note;
    extractJsonStringField(payload, "CmdId", cmdId);
    extractJsonStringField(payload, "Status", status);
    extractJsonStringField(payload, "FileSize", fileSize);
    extractJsonStringField(payload, "FileCrc", fileCrc);
    extractJsonStringField(payload, "ModifyTime", modifyTime);
    extractJsonStringField(payload, "ErrorCode", errorCode);
    extractJsonStringField(payload, "Note", note);
    std::ostringstream os;
    os << "简报摘要: CmdId=" << cmdId
       << (status == "0" ? " 成功" : status == "1" ? " 失败" : " Status=" + status);
    if (!fileSize.empty()) os << " FileSize=" << fileSize;
    if (!fileCrc.empty()) os << " FileCrc=" << fileCrc;
    if (!modifyTime.empty()) os << " ModifyTime=" << modifyTime;
    if (!errorCode.empty()) os << " ErrorCode=" << errorCode;
    if (!note.empty()) os << " Note=" << note;
    transfer::log::platformInfo(os.str());
}

void summarizeGatewayContent(const std::string& payload) {
    std::string cmdId, segNo, cont, contFlag;
    extractJsonStringField(payload, "CmdId", cmdId);
    extractJsonStringField(payload, "FileSegNo", segNo);
    extractJsonStringField(payload, "Content", cont);
    extractJsonStringField(payload, "Continue", contFlag);
    std::ostringstream os;
    os << "内容段摘要: CmdId=" << cmdId << " SegNo=" << segNo
       << " Base64长度=" << cont.size()
       << (contFlag == "1" ? " 还有后续" : contFlag == "0" ? " 最后一段" : "");
    transfer::log::platformInfo(os.str());
}

void printUsage(const char* prog) {
    std::cout
        << "用法: " << prog << " [选项]\n"
        << "  -c, --config <路径>   配置文件 (默认: " << kDefaultConfig << ")\n"
        << "  --demo                仅当网关也在本机：在开发机创建测试文件并发召唤\n"
        << "  --gateway-file <路径> 目标机联调：发召唤，<路径>为目标机本地文件(须已存在)\n"
        << "  --start-byte <N>      与 --gateway-file 合用，1-based 断点续传起点(默认 1)\n"
        << "  --publish <json>      发布一条召唤 JSON 到召唤 Topic\n"
        << "  -f, --file <路径>     从文件读取召唤 JSON 并发布\n"
        << "  --listen-only         仅订阅简报/内容，不主动发布\n"
        << "  -h, --help            帮助\n"
        << "\n"
        << "典型拓扑: 开发机跑 platform_sim；目标机跑 transferFile；共用同一 Broker/gatewayId\n"
        << "  目标机: ./transferFile -c config/transferFile.gateway.target.json\n"
        << "  开发机: ./platform_sim -c config/transferFile.platform.json --gateway-file /tmp/xxx\n";
}

bool parseArgs(int argc, char* argv[], std::string& configPath, bool& demo,
               bool& listenOnly, std::string& publishJson, std::string& publishFile,
               std::string& gatewayFilePath, uint64_t& startByte, bool& showHelp) {
    configPath = kDefaultConfig;
    startByte = 1;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--demo") {
            demo = true;
        } else if (arg == "--listen-only") {
            listenOnly = true;
        } else if (arg == "-h" || arg == "--help") {
            showHelp = true;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else if ((arg == "--publish") && i + 1 < argc) {
            publishJson = argv[++i];
        } else if ((arg == "-f" || arg == "--file") && i + 1 < argc) {
            publishFile = argv[++i];
        } else if (arg == "--gateway-file" && i + 1 < argc) {
            gatewayFilePath = argv[++i];
        } else if (arg == "--start-byte" && i + 1 < argc) {
            startByte = std::stoull(argv[++i]);
        } else {
            std::cerr << "未知参数: " << arg << "\n";
            return false;
        }
    }
    if (demo && !gatewayFilePath.empty()) {
        std::cerr << "--demo 与 --gateway-file 不能同时使用\n";
        return false;
    }
    return true;
}

#ifdef TRANSFER_WITH_MOSQUITTO

struct PlatformMqttClient {
    transfer::MqttConfig config;
    mosquitto* mosq = nullptr;
    bool connected = false;
    bool demoMode = false;           // --demo：收齐后自动退出
    bool transferDone = false;
    bool transferFailed = false;
    std::string summonedPath;        // 本次召唤要求网关读取的路径
    std::string saveReceivedPath;    // 平台侧保存网关上传内容的文件
    std::vector<uint8_t> receivedFile;

    void onBriefMessage(const std::string& payload) {
        summarizeGatewayBrief(payload);
        std::string status, errorCode, note;
        extractJsonStringField(payload, "Status", status);
        extractJsonStringField(payload, "ErrorCode", errorCode);
        extractJsonStringField(payload, "Note", note);
        if (status == "1") {
            transferFailed = true;
            std::ostringstream os;
            os << "网关简报失败，未收到文件内容";
            if (!errorCode.empty()) os << " ErrorCode=" << errorCode;
            if (!note.empty()) os << " (" << note << ")";
            transfer::log::platformInfo(os.str());
            if (demoMode) g_running = false;
            return;
        }
        if (status == "0") {
            transfer::log::platformInfo("网关接受召唤，即将下发文件内容...");
        }
    }

    void onContentMessage(const std::string& payload) {
        summarizeGatewayContent(payload);
        std::string b64, contFlag;
        if (!extractJsonStringField(payload, "Content", b64)) return;
        std::vector<uint8_t> chunk;
        if (!transfer::base64Decode(b64, chunk)) {
            transfer::log::platformInfo("内容段 Base64 解码失败");
            return;
        }
        receivedFile.insert(receivedFile.end(), chunk.begin(), chunk.end());

        extractJsonStringField(payload, "Continue", contFlag);
        if (contFlag != "0") return;

        transferDone = true;
        if (!saveReceivedPath.empty()) {
            std::ofstream out(saveReceivedPath, std::ios::binary);
            out.write(reinterpret_cast<const char*>(receivedFile.data()),
                      static_cast<std::streamsize>(receivedFile.size()));
        }
        std::ostringstream os;
        os << "文件接收完成: " << receivedFile.size() << " 字节";
        if (!saveReceivedPath.empty()) os << " 已保存 -> " << saveReceivedPath;
        if (!summonedPath.empty()) os << " (网关读取: " << summonedPath << ")";
        transfer::log::platformInfo(os.str());
        if (demoMode) g_running = false;
    }

    static void onConnect(struct mosquitto* m, void* ud, int rc) {
        auto* self = static_cast<PlatformMqttClient*>(ud);
        if (rc != 0) {
            std::cerr << "平台 MQTT 连接失败 rc=" << rc << "\n";
            return;
        }
        self->connected = true;
        mosquitto_subscribe(m, nullptr, self->config.topicBrief.c_str(), self->config.qos);
        mosquitto_subscribe(m, nullptr, self->config.topicContent.c_str(), self->config.qos);
        std::cout << "已订阅: " << self->config.topicBrief << "\n"
                  << "         " << self->config.topicContent << "\n";
    }

    static void onMessage(struct mosquitto*, void* ud, const struct mosquitto_message* msg) {
        auto* self = static_cast<PlatformMqttClient*>(ud);
        if (!msg || !msg->topic || !msg->payload) return;
        std::string topic(msg->topic);
        std::string payload(static_cast<const char*>(msg->payload), msg->payloadlen);
        std::cout << "\n>>> 收到 [" << topic << "]\n" << payload << "\n";

        if (topic == self->config.topicBrief) {
            self->onBriefMessage(payload);
        } else if (topic == self->config.topicContent) {
            self->onContentMessage(payload);
        }
    }

    bool start(std::string& err) {
        if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
            err = "mosquitto_lib_init";
            return false;
        }
        std::string clientId = "platform-sim-" + config.gatewayId;
        mosq = mosquitto_new(clientId.c_str(), true, this);
        if (!mosq) {
            err = "mosquitto_new";
            return false;
        }
        mosquitto_connect_callback_set(mosq, onConnect);
        mosquitto_message_callback_set(mosq, onMessage);
        if (!config.username.empty()) {
            mosquitto_username_pw_set(mosq, config.username.c_str(),
                                      config.password.empty() ? nullptr
                                                             : config.password.c_str());
        }
        int rc = mosquitto_connect(mosq, config.brokerHost.c_str(), config.brokerPort,
                                   static_cast<int>(config.keepAliveSec));
        if (rc != MOSQ_ERR_SUCCESS) {
            err = mosquitto_strerror(rc);
            return false;
        }
        return true;
    }

    void stop() {
        if (mosq) {
            mosquitto_disconnect(mosq);
            mosquitto_destroy(mosq);
            mosq = nullptr;
        }
        mosquitto_lib_cleanup();
        connected = false;
    }

    int loop(int ms) { return mosq ? mosquitto_loop(mosq, ms, 1) : -1; }

    bool publishSummon(const std::string& json, std::string& err) {
        if (!mosq) {
            err = "not connected";
            return false;
        }
        int rc = mosquitto_publish(mosq, nullptr, config.topicSummon.c_str(),
                                   static_cast<int>(json.size()), json.data(),
                                   config.qos, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            err = mosquitto_strerror(rc);
            return false;
        }
        transfer::log::platformInfo("<<< 已发布召唤 -> " + config.topicSummon);
        std::cout << json << "\n";
        return true;
    }
};

#endif  // TRANSFER_WITH_MOSQUITTO

}  // namespace

int main(int argc, char* argv[]) {
    transfer::log::init("log");
    struct LogGuard {
        ~LogGuard() { transfer::log::shutdown(); }
    } logGuard;

    std::string configPath;
    bool demo = false;
    bool listenOnly = false;
    bool showHelp = false;
    std::string publishJson;
    std::string publishFile;
    std::string gatewayFilePath;
    uint64_t startByte = 1;
    if (!parseArgs(argc, argv, configPath, demo, listenOnly, publishJson, publishFile,
                   gatewayFilePath, startByte, showHelp)) {
        printUsage(argv[0]);
        return 1;
    }
    if (showHelp) {
        printUsage(argv[0]);
        return 0;
    }

#ifndef TRANSFER_WITH_MOSQUITTO
    std::cerr << "platform_sim 需要 libmosquitto，请安装后重新编译:\n"
              << "  sudo apt install libmosquitto-dev\n"
              << "  ./scripts/build-native.sh\n";
    return 1;
#else

    transfer::AppConfig app;
    std::string loadErr;
    if (!transfer::loadAppConfig(configPath, app, loadErr)) {
        std::cerr << "加载配置失败: " << loadErr << "\n";
        return 1;
    }

    std::cout << "=== platform_sim (平台模拟) ===\n"
              << "Broker: " << app.mqtt.brokerHost << ":" << app.mqtt.brokerPort << "\n"
              << "发布召唤: " << app.mqtt.topicSummon << "\n";

    PlatformMqttClient client;
    client.config = app.mqtt;
    if (client.config.clientId.empty()) {
        client.config.clientId = "platform-sim-" + client.config.gatewayId;
    }

    std::string err;
    if (!client.start(err)) {
        std::cerr << "MQTT 启动失败: " << err << "\n";
        return 1;
    }

    std::signal(SIGINT, onSignal);

    // 等待连接并完成订阅
    for (int i = 0; i < 50 && g_running && !client.connected; ++i) {
        client.loop(100);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!client.connected) {
        std::cerr << "连接 Broker 超时，请确认 mosquitto 已启动且地址正确\n";
        client.stop();
        return 1;
    }

    auto doPublish = [&](const std::string& json) {
        std::string pubErr;
        if (!client.publishSummon(json, pubErr)) {
            std::cerr << "发布失败: " << pubErr << "\n";
        }
    };

    if (!publishFile.empty()) {
        std::ifstream in(publishFile);
        std::string json((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
        doPublish(json);
    } else if (!publishJson.empty()) {
        doPublish(publishJson);
    } else if (demo) {
        client.demoMode = true;
        const std::string testPath = "/tmp/platform_test_file.bin";
        const std::string receivedPath = "/tmp/platform_received_file.bin";
        client.summonedPath = testPath;
        client.saveReceivedPath = receivedPath;
        client.receivedFile.clear();

        const std::string fileBody =
            "Hello MQTT from platform_sim " +
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        {
            std::ofstream out(testPath, std::ios::binary);
            out << fileBody;
        }
        transfer::log::platformInfo("【本机网关冒烟】已在开发机创建: " + testPath);
        transfer::log::platformInfo("若网关在目标机，请改用 --gateway-file <目标机路径>");

        const std::string summon = R"({"Data":{"CmdId":"8001","FullPathFileName":")" +
                                   testPath + R"(","StartByte":"1"}})";
        doPublish(summon);
    } else if (!gatewayFilePath.empty()) {
        client.demoMode = true;
        client.summonedPath = gatewayFilePath;
        client.saveReceivedPath = "/tmp/platform_received_file.bin";
        client.receivedFile.clear();
        transfer::log::platformInfo("【目标机网关】召唤路径(须在目标机已存在): " + gatewayFilePath);
        if (startByte > 1) {
            transfer::log::platformInfo("断点续传 StartByte=" + std::to_string(startByte));
        }
        transfer::log::platformInfo("请确认目标机 transferFile 已启动且 gatewayId/Broker 与配置一致");

        const std::string summon = R"({"Data":{"CmdId":"8001","FullPathFileName":")" +
                                   gatewayFilePath + R"(","StartByte":")" +
                                   std::to_string(startByte) + R"("}})";
        doPublish(summon);
    } else if (!listenOnly) {
        std::cout << "未指定 --demo/--publish/--file，进入仅监听模式（Ctrl+C 退出）\n";
    }

    transfer::log::platformInfo("监听网关应答中...（收到简报/内容时会打印解析摘要）");
    while (g_running) {
        int rc = client.loop(100);
        if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_NO_CONN) {
            std::cerr << "loop: " << mosquitto_strerror(rc) << "\n";
        }
    }

    client.stop();
    if (demo || !gatewayFilePath.empty()) {
        if (client.transferDone) {
            transfer::log::platformInfo("联调成功：平台发召唤，目标机网关已回传文件");
            return 0;
        }
        if (client.transferFailed) {
            std::cerr << "联调失败：网关简报返回失败（检查目标机文件路径与 allowedPathRoots）\n";
            return 2;
        }
        std::cerr << "联调未完成：未收齐文件内容（请确认目标机 transferFile 已连上 Broker）\n";
        return 3;
    }
    std::cout << "已退出\n";
    return 0;

#endif
}
