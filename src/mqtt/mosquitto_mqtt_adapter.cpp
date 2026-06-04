// libmosquitto MQTT 适配器
// 未定义 TRANSFER_WITH_MOSQUITTO 时提供空桩实现，start 返回未编译错误。

#include "transfer/mosquitto_mqtt_adapter.hpp"

#ifdef TRANSFER_WITH_MOSQUITTO

#include "transfer/runtime_log.hpp"

#include <mosquitto.h>

#include <iostream>

namespace transfer {
namespace {

// 连接成功后订阅召唤、推送简报/内容、内容确认 Topic
void onConnect(struct mosquitto* mosq, void* userdata, int rc) {
    auto* self = static_cast<MosquittoMqttAdapter*>(userdata);    // 转换为MosquittoMqttAdapter
    if (rc != 0) {
        std::cerr << "MQTT on_connect 失败 rc=" << rc << "\n";    // 打印MQTT on_connect失败
        return;
    }
    const MqttConfig& cfg = self->config();    // 获取配置
    mosquitto_subscribe(mosq, nullptr, cfg.topicSummon.c_str(), cfg.qos);    // 订阅召唤Topic
    mosquitto_subscribe(mosq, nullptr, cfg.topicPushBrief.c_str(), cfg.qos);    // 订阅推送简报Topic
    mosquitto_subscribe(mosq, nullptr, cfg.topicPushContent.c_str(), cfg.qos);    // 订阅推送内容Topic
    mosquitto_subscribe(mosq, nullptr, cfg.topicContentConfirm.c_str(), cfg.qos);    // 订阅内容确认Topic
    log::gatewayInfo("MQTT 已连接，已订阅Topic：召唤/推送/内容确认");
}

// 按 Topic 分发到编排器注册的 handler
void onMessage(struct mosquitto* /*mosq*/, void* userdata, const struct mosquitto_message* message) {
    auto* self = static_cast<MosquittoMqttAdapter*>(userdata);    // 转换为MosquittoMqttAdapter
    if (!message || !message->payload || !message->topic) return;    // 如果消息为空，则返回
    std::string topic(message->topic);    // 获取Topic
    std::string payload(static_cast<const char*>(message->payload), message->payloadlen);    // 获取Payload
    const MqttConfig& cfg = self->config();    // 获取配置
    if (topic == cfg.topicSummon) {    // 如果Topic为召唤Topic，则分发召唤
        self->dispatchSummon(payload);    // 分发召唤
    } else if (topic == cfg.topicPushBrief) {    // 如果Topic为推送简报Topic，则分发推送简报
        self->dispatchPushBrief(payload);    // 分发推送简报
    } else if (topic == cfg.topicPushContent) {    // 如果Topic为推送内容Topic，则分发推送内容
        self->dispatchPushContent(payload);    // 分发推送内容
    } else if (topic == cfg.topicContentConfirm) {    // 如果Topic为内容确认Topic，则分发内容确认
        self->dispatchContentConfirm(payload);    // 分发内容确认
    }
}

}  // namespace

void MosquittoMqttAdapter::dispatchSummon(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁
    if (onSummon_) onSummon_(payload);    // 调用召唤处理函数
}

void MosquittoMqttAdapter::dispatchPushBrief(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁
    if (onPushBrief_) onPushBrief_(payload);    // 调用推送简报处理函数
}

void MosquittoMqttAdapter::dispatchPushContent(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁
    if (onPushContent_) onPushContent_(payload);    // 调用推送内容处理函数
}

void MosquittoMqttAdapter::dispatchContentConfirm(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁
    if (onContentConfirm_) onContentConfirm_(payload);    // 调用内容确认处理函数
}

MosquittoMqttAdapter::MosquittoMqttAdapter(const MqttConfig& config) : config_(config) {}    // 构造函数

MosquittoMqttAdapter::~MosquittoMqttAdapter() { stop(); }    // 析构函数

void MosquittoMqttAdapter::setSummonHandler(std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁   
    onSummon_ = std::move(handler);    // 设置召唤处理函数
}

void MosquittoMqttAdapter::setPushBriefHandler(std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁
    onPushBrief_ = std::move(handler);    // 设置推送简报处理函数
}

void MosquittoMqttAdapter::setPushContentHandler(std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁   
    onPushContent_ = std::move(handler);    // 设置推送内容处理函数
}

void MosquittoMqttAdapter::setContentConfirmHandler(
    std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);    // 锁定互斥锁
    onContentConfirm_ = std::move(handler);    // 设置内容确认处理函数
}

