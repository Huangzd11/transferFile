// transferFile 网关主程序
// 加载配置、初始化 MQTT/编排器，主循环 loop+看门狗 tick；--simulate 走内存总线演示。

#include "transfer/config_loader.hpp"
#include "transfer/crc32.hpp"
#include "transfer/file_store.hpp"
#include "transfer/gateway_mqtt.hpp"
#include "transfer/mqtt_adapter.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/session_store.hpp"
#include "transfer/simulated_mqtt_bus.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/runtime_log.hpp"
#include "transfer/push_protocol_codec.hpp"
#include "transfer/push_receive_orchestrator.hpp"
#include "transfer/push_session_store.hpp"
#include "transfer/transfer_orchestrator.hpp"
#include "transfer/version.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {

const char* kDefaultConfigPath = "config/transferFile.json";    // 默认配置文件路径

std::atomic<bool> g_running{true}; // 运行标志, 默认true, 收到信号时设置为false

void onSignal(int) { g_running = false; } // 信号处理函数, 收到信号时设置为false

void printUsage(const char* prog) {
    std::cout << "用法: " << prog << " [选项]\n"
              << "  -c, --config <路径>  配置文件 (默认: " << kDefaultConfigPath << ")\n"
              << "  --simulate           内存模拟演示（强制 useSimulatedBus）\n"
              << "  -h, --help           显示帮助\n";
}

std::string joinPathRoots(const std::vector<std::string>& roots) {    // 路径拼接函数, 将多个路径拼接成一个字符串
    std::ostringstream os; // 路径拼接, 将多个路径拼接成一个字符串
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i) os << ", ";
        os << roots[i]; // 拼接路径, 将路径拼接成一个字符串
    }
    return os.str(); // 返回拼接后的路径字符串
}

// Topic 去掉公共前缀，避免重复打印 transfer/sim/gw001/
std::string shortTopic(const std::string& topic, const std::string& prefix) {    // 去掉公共前缀函数, 去掉topic中的公共前缀
    if (prefix.empty() || topic.size() < prefix.size()) return topic; // 如果前缀为空或topic长度小于前缀长度, 则返回topic
    if (topic.compare(0, prefix.size(), prefix) == 0) {     
        return topic.substr(prefix.size()); // 如果topic以prefix开头, 则返回topic去掉prefix后的字符串
    }
    return topic; // 否则返回topic
}

void printTopicLine(const char* dir, const char* name, const std::string& suffix) {    // 打印Topic行函数, 打印Topic行
    std::cout << "      " << std::left << std::setw(4) << dir << std::setw(18) << name << " "
              << suffix << "\n"; // 打印Topic行, 打印Topic行
}

void printBanner(const transfer::AppConfig& app) {
    const auto& mqtt = app.mqtt;    // 获取MQTT配置
    const auto& tr = app.transfer;    // 获取传输配置
    const std::string topicBase = "transfer/sim/" + mqtt.gatewayId + "/";    // 获取Topic基址

    std::string mode = mqtt.useSimulatedBus ? "模拟总线" : "mosquitto";    // 获取传输模式
    if (!mqtt.useSimulatedBus && !transfer::isMosquittoSupported()) {    // 如果传输模式为模拟总线且未编译mosquitto, 则添加(未编译)
        mode += "(未编译)";    // 添加(未编译)
    }

    std::cout << "  transferFile V" << transfer::kVersionString << "\n"
              << "  ---------------------------------------------\n"
              << "  编译  " << transfer::kBuildDateTime << "\n"
              << "  配置  "
              << (app.configFilePath.empty() ? "(内置默认)" : app.configFilePath) << "\n"
              << "  传输  " << tr.timeoutSec << "s · " << tr.chunkSize << "B    路径  "
              << joinPathRoots(tr.allowedPathRoots) << "\n"
              << "  MQTT  " << mqtt.brokerHost << ":" << mqtt.brokerPort << " · "
              << mqtt.gatewayId;
    if (!mqtt.clientId.empty() && mqtt.clientId.find(mqtt.gatewayId) == std::string::npos) {
        std::cout << " · " << mqtt.clientId;
    }
    std::cout << " · " << mode << "\n"
              << "  Topic " << topicBase << "\n"
              << "      召唤上传\n";
    printTopicLine("sub", "summon", shortTopic(mqtt.topicSummon, topicBase));
    printTopicLine("pub", "brief", shortTopic(mqtt.topicBrief, topicBase));
    printTopicLine("pub", "content", shortTopic(mqtt.topicContent, topicBase));
    printTopicLine("sub", "content_confirm",
                   shortTopic(mqtt.topicContentConfirm, topicBase));
    std::cout << "      平台推送\n";
    printTopicLine("sub", "push/brief", shortTopic(mqtt.topicPushBrief, topicBase));
    printTopicLine("pub", "push/brief_ok",
                    shortTopic(mqtt.topicPushBriefConfirm, topicBase));
    printTopicLine("sub", "push/content", shortTopic(mqtt.topicPushContent, topicBase));
    printTopicLine("pub", "push/content_ok",
                    shortTopic(mqtt.topicPushContentConfirm, topicBase));
    std::cout << "  ---------------------------------------------\n"
              << "  就绪  召唤上传 + 平台推送 (Ctrl+C 退出)\n\n";
}

