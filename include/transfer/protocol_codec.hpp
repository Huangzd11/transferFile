// 召唤上传协议 JSON 编解码接口
// 召唤、简报、内容段及 V0.0.4 内容确认的序列化/反序列化。

#pragma once

#include "transfer/types.hpp"

#include <string>
#include <string_view>

namespace transfer {

// 召唤上传协议编解码抽象
class IProtocolCodec {
public:
    virtual ~IProtocolCodec() = default;

    // 平台 → 网关：解析召唤指令
    virtual bool decodeSummon(std::string_view jsonUtf8, SummonRequest& out,
                              std::string& errorDetail) = 0;

    // 网关 → 平台：文件简报、内容段
    virtual std::string encodeBrief(const BriefResponse& brief) = 0;
    virtual std::string encodeContent(const ContentSegment& seg) = 0;

    // V0.0.4 平台 → 网关：内容段确认；网关编码用于 platform_sim
    virtual bool decodeContentConfirm(std::string_view jsonUtf8, ContentConfirmRequest& out,
                                      std::string& errorDetail) = 0;
    virtual std::string encodeContentConfirm(const ContentConfirmRequest& req) = 0;
};

class JsonProtocolCodec : public IProtocolCodec {
public:
    bool decodeSummon(std::string_view jsonUtf8, SummonRequest& out,
                      std::string& errorDetail) override;

    std::string encodeBrief(const BriefResponse& brief) override;
    std::string encodeContent(const ContentSegment& seg) override;

    bool decodeContentConfirm(std::string_view jsonUtf8, ContentConfirmRequest& out,
                              std::string& errorDetail) override;
    std::string encodeContentConfirm(const ContentConfirmRequest& req) override;
};

}  // namespace transfer
