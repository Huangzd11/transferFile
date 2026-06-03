#include "transfer/protocol_codec.hpp"

#include "transfer/base64.hpp"

#include <sstream>

namespace transfer {
namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out += c;
                break;
        }
    }
    return out;
}

bool extractJsonStringField(std::string_view json, std::string_view key,
                            std::string& out) {
    const std::string pattern = std::string("\"") + std::string(key) + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '\\' && pos < json.size()) {
            char e = json[pos++];
            if (e == 'n')
                out += '\n';
            else if (e == 'r')
                out += '\r';
            else if (e == 't')
                out += '\t';
            else
                out += e;
            continue;
        }
        if (c == '"') return true;
        out += c;
    }
    return false;
}

bool parseU64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        uint64_t next = v * 10 + static_cast<uint64_t>(c - '0');
        if (next < v) return false;
        v = next;
    }
    out = v;
    return true;
}

bool parseU32(const std::string& s, uint32_t& out) {
    uint64_t v = 0;
    if (!parseU64(s, v) || v > 0xFFFFFFFFULL) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

}  // namespace

bool JsonProtocolCodec::decodeSummon(std::string_view jsonUtf8, SummonRequest& out,
                                     std::string& errorDetail) {
    out = SummonRequest{};
    if (jsonUtf8.find("\"Data\"") == std::string_view::npos) {
        errorDetail = "missing Data";
        return false;
    }

    std::string cmdStr, path, startStr;
    if (!extractJsonStringField(jsonUtf8, "CmdId", cmdStr) ||
        !extractJsonStringField(jsonUtf8, "FullPathFileName", path) ||
        !extractJsonStringField(jsonUtf8, "StartByte", startStr)) {
        errorDetail = "missing required field";
        return false;
    }

    if (!parseU32(cmdStr, out.cmdId)) {
        errorDetail = "invalid CmdId";
        return false;
    }
    if (!parseU64(startStr, out.startByte) || out.startByte < 1) {
        errorDetail = "invalid StartByte";
        return false;
    }

    out.fullPathFileName = std::move(path);
    out.fileOffset = out.startByte - 1;
    out.rawValid = true;
    return true;
}

std::string JsonProtocolCodec::encodeBrief(const BriefResponse& brief) {
    std::ostringstream os;
    os << "{\"Data\":{";
    os << "\"CmdId\":\"" << brief.cmdId << "\",";
    os << "\"Status\":\""
       << (brief.status == BriefStatus::Success ? "0" : "1") << "\",";
    os << "\"ErrorCode\":\"" << jsonEscape(brief.errorCode) << "\",";
    os << "\"Note\":\"" << jsonEscape(brief.note) << "\",";
    os << "\"FileCrc\":\"" << jsonEscape(brief.fileCrc) << "\",";
    os << "\"FileSize\":\"" << brief.fileSize << "\",";
    os << "\"ModifyTime\":\"" << jsonEscape(brief.modifyTime) << "\"";
    os << "}}";
    return os.str();
}

std::string JsonProtocolCodec::encodeContent(const ContentSegment& seg) {
    std::ostringstream os;
    os << "{\"Data\":{";
    os << "\"CmdId\":\"" << seg.cmdId << "\",";
    os << "\"FileSegNo\":\"" << seg.fileSegNo << "\",";
    os << "\"Content\":\"" << jsonEscape(base64Encode(seg.contentRaw)) << "\",";
    os << "\"Continue\":\"" << (seg.continueFlag ? "1" : "0") << "\"";
    os << "}}";
    return os.str();
}

}  // namespace transfer
