// 测试用 MQTT 发布记录器

#include "transfer/mqtt_publisher.hpp"

namespace transfer {

bool RecordingMqttPublisher::publishBrief(std::string_view jsonUtf8) {    // 发布简报
    messages_.push_back({PublishedMessage::Kind::Brief, std::string(jsonUtf8)});    // 添加消息
    return true;    // 返回true
}

bool RecordingMqttPublisher::publishContent(std::string_view jsonUtf8) {    // 发布内容
    messages_.push_back({PublishedMessage::Kind::Content, std::string(jsonUtf8)});    // 添加消息
    return true;    // 返回true
}

void RecordingMqttPublisher::clear() { messages_.clear(); }    // 清空消息

}  // namespace transfer    
