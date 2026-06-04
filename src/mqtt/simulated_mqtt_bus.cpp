// 内存 MQTT 总线：订阅注册与同步 publish 分发

#include "transfer/simulated_mqtt_bus.hpp"

namespace transfer {

void SimulatedMqttBus::subscribe(const std::string& topic, MessageHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_[topic].push_back(std::move(handler));
}

void SimulatedMqttBus::unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.erase(topic);
}

void SimulatedMqttBus::publish(const std::string& topic, const std::string& payload) {
    std::vector<MessageHandler> handlers;
  {
        std::lock_guard<std::mutex> lock(mutex_);
        history_.push_back({topic, payload});
        auto it = subscribers_.find(topic);
        if (it != subscribers_.end()) handlers = it->second;
    }
    for (const auto& h : handlers) {
        if (h) h(topic, payload);
    }
}

void SimulatedMqttBus::clearHistory() {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.clear();
}

}  // namespace transfer
