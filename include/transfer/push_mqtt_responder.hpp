#pragma once

#include <string>
#include <string_view>

namespace transfer {

// 网关向平台回复推送确认（简报确认 / 内容确认）
class IPushMqttResponder {
public:
    virtual ~IPushMqttResponder() = default;
    virtual bool publishPushBriefConfirm(std::string_view jsonUtf8) = 0;
    virtual bool publishPushContentConfirm(std::string_view jsonUtf8) = 0;
};

}  // namespace transfer
