// 平台推送协议 JSON 编解码接口
// 推送简报/内容段解码，简报确认/内容确认编码。

#pragma once

#include "transfer/push_types.hpp"

#include <string>
#include <string_view>

namespace transfer {

// 平台推送协议编解码抽象
class IPushProtocolCodec {
public:
    virtual ~IPushProtocolCodec() = default;

    // 平台 → 网关
    virtual bool decodePushBrief(std::string_view jsonUtf8, PushBriefRequest& out,
                                 std::string& errorDetail) = 0;
    virtual bool decodePushContent(std::string_view jsonUtf8, PushContentSegment& out,
                                 std::string& errorDetail) = 0;

    // 网关 → 平台
    virtual std::string encodePushBriefConfirm(const ProtocolConfirmResponse& resp) = 0;
    virtual std::string encodePushContentConfirm(const ProtocolConfirmResponse& resp) = 0;
};

class JsonPushProtocolCodec : public IPushProtocolCodec {
public:
    bool decodePushBrief(std::string_view jsonUtf8, PushBriefRequest& out,
                         std::string& errorDetail) override;
    bool decodePushContent(std::string_view jsonUtf8, PushContentSegment& out,
                           std::string& errorDetail) override;

    std::string encodePushBriefConfirm(const ProtocolConfirmResponse& resp) override;
    std::string encodePushContentConfirm(const ProtocolConfirmResponse& resp) override;
};

// 解析协议 FileCrc 字符串（如 0xAABBCCDD）为数值
bool parseFileCrcHex(const std::string& crcStr, uint32_t& out);

}  // namespace transfer