bool MosquittoMqttAdapter::start(std::string& errorDetail) {
    if (started_) return true;    // 如果已经启动，则返回true

    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
        errorDetail = "mosquitto_lib_init 失败";    // 设置错误详情
        return false;
    }

    mosq_ = mosquitto_new(config_.clientId.c_str(), true, this);
    if (!mosq_) {
        errorDetail = "mosquitto_new 失败";    // 设置错误详情
        mosquitto_lib_cleanup();
        return false;
    }

    mosquitto_connect_callback_set(mosq_, onConnect);    // 设置连接回调函数        
    mosquitto_message_callback_set(mosq_, onMessage);    // 设置消息回调函数

    if (!config_.username.empty()) {    // 如果用户名不为空，则设置用户名和密码
        mosquitto_username_pw_set(mosq_, config_.username.c_str(),
                                  config_.password.empty() ? nullptr    // 如果密码为空，则设置为空
                                                          : config_.password.c_str());    // 设置密码
    }

    int rc = mosquitto_connect(mosq_, config_.brokerHost.c_str(), config_.brokerPort,    // 连接MQTT Broker
                               static_cast<int>(config_.keepAliveSec));    // 设置保持连接时间
    if (rc != MOSQ_ERR_SUCCESS) {
        errorDetail = std::string("mosquitto_connect: ") + mosquitto_strerror(rc);    // 设置错误详情
        mosquitto_destroy(mosq_);    // 销毁mosquitto实例
        mosq_ = nullptr;    // 设置mosquitto实例为空
        mosquitto_lib_cleanup();    // 清理mosquitto库
        return false;
    }

    started_ = true;
    log::gatewayInfo("正在连接 MQTT Broker " + config_.brokerHost + ":" +    
                     std::to_string(config_.brokerPort) + " ...");    // 打印连接MQTT Broker端口
    return true;
}

void MosquittoMqttAdapter::stop() {
    if (!started_ && !mosq_) return;    // 如果未启动或mosquitto实例为空，则返回
    if (mosq_) {
        mosquitto_disconnect(mosq_);    // 断开连接
        mosquitto_destroy(mosq_);    // 销毁mosquitto实例
        mosq_ = nullptr;    // 设置mosquitto实例为空
    }
    mosquitto_lib_cleanup();    // 清理mosquitto库
    started_ = false;    // 设置启动状态为false
    connected_ = false;    // 设置连接状态为false
}

int MosquittoMqttAdapter::loop(int timeoutMs) {
    if (!mosq_) return -1;    // 如果mosquitto实例为空，则返回-1
    return mosquitto_loop(mosq_, timeoutMs, 1);    // 循环
}

bool MosquittoMqttAdapter::publishTopic(const std::string& topic, std::string_view jsonUtf8) {
    if (!mosq_) return false;    // 如果mosquitto实例为空，则返回false
    const std::string payload(jsonUtf8);    // 获取Payload
    int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(),    // 发布Topic
                               static_cast<int>(payload.size()), payload.data(),    // 设置Payload
                               config_.qos, false);    // 设置QoS和保留
    return rc == MOSQ_ERR_SUCCESS;    // 如果返回成功，则返回true
}

bool MosquittoMqttAdapter::publishBrief(std::string_view jsonUtf8) {
    return publishTopic(config_.topicBrief, jsonUtf8);    // 发布简报
}

bool MosquittoMqttAdapter::publishContent(std::string_view jsonUtf8) {
    return publishTopic(config_.topicContent, jsonUtf8);    // 发布内容
}

bool MosquittoMqttAdapter::publishPushBriefConfirm(std::string_view jsonUtf8) {
    return publishTopic(config_.topicPushBriefConfirm, jsonUtf8);    // 发布简报确认
}

bool MosquittoMqttAdapter::publishPushContentConfirm(std::string_view jsonUtf8) {
    return publishTopic(config_.topicPushContentConfirm, jsonUtf8);    // 发布内容确认
}

}  // namespace transfer

#else  // !TRANSFER_WITH_MOSQUITTO

namespace transfer {

void MosquittoMqttAdapter::dispatchSummon(const std::string&) {}
void MosquittoMqttAdapter::dispatchPushBrief(const std::string&) {}
void MosquittoMqttAdapter::dispatchPushContent(const std::string&) {}

MosquittoMqttAdapter::MosquittoMqttAdapter(const MqttConfig&) {}
MosquittoMqttAdapter::~MosquittoMqttAdapter() = default;
void MosquittoMqttAdapter::setSummonHandler(std::function<void(std::string_view)>) {}
void MosquittoMqttAdapter::setPushBriefHandler(std::function<void(std::string_view)>) {}
void MosquittoMqttAdapter::setPushContentHandler(std::function<void(std::string_view)>) {}
bool MosquittoMqttAdapter::start(std::string& errorDetail) {
    errorDetail = "构建时未启用 libmosquitto (TRANSFER_WITH_MOSQUITTO)";
    return false;
}
void MosquittoMqttAdapter::stop() {}
int MosquittoMqttAdapter::loop(int) { return -1; }
bool MosquittoMqttAdapter::publishBrief(std::string_view) { return false; }
bool MosquittoMqttAdapter::publishContent(std::string_view) { return false; }
bool MosquittoMqttAdapter::publishPushBriefConfirm(std::string_view) { return false; }
bool MosquittoMqttAdapter::publishPushContentConfirm(std::string_view) { return false; }

}  // namespace transfer

#endif
