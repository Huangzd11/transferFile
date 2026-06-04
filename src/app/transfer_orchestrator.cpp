// 召唤上传业务编排实现（V0.0.4）
// 流程：召唤→校验→简报→按 chunkSize 发内容→等确认→续传或完成。

#include "transfer/transfer_orchestrator.hpp"

#include "transfer/error_codes.hpp"
#include "transfer/runtime_log.hpp"

#include <sstream>
#include <string_view>
#include <vector>

namespace transfer {
namespace {

// 日志用：过长 MQTT 载荷截断显示
std::string payloadForLog(std::string_view payload, size_t maxLen = 2048) {
    if (payload.size() <= maxLen) {
        return std::string(payload);
    }
    std::string s(payload.substr(0, maxLen));
    s += "...[截断, 总长 ";
    s += std::to_string(payload.size());
    s += " 字节]";
    return s;
}

std::string fileErrorToCode(FileOpenError err) {
    switch (err) {
        case FileOpenError::NotFound:
            return errc::FileNotFound;
        case FileOpenError::PermissionDenied:
            return errc::PermissionDenied;
        case FileOpenError::InvalidPath:
            return errc::InvalidPath;
        case FileOpenError::IoError:
            return errc::IoError;
        default:
            return errc::IoError;
    }
}

}  // namespace

TransferOrchestrator::TransferOrchestrator(IProtocolCodec& codec, IFileStore& files,
                                         ISessionStore& sessions,
                                         ITimeoutWatchdog& watchdog, IMqttPublisher& mqtt,
                                         ICrc32Calculator& crc, TransferConfig config)
    : codec_(codec),
      files_(files),
      sessions_(sessions),
      watchdog_(watchdog),
      mqtt_(mqtt),
      crc_(crc),
      config_(std::move(config)) {}

void TransferOrchestrator::setBusyChecker(BusyChecker checker) {
    busyChecker_ = std::move(checker);
}

void TransferOrchestrator::publishBriefFailure(uint32_t cmdId,
                                               const std::string& errorCode,
                                               const std::string& note) {
    BriefResponse br;
    br.cmdId = cmdId;
    br.status = BriefStatus::Failure;
    br.errorCode = errorCode;
    br.note = note;
    mqtt_.publishBrief(codec_.encodeBrief(br));
    std::ostringstream os;
    os << ">>> 已发布简报失败 CmdId=" << cmdId << " ErrorCode=" << errorCode;
    if (!note.empty()) os << " (" << note << ")";
    log::gatewayInfo(os.str());
    watchdog_.disarm(cmdId);
    sessions_.remove(cmdId);
    openReadHandles_.erase(cmdId);
}

void TransferOrchestrator::abortTransfer(uint32_t cmdId, const std::string& reason) {
    auto it = openReadHandles_.find(cmdId);
    if (it != openReadHandles_.end()) {
        files_.close(it->second);
        openReadHandles_.erase(it);
    }
    sessions_.remove(cmdId);
    watchdog_.disarm(cmdId);
    log::gatewayWarn("召唤上传中止 CmdId=" + std::to_string(cmdId) + " " + reason);
}

void TransferOrchestrator::completeTransfer(uint32_t cmdId) {
    auto it = openReadHandles_.find(cmdId);
    if (it != openReadHandles_.end()) {
        files_.close(it->second);
        openReadHandles_.erase(it);
    }
    auto rec = sessions_.getByCmdId(cmdId);
    if (rec) {
        log::gatewayInfo("传输完成 CmdId=" + std::to_string(cmdId) + " 总大小=" +
                         std::to_string(rec->fileSize) + " 字节");
    }
    sessions_.remove(cmdId);
    watchdog_.disarm(cmdId);
}

void TransferOrchestrator::sendNextContentSegment(uint32_t cmdId) {
    auto rec = sessions_.getByCmdId(cmdId);
    if (!rec) return;
    SessionRecord session = *rec;

    auto hit = openReadHandles_.find(cmdId);
    if (hit == openReadHandles_.end()) {
        abortTransfer(cmdId, "文件句柄丢失");
        return;
    }

    if (session.nextFileOffset >= session.fileSize) {
        completeTransfer(cmdId);
        return;
    }

    uint64_t remaining = session.fileSize - session.nextFileOffset;
    size_t toRead =
        static_cast<size_t>(std::min<uint64_t>(config_.chunkSize, remaining));

    std::vector<uint8_t> buf(toRead);
    size_t nRead = 0;
    if (files_.readAt(hit->second, session.nextFileOffset, buf.data(), buf.size(),
                      nRead) != FileOpenError::Ok ||
        nRead == 0) {
        abortTransfer(cmdId, "读取文件失败");
        return;
    }
    buf.resize(nRead);
    const size_t segBytes = nRead;
    session.nextFileOffset += nRead;

    ContentSegment seg;
    seg.cmdId = cmdId;
    seg.fileSegNo = session.nextSegNo;
    seg.contentRaw = std::move(buf);
    seg.continueFlag = (session.nextFileOffset < session.fileSize);

    const std::string contentJson = codec_.encodeContent(seg);
    mqtt_.publishContent(contentJson);

    session.awaitingConfirmSegNo = seg.fileSegNo;
    session.nextSegNo = seg.fileSegNo + 1;
    session.state = SessionState::WaitingContentConfirm;
    session.lastActivity = std::chrono::steady_clock::now();
    sessions_.upsert(session);
    watchdog_.reset(cmdId);

    std::ostringstream os;
    os << ">>> 已发布内容段 CmdId=" << cmdId << " SegNo=" << seg.fileSegNo
       << " 字节=" << segBytes << " Continue=" << (seg.continueFlag ? "1" : "0")
       << " 报文: " << payloadForLog(contentJson);
    log::gatewayInfo(os.str());
}

void TransferOrchestrator::onContentConfirm(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到文件内容确认，长度 " + std::to_string(jsonUtf8.size()) +
                     " 字节 报文: " + payloadForLog(jsonUtf8));

