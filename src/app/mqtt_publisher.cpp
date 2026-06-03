#include "transfer/mqtt_publisher.hpp"

namespace transfer {

bool RecordingMqttPublisher::publishBrief(std::string_view jsonUtf8) {
    messages_.push_back({PublishedMessage::Kind::Brief, std::string(jsonUtf8)});
    return true;
}

bool RecordingMqttPublisher::publishContent(std::string_view jsonUtf8) {
    messages_.push_back({PublishedMessage::Kind::Content, std::string(jsonUtf8)});
    return true;
}

void RecordingMqttPublisher::clear() { messages_.clear(); }

}  // namespace transfer