// --simulate：发布一条召唤并打印平台模拟器收到的简报/内容
int runSimulateDemo(transfer::SimulatedMqttBus& bus, const transfer::AppConfig& app,
                    transfer::TransferOrchestrator& orch, transfer::IGatewayMqtt& mqtt,
                    transfer::TimeoutWatchdog& watchdog) {
    std::string err;
    if (!mqtt.start(err)) {
        std::cerr << err << "\n";
        return 1;
    }

    const std::string testPath = "/tmp/transfer_demo_file.bin";
    {
        std::ofstream out(testPath, std::ios::binary);
        out << "0123456789";
    }

    transfer::PlatformMqttSimulator platform(bus, app.mqtt);    // 平台模拟器
    const std::string summon = R"({"Data":{"CmdId":"9001","FullPathFileName":")" +
                               testPath + R"(","StartByte":"1"}})";    // 召唤命令
    std::cout << "[平台模拟] 发布召唤 -> " << app.mqtt.topicSummon << "\n";    // 打印召唤命令
    platform.publishSummon(summon);    // 发布召唤命令
    watchdog.tick();    // 看门狗tick

    std::cout << "[平台模拟] 收到应答 " << platform.received().size() << " 条:\n";    // 打印收到应答条数
    for (const auto& kv : platform.received()) {    // 遍历收到应答
        std::cout << "  Topic: " << kv.first << "\n  Payload: " << kv.second << "\n\n";    // 打印收到应答
    }
    mqtt.stop();    // 停止MQTT
    return 0;    // 返回0
}

bool parseArgs(int argc, char* argv[], std::string& configPath, bool& simulate,    // 解析参数函数, 解析参数
               bool& showHelp) {
    configPath = kDefaultConfigPath;    // 默认配置文件路径
    simulate = false;    // 模拟模式
    showHelp = false;    // 帮助模式
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];    // 获取参数
        if (arg == "--simulate") {
            simulate = true;    // 设置模拟模式
        } else if (arg == "-h" || arg == "--help") {
            showHelp = true;    // 设置帮助模式
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];    // 获取配置文件路径
        } else {    // 未知参数
            std::cerr << "未知参数: " << arg << "\n";
            return false;    // 返回false
        }
    }
    return true;    // 返回true
}

}  // namespace