    ContentConfirmRequest req;
    std::string errDetail;
    if (!codec_.decodeContentConfirm(jsonUtf8, req, errDetail)) {
        log::gatewayError("内容确认解析失败: " + errDetail);
        return;
    }

    auto rec = sessions_.getByCmdId(req.cmdId);
    if (!rec) {
        log::gatewayWarn("内容确认无会话 CmdId=" + std::to_string(req.cmdId));
        return;
    }
    SessionRecord session = *rec;

    if (session.state != SessionState::WaitingContentConfirm ||
        session.awaitingConfirmSegNo == 0) {
        log::gatewayWarn("内容确认时会话状态无效 CmdId=" + std::to_string(req.cmdId));
        return;
    }

    if (req.fileSegNo != session.awaitingConfirmSegNo) {
        abortTransfer(req.cmdId, "段序号与待确认不一致");
        return;
    }

    if (req.status != BriefStatus::Success) {
        std::ostringstream os;
        os << "平台内容确认失败 SegNo=" << req.fileSegNo;
        if (!req.errorCode.empty()) os << " ErrorCode=" << req.errorCode;
        abortTransfer(req.cmdId, os.str());
        return;
    }

    session.awaitingConfirmSegNo = 0;
    session.state = SessionState::SendingContent;
    sessions_.upsert(session);
    watchdog_.reset(req.cmdId);

    log::gatewayInfo("内容确认成功 CmdId=" + std::to_string(req.cmdId) + " SegNo=" +
                     std::to_string(req.fileSegNo));

    if (session.nextFileOffset >= session.fileSize) {
        completeTransfer(req.cmdId);
        return;
    }
    sendNextContentSegment(req.cmdId);
}

