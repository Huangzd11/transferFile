// 内存 MQTT 总线模拟
// 按 Topic 同步分发消息，用于无 Broker 的单测与 --simulate 演示。

#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace transfer {

// 内存 MQTT 模拟：按 Topic 分发消息，用于无 Broker 时的联调
class SimulatedMqttBus {
public:
    using MessageHandler = std::function<void(const std::string& topic, const std::string& payload)>;

    void subscribe(const std::string& topic, MessageHandler handler);
  void unsubscribe(const std::string& topic);

    // 发布消息并同步调用该 Topic 所有订阅者
    void publish(const std::string& topic, const std::string& payload);

    // 记录最近消息（便于模拟平台侧查看网关应答）
    const std::vector<std::pair<std::string, std::string>>& history() const {
        return history_;
    }
    void clearHistory();

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::vector<MessageHandler>> subscribers_;
    std::vector<std::pair<std::string, std::string>> history_;
};

}  // namespace transfer
