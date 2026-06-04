// 召唤上传方向：网关 MQTT 发布接口
// 发布文件简报与文件内容段 JSON。

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace transfer {

struct PublishedMessage {
    enum class Kind { Brief, Content };
    Kind kind;
    std::string payload;
};

class IMqttPublisher {
public:
    virtual ~IMqttPublisher() = default;
    virtual bool publishBrief(std::string_view jsonUtf8) = 0;
    virtual bool publishContent(std::string_view jsonUtf8) = 0;
};

// 测试用：记录发布顺序与载荷，不连接真实 Broker
class RecordingMqttPublisher : public IMqttPublisher {
public:
    bool publishBrief(std::string_view jsonUtf8) override;
    bool publishContent(std::string_view jsonUtf8) override;

    const std::vector<PublishedMessage>& messages() const { return messages_; }
    void clear();

private:
    std::vector<PublishedMessage> messages_;
};

}  // namespace transfer
