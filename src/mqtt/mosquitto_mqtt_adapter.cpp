#include "transfer/mosquitto_mqtt_adapter.hpp"

#ifdef TRANSFER_WITH_MOSQUITTO

#include "transfer/runtime_log.hpp"

#include <mosquitto.h>

#include <iostream>

namespace transfer {
namespace {

void onConnect(struct mosquitto* mosq, void* userdata, int rc) {
    auto* self = static_cast<MosquittoMqttAdapter*>(userdata);
    if (rc != 0) {
        std::cerr << "MQTT on_connect 失败 rc=" << rc << "\n";
        return;
    }
    const MqttConfig& cfg = self->config();
    mosquitto_subscribe(mosq, nullptr, cfg.topicSummon.c_str(), cfg.qos);
    mosquitto_subscribe(mosq, nullptr, cfg.topicPushBrief.c_str(), cfg.qos);
    mosquitto_subscribe(mosq, nullptr, cfg.topicPushContent.c_str(), cfg.qos);
    mosquitto_subscribe(mosq, nullptr, cfg.topicContentConfirm.c_str(), cfg.qos);
    log::gatewayInfo("MQTT 已连接，已订阅召唤/推送/内容确认 Topic");
}

void onMessage(struct mosquitto* /*mosq*/, void* userdata,
               const struct mosquitto_message* message) {
    auto* self = static_cast<MosquittoMqttAdapter*>(userdata);
    if (!message || !message->payload || !message->topic) return;
    std::string topic(message->topic);
    std::string payload(static_cast<const char*>(message->payload), message->payloadlen);
    const MqttConfig& cfg = self->config();
    if (topic == cfg.topicSummon) {
        self->dispatchSummon(payload);
    } else if (topic == cfg.topicPushBrief) {
        self->dispatchPushBrief(payload);
    } else if (topic == cfg.topicPushContent) {
        self->dispatchPushContent(payload);
    } else if (topic == cfg.topicContentConfirm) {
        self->dispatchContentConfirm(payload);
    }
}

}  // namespace

void MosquittoMqttAdapter::dispatchSummon(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    if (onSummon_) onSummon_(payload);
}

void MosquittoMqttAdapter::dispatchPushBrief(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    if (onPushBrief_) onPushBrief_(payload);
}

void MosquittoMqttAdapter::dispatchPushContent(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    if (onPushContent_) onPushContent_(payload);
}

void MosquittoMqttAdapter::dispatchContentConfirm(const std::string& payload) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    if (onContentConfirm_) onContentConfirm_(payload);
}

MosquittoMqttAdapter::MosquittoMqttAdapter(const MqttConfig& config) : config_(config) {}

MosquittoMqttAdapter::~MosquittoMqttAdapter() { stop(); }

void MosquittoMqttAdapter::setSummonHandler(std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    onSummon_ = std::move(handler);
}

void MosquittoMqttAdapter::setPushBriefHandler(std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    onPushBrief_ = std::move(handler);
}

void MosquittoMqttAdapter::setPushContentHandler(std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    onPushContent_ = std::move(handler);
}

void MosquittoMqttAdapter::setContentConfirmHandler(
    std::function<void(std::string_view)> handler) {
    std::lock_guard<std::mutex> lock(handlerMutex_);
    onContentConfirm_ = std::move(handler);
}

bool MosquittoMqttAdapter::start(std::string& errorDetail) {
    if (started_) return true;

    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
        errorDetail = "mosquitto_lib_init 失败";
        return false;
    }

    mosq_ = mosquitto_new(config_.clientId.c_str(), true, this);
    if (!mosq_) {
        errorDetail = "mosquitto_new 失败";
        mosquitto_lib_cleanup();
        return false;
    }

    mosquitto_connect_callback_set(mosq_, onConnect);
    mosquitto_message_callback_set(mosq_, onMessage);

    if (!config_.username.empty()) {
        mosquitto_username_pw_set(mosq_, config_.username.c_str(),
                                  config_.password.empty() ? nullptr
                                                          : config_.password.c_str());
    }

    int rc = mosquitto_connect(mosq_, config_.brokerHost.c_str(), config_.brokerPort,
                               static_cast<int>(config_.keepAliveSec));
    if (rc != MOSQ_ERR_SUCCESS) {
        errorDetail = std::string("mosquitto_connect: ") + mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        mosquitto_lib_cleanup();
        return false;
    }

    started_ = true;
    log::gatewayInfo("正在连接 MQTT Broker " + config_.brokerHost + ":" +
                     std::to_string(config_.brokerPort) + " ...");
    return true;
}

void MosquittoMqttAdapter::stop() {
    if (!started_ && !mosq_) return;
    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    mosquitto_lib_cleanup();
    started_ = false;
    connected_ = false;
}

int MosquittoMqttAdapter::loop(int timeoutMs) {
    if (!mosq_) return -1;
    return mosquitto_loop(mosq_, timeoutMs, 1);
}

bool MosquittoMqttAdapter::publishTopic(const std::string& topic, std::string_view jsonUtf8) {
    if (!mosq_) return false;
    const std::string payload(jsonUtf8);
    int rc = mosquitto_publish(mosq_, nullptr, topic.c_str(),
                               static_cast<int>(payload.size()), payload.data(),
                               config_.qos, false);
    return rc == MOSQ_ERR_SUCCESS;
}

bool MosquittoMqttAdapter::publishBrief(std::string_view jsonUtf8) {
    return publishTopic(config_.topicBrief, jsonUtf8);
}

bool MosquittoMqttAdapter::publishContent(std::string_view jsonUtf8) {
    return publishTopic(config_.topicContent, jsonUtf8);
}

bool MosquittoMqttAdapter::publishPushBriefConfirm(std::string_view jsonUtf8) {
    return publishTopic(config_.topicPushBriefConfirm, jsonUtf8);
}

bool MosquittoMqttAdapter::publishPushContentConfirm(std::string_view jsonUtf8) {
    return publishTopic(config_.topicPushContentConfirm, jsonUtf8);
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
