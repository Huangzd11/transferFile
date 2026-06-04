// libmosquitto 真实 MQTT 适配器
// useSimulatedBus=false 时连接 Broker，订阅召唤/推送/内容确认 Topic。

#pragma once

#include "transfer/gateway_mqtt.hpp"
#include "transfer/mqtt_config.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

struct mosquitto;

namespace transfer {

// libmosquitto 真实 MQTT 适配（useSimulatedBus=false）
class MosquittoMqttAdapter : public IGatewayMqtt {     //  MosquittoMqttAdapter 继承 IGatewayMqtt
public:
    explicit MosquittoMqttAdapter(const MqttConfig& config);    // 构造函数 
    ~MosquittoMqttAdapter() override;    // 析构函数

    void setSummonHandler(std::function<void(std::string_view)> handler) override;    // 设置召唤处理函数
    void setPushBriefHandler(std::function<void(std::string_view)> handler) override;    // 设置推送简报处理函数
    void setPushContentHandler(std::function<void(std::string_view)> handler) override;    // 设置推送内容处理函数
    void setContentConfirmHandler(std::function<void(std::string_view)> handler) override;    // 设置内容确认处理函数
    bool start(std::string& errorDetail) override;    // 启动
    void stop() override;    // 停止
    int loop(int timeoutMs) override;    // 循环

    bool publishBrief(std::string_view jsonUtf8) override;    // 发布简报
    bool publishContent(std::string_view jsonUtf8) override;    // 发布内容
    bool publishPushBriefConfirm(std::string_view jsonUtf8) override;    // 发布简报确认
    bool publishPushContentConfirm(std::string_view jsonUtf8) override;    // 发布内容确认

    const MqttConfig& config() const override { return config_; }    // 获取配置

    void dispatchSummon(const std::string& payload);    // 分发召唤
    void dispatchPushBrief(const std::string& payload);    // 分发推送简报
    void dispatchPushContent(const std::string& payload);    // 分发推送内容
    void dispatchContentConfirm(const std::string& payload);    // 分发内容确认

private:
    bool publishTopic(const std::string& topic, std::string_view jsonUtf8);    // 发布Topic

    MqttConfig config_;    // 配置
    mosquitto* mosq_ = nullptr;    //  mosquitto实例
    std::function<void(std::string_view)> onSummon_;    // 召唤处理函数
    std::function<void(std::string_view)> onPushBrief_;    // 推送简报处理函数
    std::function<void(std::string_view)> onPushContent_;    // 推送内容处理函数
    std::function<void(std::string_view)> onContentConfirm_;    // 内容确认处理函数
    std::mutex handlerMutex_;    // 处理函数互斥锁
    std::atomic<bool> connected_{false};    // 连接状态
    bool started_ = false;    // 启动状态
};    //  MosquittoMqttAdapter

}  // namespace transfer
