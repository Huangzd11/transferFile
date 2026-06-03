#include "transfer/transfer_orchestrator.hpp"

#include "transfer/error_codes.hpp"
#include "transfer/runtime_log.hpp"

#include <sstream>
#include <vector>

namespace transfer {
namespace {

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
}

void TransferOrchestrator::sendContentFrom(SessionRecord& session, FileHandle& handle) {
    session.state = SessionState::SendingContent;
    sessions_.upsert(session);

    uint32_t segNo = 1;
    uint64_t offset = session.nextFileOffset;

    while (offset < session.fileSize) {
        uint64_t remaining = session.fileSize - offset;
        size_t toRead =
            static_cast<size_t>(std::min<uint64_t>(config_.chunkSize, remaining));

        std::vector<uint8_t> buf(toRead);
        size_t nRead = 0;
        if (files_.readAt(handle, offset, buf.data(), buf.size(), nRead) !=
                FileOpenError::Ok ||
            nRead == 0) {
            session.state = SessionState::Aborted;
            sessions_.remove(session.cmdId);
            watchdog_.disarm(session.cmdId);
            files_.close(handle);
            log::gatewayError("读取文件失败，传输中止 CmdId=" + std::to_string(session.cmdId));
            return;
        }
        buf.resize(nRead);
        offset += nRead;
        const size_t segBytes = nRead;

        ContentSegment seg;
        seg.cmdId = session.cmdId;
        seg.fileSegNo = segNo++;
        seg.contentRaw = std::move(buf);
        seg.continueFlag = (offset < session.fileSize);

        mqtt_.publishContent(codec_.encodeContent(seg));
        std::ostringstream os;
        os << ">>> 已发布内容段 CmdId=" << session.cmdId << " SegNo=" << seg.fileSegNo
           << " 字节=" << segBytes << " Continue=" << (seg.continueFlag ? "1" : "0");
        log::gatewayInfo(os.str());
    }

    log::gatewayInfo("传输完成 CmdId=" + std::to_string(session.cmdId) + " 总大小=" +
                     std::to_string(session.fileSize) + " 字节");
    session.state = SessionState::Completed;
    session.nextFileOffset = session.fileSize;
    sessions_.upsert(session);
    sessions_.remove(session.cmdId);
    watchdog_.disarm(session.cmdId);
    files_.close(handle);
}

void TransferOrchestrator::onSummon(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到召唤报文，长度 " + std::to_string(jsonUtf8.size()) + " 字节");

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

    if (sessions_.hasActiveSessionOtherThan(req.cmdId)) {
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
    mqtt_.publishBrief(codec_.encodeBrief(br));
    {
        std::ostringstream os;
        os << ">>> 已发布简报成功 CmdId=" << req.cmdId << " FileSize=" << fileSize
           << " FileCrc=" << crcHex << " ModifyTime=" << modifyTime;
        log::gatewayInfo(os.str());
    }

    SessionRecord session;
    session.cmdId = req.cmdId;
    session.fullPathFileName = req.fullPathFileName;
    session.fileSize = fileSize;
    session.nextFileOffset = req.fileOffset;
    session.nextSegNo = 1;
    session.fileCrc = crcHex;
    session.modifyTime = modifyTime;
    session.state = SessionState::BriefOk;
    session.lastActivity = std::chrono::steady_clock::now();
    sessions_.upsert(session);

    // StartByte == fileSize+1：无内容可发
    if (req.fileOffset >= fileSize) {
        log::gatewayInfo("无需发送内容（StartByte 已达文件末尾）CmdId=" +
                         std::to_string(req.cmdId));
        session.state = SessionState::Completed;
        sessions_.remove(session.cmdId);
        watchdog_.disarm(session.cmdId);
        files_.close(handle);
        return;
    }

    log::gatewayInfo("开始发送文件内容...");
    sendContentFrom(session, handle);
}

void TransferOrchestrator::onTimeout(uint32_t cmdId) {
    auto rec = sessions_.getByCmdId(cmdId);
    if (!rec) return;
    log::gatewayWarn("传输超时中止 CmdId=" + std::to_string(cmdId) +
                     "（超过配置 timeoutSec）");
    SessionRecord s = *rec;
    s.state = SessionState::Aborted;
    sessions_.remove(cmdId);
    watchdog_.disarm(cmdId);
}

}  // namespace transfer