void TransferOrchestrator::onSummon(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到召唤报文，长度 " + std::to_string(jsonUtf8.size()) +
                     " 字节 报文: " + payloadForLog(jsonUtf8));

    SummonRequest req;
    std::string errDetail;
    if (!codec_.decodeSummon(jsonUtf8, req, errDetail)) {
        log::gatewayError("召唤解析失败: " + errDetail);
        publishBriefFailure(0, errc::BadFrame, errDetail);
        return;
    }

    {
        std::ostringstream os;
        os << "解析召唤 CmdId=" << req.cmdId << " 文件=" << req.fullPathFileName
           << " StartByte=" << req.startByte;
        if (req.startByte > 1) {
            os << "（断点续传，0-based 偏移 " << req.fileOffset << "）";
        }
        log::gatewayInfo(os.str());
    }

    watchdog_.reset(req.cmdId);
    watchdog_.arm(req.cmdId, config_.timeoutSec);

    if ((busyChecker_ && busyChecker_()) ||
        sessions_.hasActiveSessionOtherThan(req.cmdId)) {
        publishBriefFailure(req.cmdId, errc::Busy, "gateway busy");
        return;
    }

    if (files_.validatePath(req.fullPathFileName) != FileOpenError::Ok) {
        publishBriefFailure(req.cmdId, errc::InvalidPath, "path not allowed");
        return;
    }

    FileHandle handle;
    FileOpenError openErr = files_.openReadOnly(req.fullPathFileName, handle);
    if (openErr != FileOpenError::Ok) {
        publishBriefFailure(req.cmdId, fileErrorToCode(openErr), "cannot open file");
        return;
    }

    uint64_t fileSize = 0;
    if (files_.getSize(handle, fileSize) != FileOpenError::Ok) {
        files_.close(handle);
        publishBriefFailure(req.cmdId, errc::IoError, "get size failed");
        return;
    }

    if (req.fileOffset > fileSize) {
        files_.close(handle);
        publishBriefFailure(req.cmdId, errc::InvalidStartByte, "start beyond file end");
        return;
    }

    std::string modifyTime;
    if (files_.getModifyTime(handle, modifyTime) != FileOpenError::Ok) {
        files_.close(handle);
        publishBriefFailure(req.cmdId, errc::IoError, "mtime failed");
        return;
    }

    uint32_t crcVal = crc_.computeFile(req.fullPathFileName);
    std::string crcHex = crc_.toHexString(crcVal);

    BriefResponse br;
    br.cmdId = req.cmdId;
    br.status = BriefStatus::Success;
    br.fileSize = fileSize;
    br.fileCrc = crcHex;
    br.modifyTime = modifyTime;
    const std::string briefJson = codec_.encodeBrief(br);
    mqtt_.publishBrief(briefJson);
    {
        std::ostringstream os;
        os << ">>> 已发布简报成功 CmdId=" << req.cmdId << " FileSize=" << fileSize
           << " FileCrc=" << crcHex << " ModifyTime=" << modifyTime
           << " 报文: " << briefJson;
        log::gatewayInfo(os.str());
    }

    SessionRecord session;
    session.cmdId = req.cmdId;
    session.fullPathFileName = req.fullPathFileName;
    session.fileSize = fileSize;
    session.nextFileOffset = req.fileOffset;
    session.nextSegNo = 1;
    session.awaitingConfirmSegNo = 0;
    session.fileCrc = crcHex;
    session.modifyTime = modifyTime;
    session.state = SessionState::BriefOk;
    session.lastActivity = std::chrono::steady_clock::now();
    sessions_.upsert(session);
    openReadHandles_.emplace(req.cmdId, std::move(handle));

    if (req.fileOffset >= fileSize) {
        log::gatewayInfo("无需发送内容（StartByte 已达文件末尾）CmdId=" +
                         std::to_string(req.cmdId));
        completeTransfer(req.cmdId);
        return;
    }

    log::gatewayInfo("开始发送文件内容（等待平台逐段确认）...");
    sendNextContentSegment(req.cmdId);
}

void TransferOrchestrator::onTimeout(uint32_t cmdId) {
    auto rec = sessions_.getByCmdId(cmdId);
    if (!rec) return;
    abortTransfer(cmdId, "超时未收到平台内容确认或新召唤");
}

}  // namespace transfer