int main(int argc, char* argv[]) {
    struct LogGuard {    // 日志守护函数, 日志守护函数
        ~LogGuard() { transfer::log::shutdown(); }
    } logGuard;    // 日志守护函数, 日志守护函数

    std::string configPath;    // 配置文件路径
    bool simulate = false;    // 模拟模式
    bool showHelp = false;    // 帮助模式
    if (!parseArgs(argc, argv, configPath, simulate, showHelp)) {
        printUsage(argv[0]);    // 打印使用方法
        return 1;
    }
    if (showHelp) {
        printUsage(argv[0]);
        return 0;
    }

    transfer::AppConfig app;
    std::string loadErr;
    if (!transfer::loadAppConfig(configPath, app, loadErr)) {
        std::cerr << "加载配置失败: " << loadErr << "\n";
        return 1;
    }

    transfer::log::init(app.log);    // 初始化日志

    if (simulate) {
        app.mqtt.useSimulatedBus = true;    // 设置模拟模式
    }

    printBanner(app);    // 打印Banner

    transfer::SimulatedMqttBus bus;    // 模拟总线
    std::string mqttCreateErr;    // MQTT创建错误
    std::unique_ptr<transfer::IGatewayMqtt> mqtt =
        transfer::createGatewayMqtt(app.mqtt.useSimulatedBus ? &bus : nullptr, app.mqtt,
                                  mqttCreateErr);
    if (!mqtt) {    // 如果MQTT创建失败
        std::cerr << "MQTT 初始化失败: " << mqttCreateErr << "\n";
        return 1;
    }

    transfer::JsonProtocolCodec codec;  // 协议编解码器
    transfer::JsonPushProtocolCodec pushCodec; // 推送协议编解码器
    transfer::FileStore fileStore(app.transfer.allowedPathRoots); // 文件存储器
    transfer::MemorySessionStore sessionStore; // 会话存储器
    transfer::MemoryPushSessionStore pushSessionStore; // 推送会话存储器
    transfer::SteadyClock clock; // 时钟
    transfer::TimeoutWatchdog watchdog(clock); // 看门狗
    transfer::Crc32Calculator crcCalc; // CRC32计算器

    transfer::TransferOrchestrator orch(codec, fileStore, sessionStore, watchdog, *mqtt,    // 传输编排器
                                        crcCalc, app.transfer);
    transfer::PushReceiveOrchestrator pushOrch(pushCodec, fileStore, pushSessionStore,    // 推送接收编排器
                                               watchdog, *mqtt, app.transfer);

    orch.setBusyChecker([&]() { return pushSessionStore.hasActiveSession(); });    // 设置忙碌检查器
    pushOrch.setBusyChecker([&]() {
        return sessionStore.hasActiveSessionOtherThan(0);    // 设置忙碌检查器
    });

    mqtt->setSummonHandler([&orch](std::string_view payload) { orch.onSummon(payload); });    // 设置召唤处理函数
    mqtt->setContentConfirmHandler(
        [&orch](std::string_view payload) { orch.onContentConfirm(payload); });    // 设置内容确认处理函数
    mqtt->setPushBriefHandler(
        [&pushOrch](std::string_view payload) { pushOrch.onPushBrief(payload); });    // 设置推送简报处理函数
    mqtt->setPushContentHandler(
        [&pushOrch](std::string_view payload) { pushOrch.onPushContent(payload); });    // 设置推送内容处理函数
    watchdog.setCallback([&](uint32_t cmdId) {
        orch.onTimeout(cmdId);    // 设置超时处理函数
        pushOrch.onTimeout(cmdId);    // 设置超时处理函数
    });

    if (simulate) {
        return runSimulateDemo(bus, app, orch, *mqtt, watchdog);    // 运行模拟演示
    }

    std::string startErr;
    if (!mqtt->start(startErr)) {    // 启动MQTT
        std::cerr << "MQTT 启动失败: " << startErr << "\n";
        return 1;
    }

    std::signal(SIGINT, onSignal);    // 设置信号处理函数
    std::signal(SIGTERM, onSignal);    // 设置信号处理函数

    transfer::log::gatewayInfo("服务就绪：召唤上传 + 平台推送");

    while (g_running) {
        int loopRc = mqtt->loop(100);    // 循环100毫秒
        if (loopRc != 0 && !app.mqtt.useSimulatedBus) {
            std::cerr << "mosquitto_loop 异常: " << loopRc << "\n";    // 打印mosquitto_loop异常
        }
        watchdog.tick();    // 看门狗tick
        std::this_thread::sleep_for(std::chrono::milliseconds(10));    // 睡眠10毫秒
    }

    mqtt->stop();    // 停止MQTT
    std::cout << "已退出\n";    // 打印已退出
    return 0;    // 返回0
}
