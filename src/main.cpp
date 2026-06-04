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

const char* kDefaultConfigPath = "config/transferFile.json";

std::atomic<bool> g_running{true};

void onSignal(int) { g_running = false; }

void printUsage(const char* prog) {
    std::cout << "用法: " << prog << " [选项]\n"
              << "  -c, --config <路径>  配置文件 (默认: " << kDefaultConfigPath << ")\n"
              << "  --simulate           内存模拟演示（强制 useSimulatedBus）\n"
              << "  -h, --help           显示帮助\n";
}

std::string joinPathRoots(const std::vector<std::string>& roots) {
    std::ostringstream os;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (i) os << ", ";
        os << roots[i];
    }
    return os.str();
}

// Topic 去掉公共前缀，避免重复打印 transfer/sim/gw001/
std::string shortTopic(const std::string& topic, const std::string& prefix) {
    if (prefix.empty() || topic.size() < prefix.size()) return topic;
    if (topic.compare(0, prefix.size(), prefix) == 0) {
        return topic.substr(prefix.size());
    }
    return topic;
}

void printTopicLine(const char* dir, const char* name, const std::string& suffix) {
    std::cout << "      " << std::left << std::setw(4) << dir << std::setw(18) << name << " "
              << suffix << "\n";
}

void printBanner(const transfer::AppConfig& app) {
    const auto& mqtt = app.mqtt;
    const auto& tr = app.transfer;
    const std::string topicBase = "transfer/sim/" + mqtt.gatewayId + "/";

    std::string mode = mqtt.useSimulatedBus ? "模拟总线" : "mosquitto";
    if (!mqtt.useSimulatedBus && !transfer::isMosquittoSupported()) {
        mode += "(未编译)";
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

    transfer::PlatformMqttSimulator platform(bus, app.mqtt);
    const std::string summon = R"({"Data":{"CmdId":"9001","FullPathFileName":")" +
                               testPath + R"(","StartByte":"1"}})";
    std::cout << "[平台模拟] 发布召唤 -> " << app.mqtt.topicSummon << "\n";
    platform.publishSummon(summon);
    watchdog.tick();

    std::cout << "[平台模拟] 收到应答 " << platform.received().size() << " 条:\n";
    for (const auto& kv : platform.received()) {
        std::cout << "  Topic: " << kv.first << "\n  Payload: " << kv.second << "\n\n";
    }
    mqtt.stop();
    return 0;
}

bool parseArgs(int argc, char* argv[], std::string& configPath, bool& simulate,
               bool& showHelp) {
    configPath = kDefaultConfigPath;
    simulate = false;
    showHelp = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--simulate") {
            simulate = true;
        } else if (arg == "-h" || arg == "--help") {
            showHelp = true;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else {
            std::cerr << "未知参数: " << arg << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    struct LogGuard {
        ~LogGuard() { transfer::log::shutdown(); }
    } logGuard;

    std::string configPath;
    bool simulate = false;
    bool showHelp = false;
    if (!parseArgs(argc, argv, configPath, simulate, showHelp)) {
        printUsage(argv[0]);
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

    transfer::log::init(app.log);

    if (simulate) {
        app.mqtt.useSimulatedBus = true;
    }

    printBanner(app);

    transfer::SimulatedMqttBus bus;
    std::string mqttCreateErr;
    std::unique_ptr<transfer::IGatewayMqtt> mqtt =
        transfer::createGatewayMqtt(app.mqtt.useSimulatedBus ? &bus : nullptr, app.mqtt,
                                  mqttCreateErr);
    if (!mqtt) {
        std::cerr << "MQTT 初始化失败: " << mqttCreateErr << "\n";
        return 1;
    }

    transfer::JsonProtocolCodec codec;
    transfer::JsonPushProtocolCodec pushCodec;
    transfer::FileStore fileStore(app.transfer.allowedPathRoots);
    transfer::MemorySessionStore sessionStore;
    transfer::MemoryPushSessionStore pushSessionStore;
    transfer::SteadyClock clock;
    transfer::TimeoutWatchdog watchdog(clock);
    transfer::Crc32Calculator crcCalc;

    transfer::TransferOrchestrator orch(codec, fileStore, sessionStore, watchdog, *mqtt,
                                        crcCalc, app.transfer);
    transfer::PushReceiveOrchestrator pushOrch(pushCodec, fileStore, pushSessionStore,
                                               watchdog, *mqtt, app.transfer);

    orch.setBusyChecker([&]() { return pushSessionStore.hasActiveSession(); });
    pushOrch.setBusyChecker([&]() {
        return sessionStore.hasActiveSessionOtherThan(0);
    });

    mqtt->setSummonHandler([&orch](std::string_view payload) { orch.onSummon(payload); });
    mqtt->setContentConfirmHandler(
        [&orch](std::string_view payload) { orch.onContentConfirm(payload); });
    mqtt->setPushBriefHandler(
        [&pushOrch](std::string_view payload) { pushOrch.onPushBrief(payload); });
    mqtt->setPushContentHandler(
        [&pushOrch](std::string_view payload) { pushOrch.onPushContent(payload); });
    watchdog.setCallback([&](uint32_t cmdId) {
        orch.onTimeout(cmdId);
        pushOrch.onTimeout(cmdId);
    });

    if (simulate) {
        return runSimulateDemo(bus, app, orch, *mqtt, watchdog);
    }

    std::string startErr;
    if (!mqtt->start(startErr)) {
        std::cerr << "MQTT 启动失败: " << startErr << "\n";
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    transfer::log::gatewayInfo("服务就绪：召唤上传 + 平台推送");

    while (g_running) {
        int loopRc = mqtt->loop(100);
        if (loopRc != 0 && !app.mqtt.useSimulatedBus) {
            std::cerr << "mosquitto_loop 异常: " << loopRc << "\n";
        }
        watchdog.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    mqtt->stop();
    std::cout << "已退出\n";
    return 0;
}
