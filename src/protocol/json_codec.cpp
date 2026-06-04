// 召唤上传协议 JSON 编解码实现
// 轻量字符串解析（非完整 JSON 库）；Content 经 Base64 + jsonEscape。

#include "transfer/protocol_codec.hpp"

#include "transfer/base64.hpp"

#include <sstream>

namespace transfer {
namespace {

// 对 JSON 字符串值中的特殊字符转义
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

// 从 JSON 文本中提取 "key":"..." 字符串字段（不支持嵌套对象内深层路径）
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

bool parseU64(const std::string& s, uint64_t& out) {    // 解析无符号64位整数
    if (s.empty()) return false;    // 如果为空，则返回false
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;    // 如果不在0-9之间，则返回false
        uint64_t next = v * 10 + static_cast<uint64_t>(c - '0');    // 转换为整数
        if (next < v) return false;    // 如果溢出，则返回false
        v = next;    // 设置输出
    }
    out = v;    // 设置输出
    return true;    // 返回true
}

bool parseU32(const std::string& s, uint32_t& out) {    // 解析无符号32位整数
    uint64_t v = 0;
    if (!parseU64(s, v) || v > 0xFFFFFFFFULL) return false;    // 如果不在0-FFFFFFFF之间，则返回false
    out = static_cast<uint32_t>(v);    // 设置输出
    return true;    // 返回true
}

}  // namespace

bool JsonProtocolCodec::decodeSummon(std::string_view jsonUtf8, SummonRequest& out,
                                     std::string& errorDetail) {    // 解码召唤
    out = SummonRequest{};    // 设置输出
    if (jsonUtf8.find("\"Data\"") == std::string_view::npos) {
        errorDetail = "missing Data";    // 缺少Data
        return false;
    }

    std::string cmdStr, path, startStr;
    if (!extractJsonStringField(jsonUtf8, "CmdId", cmdStr) ||
        !extractJsonStringField(jsonUtf8, "FullPathFileName", path) ||
        !extractJsonStringField(jsonUtf8, "StartByte", startStr)) {
        errorDetail = "missing required field";    // 缺少必填字段
        return false;
    }

    if (!parseU32(cmdStr, out.cmdId)) {
        errorDetail = "invalid CmdId";    // 无效CmdId
        return false;
    }
    if (!parseU64(startStr, out.startByte) || out.startByte < 1) {
        errorDetail = "invalid StartByte";    // 无效StartByte
        return false;
    }

    out.fullPathFileName = std::move(path);    // 设置文件路径
    out.fileOffset = out.startByte - 1;    // 设置文件偏移
    out.rawValid = true;    // 设置rawValid
    return true;    // 返回true
}

std::string JsonProtocolCodec::encodeBrief(const BriefResponse& brief) {    // 编码简报 
    std::ostringstream os;    // 输出流
    os << "{\"Data\":{";
    os << "\"CmdId\":\"" << brief.cmdId << "\",";    // 设置CmdId
    os << "\"Status\":\""
       << (brief.status == BriefStatus::Success ? "0" : "1") << "\",";    // 设置Status
    os << "\"ErrorCode\":\"" << jsonEscape(brief.errorCode) << "\",";    // 设置ErrorCode
    os << "\"Note\":\"" << jsonEscape(brief.note) << "\",";    // 设置Note
    os << "\"FileCrc\":\"" << jsonEscape(brief.fileCrc) << "\",";
    os << "\"FileSize\":\"" << brief.fileSize << "\",";    // 设置FileSize
    os << "\"ModifyTime\":\"" << jsonEscape(brief.modifyTime) << "\"";    // 设置ModifyTime
    os << "}}";
    return os.str();    // 返回字符串
}

std::string JsonProtocolCodec::encodeContent(const ContentSegment& seg) {    // 编码内容
    std::ostringstream os;
    os << "{\"Data\":{";
    os << "\"CmdId\":\"" << seg.cmdId << "\",";    // 设置CmdId
    os << "\"FileSegNo\":\"" << seg.fileSegNo << "\",";
    os << "\"Content\":\"" << jsonEscape(base64Encode(seg.contentRaw)) << "\",";    // 设置Content
    os << "\"Continue\":\"" << (seg.continueFlag ? "1" : "0") << "\"";    // 设置Continue           
    os << "}}";
    return os.str();
}

bool JsonProtocolCodec::decodeContentConfirm(std::string_view jsonUtf8,
                                             ContentConfirmRequest& out,
                                             std::string& errorDetail) {    // 解码内容确认
    out = ContentConfirmRequest{};    // 设置输出
    if (jsonUtf8.find("\"Data\"") == std::string_view::npos) {
        errorDetail = "missing Data";    // 缺少Data
        return false;
    }
    std::string cmdStr, segStr, statusStr, errorCode, note;    // 字符串
    if (!extractJsonStringField(jsonUtf8, "CmdId", cmdStr) ||
        !extractJsonStringField(jsonUtf8, "FileSegNo", segStr) ||
        !extractJsonStringField(jsonUtf8, "Status", statusStr)) {
        errorDetail = "missing required field";    // 缺少必填字段
        return false;
    }
    if (!parseU32(cmdStr, out.cmdId) || !parseU32(segStr, out.fileSegNo)) {
        errorDetail = "invalid numeric field";    // 无效数字字段
        return false;
    }
    if (statusStr == "0") {
        out.status = BriefStatus::Success;
    } else if (statusStr == "1") {
        out.status = BriefStatus::Failure;
    } else {
        errorDetail = "invalid Status";    // 无效Status
        return false;
    }
    extractJsonStringField(jsonUtf8, "ErrorCode", errorCode);    // 设置ErrorCode
    extractJsonStringField(jsonUtf8, "Note", note);
    out.errorCode = std::move(errorCode);    // 设置ErrorCode
    out.note = std::move(note);    // 设置Note
    return true;    // 返回true
}

std::string JsonProtocolCodec::encodeContentConfirm(const ContentConfirmRequest& req) {    // 编码内容确认
    std::ostringstream os;
    os << "{\"Data\":{";
    os << "\"CmdId\":\"" << req.cmdId << "\",";    // 设置CmdId
    os << "\"FileSegNo\":\"" << req.fileSegNo << "\",";    // 设置FileSegNo
    os << "\"Status\":\"" << (req.status == BriefStatus::Success ? "0" : "1") << "\",";    // 设置Status
    os << "\"ErrorCode\":\"" << jsonEscape(req.errorCode) << "\",";    // 设置ErrorCode
    os << "\"Note\":\"" << jsonEscape(req.note) << "\"";    // 设置Note
    os << "}}";
    return os.str();    // 返回字符串   
}

}  // namespace transfer
