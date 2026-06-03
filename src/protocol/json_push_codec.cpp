#include "transfer/push_protocol_codec.hpp"

#include "transfer/base64.hpp"

#include <cctype>
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

bool extractJsonStringField(std::string_view json, std::string_view key, std::string& out) {
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
        v = v * 10 + static_cast<uint64_t>(c - '0');
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

std::string encodeConfirmJson(const ProtocolConfirmResponse& resp, bool withSegNo) {
    std::ostringstream os;
    os << "{\"Data\":{";
    os << "\"CmdId\":\"" << resp.cmdId << "\",";
    if (withSegNo) {
        os << "\"FileSegNo\":\"" << resp.fileSegNo << "\",";
    }
    os << "\"Status\":\""
       << (resp.status == BriefStatus::Success ? "0" : "1") << "\",";
    os << "\"ErrorCode\":\"" << jsonEscape(resp.errorCode) << "\",";
    os << "\"Note\":\"" << jsonEscape(resp.note) << "\"";
    os << "}}";
    return os.str();
}

}  // namespace

bool parseFileCrcHex(const std::string& crcStr, uint32_t& out) {
    if (crcStr.empty()) return false;
    size_t i = 0;
    if (crcStr.size() >= 2 && crcStr[0] == '0' &&
        (crcStr[1] == 'x' || crcStr[1] == 'X')) {
        i = 2;
    }
    if (i >= crcStr.size()) return false;
    uint32_t v = 0;
    for (; i < crcStr.size(); ++i) {
        char c = crcStr[i];
        int d = -1;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            return false;
        v = (v << 4) | static_cast<uint32_t>(d);
    }
    out = v;
    return true;
}

bool JsonPushProtocolCodec::decodePushBrief(std::string_view jsonUtf8,
                                            PushBriefRequest& out,
                                            std::string& errorDetail) {
    out = PushBriefRequest{};
    if (jsonUtf8.find("\"Data\"") == std::string_view::npos) {
        errorDetail = "missing Data";
        return false;
    }
    std::string cmdStr, path, crc, sizeStr, mtime;
    if (!extractJsonStringField(jsonUtf8, "CmdId", cmdStr) ||
        !extractJsonStringField(jsonUtf8, "FullPathFileName", path) ||
        !extractJsonStringField(jsonUtf8, "FileCrc", crc) ||
        !extractJsonStringField(jsonUtf8, "FileSize", sizeStr) ||
        !extractJsonStringField(jsonUtf8, "ModifyTime", mtime)) {
        errorDetail = "missing required field";
        return false;
    }
    if (!parseU32(cmdStr, out.cmdId)) {
        errorDetail = "invalid CmdId";
        return false;
    }
    if (!parseU64(sizeStr, out.fileSize)) {
        errorDetail = "invalid FileSize";
        return false;
    }
    out.fullPathFileName = std::move(path);
    out.fileCrc = std::move(crc);
    out.modifyTime = std::move(mtime);
    return true;
}

bool JsonPushProtocolCodec::decodePushContent(std::string_view jsonUtf8,
                                              PushContentSegment& out,
                                              std::string& errorDetail) {
    out = PushContentSegment{};
    if (jsonUtf8.find("\"Data\"") == std::string_view::npos) {
        errorDetail = "missing Data";
        return false;
    }
    std::string cmdStr, segStr, b64, contFlag;
    if (!extractJsonStringField(jsonUtf8, "CmdId", cmdStr) ||
        !extractJsonStringField(jsonUtf8, "FileSegNo", segStr) ||
        !extractJsonStringField(jsonUtf8, "Content", b64) ||
        !extractJsonStringField(jsonUtf8, "Continue", contFlag)) {
        errorDetail = "missing required field";
        return false;
    }
    if (!parseU32(cmdStr, out.cmdId) || !parseU32(segStr, out.fileSegNo)) {
        errorDetail = "invalid numeric field";
        return false;
    }
    if (!base64Decode(b64, out.contentRaw)) {
        errorDetail = "invalid Content base64";
        return false;
    }
    if (contFlag != "0" && contFlag != "1") {
        errorDetail = "invalid Continue";
        return false;
    }
    out.continueFlag = (contFlag == "1");
    return true;
}

std::string JsonPushProtocolCodec::encodePushBriefConfirm(
    const ProtocolConfirmResponse& resp) {
    return encodeConfirmJson(resp, false);
}

std::string JsonPushProtocolCodec::encodePushContentConfirm(
    const ProtocolConfirmResponse& resp) {
    return encodeConfirmJson(resp, true);
}

}  // namespace transfer
