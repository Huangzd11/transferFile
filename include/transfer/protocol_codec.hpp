#pragma once

#include "transfer/types.hpp"

#include <string>
#include <string_view>

namespace transfer {

class IProtocolCodec {
public:
    virtual ~IProtocolCodec() = default;

    virtual bool decodeSummon(std::string_view jsonUtf8, SummonRequest& out,
                              std::string& errorDetail) = 0;

    virtual std::string encodeBrief(const BriefResponse& brief) = 0;
    virtual std::string encodeContent(const ContentSegment& seg) = 0;
};

class JsonProtocolCodec : public IProtocolCodec {
public:
    bool decodeSummon(std::string_view jsonUtf8, SummonRequest& out,
                      std::string& errorDetail) override;

    std::string encodeBrief(const BriefResponse& brief) override;
    std::string encodeContent(const ContentSegment& seg) override;
};

}  // namespace transfer
