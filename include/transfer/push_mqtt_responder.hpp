// 平台推送方向：网关 MQTT 应答接口
// 发布推送简报确认与推送内容确认 JSON。

#pragma once

#include <string>
#include <string_view>

namespace transfer {

class IPushMqttResponder {
public:
    virtual ~IPushMqttResponder() = default;
    virtual bool publishPushBriefConfirm(std::string_view jsonUtf8) = 0;
    virtual bool publishPushContentConfirm(std::string_view jsonUtf8) = 0;
};

}  // namespace transfer
