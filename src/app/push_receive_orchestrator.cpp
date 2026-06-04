// 平台推送接收编排实现（V0.0.3）
// 简报确认后按序写盘；末段校验大小与 CRC 后结束会话。

#include "transfer/push_receive_orchestrator.hpp"

#include "transfer/crc32.hpp"
#include "transfer/error_codes.hpp"
#include "transfer/runtime_log.hpp"

#include <sstream>

namespace transfer {

PushReceiveOrchestrator::PushReceiveOrchestrator(IPushProtocolCodec& codec,
                                                 IFileStore& files,
                                                 IPushSessionStore& sessions,
                                                 ITimeoutWatchdog& watchdog,
                                                 IPushMqttResponder& mqtt,
                                                 TransferConfig config)
    : codec_(codec),
      files_(files),
      sessions_(sessions),
      watchdog_(watchdog),
      mqtt_(mqtt),
      config_(std::move(config)) {}

void PushReceiveOrchestrator::setBusyChecker(BusyChecker checker) {
    busyChecker_ = std::move(checker);
}

void PushReceiveOrchestrator::publishBriefConfirm(uint32_t cmdId, BriefStatus status,
                                                  const std::string& errorCode,
                                                  const std::string& note) {
    ProtocolConfirmResponse resp;
    resp.cmdId = cmdId;
    resp.status = status;
    resp.errorCode = errorCode;
    resp.note = note;
    mqtt_.publishPushBriefConfirm(codec_.encodePushBriefConfirm(resp));
    std::ostringstream os;
    os << ">>> 已发布推送简报确认 CmdId=" << cmdId
       << (status == BriefStatus::Success ? " 成功" : " 失败");
    if (!errorCode.empty()) os << " ErrorCode=" << errorCode;
    log::gatewayInfo(os.str());
}

void PushReceiveOrchestrator::publishContentConfirm(uint32_t cmdId, uint32_t fileSegNo,
                                                    BriefStatus status,
                                                    const std::string& errorCode,
                                                    const std::string& note) {
    ProtocolConfirmResponse resp;
    resp.cmdId = cmdId;
    resp.fileSegNo = fileSegNo;
    resp.status = status;
    resp.errorCode = errorCode;
    resp.note = note;
    mqtt_.publishPushContentConfirm(codec_.encodePushContentConfirm(resp));
    std::ostringstream os;
    os << ">>> 已发布推送内容确认 CmdId=" << cmdId << " SegNo=" << fileSegNo
       << (status == BriefStatus::Success ? " 成功" : " 失败");
    log::gatewayInfo(os.str());
}

void PushReceiveOrchestrator::abortSession(PushSessionRecord& session,
                                           const std::string& reason) {
    auto it = openWriteHandles_.find(session.cmdId);
    if (it != openWriteHandles_.end()) {
        files_.close(it->second);
        openWriteHandles_.erase(it);
    }
    session.state = PushSessionState::Aborted;
    sessions_.remove(session.cmdId);
    watchdog_.disarm(session.cmdId);
    log::gatewayWarn("推送接收中止 CmdId=" + std::to_string(session.cmdId) + " " + reason);
}

void PushReceiveOrchestrator::onPushBrief(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到推送文件简报");

    PushBriefRequest req;
    std::string errDetail;
    if (!codec_.decodePushBrief(jsonUtf8, req, errDetail)) {
        log::gatewayError("推送简报解析失败: " + errDetail);
        publishBriefConfirm(0, BriefStatus::Failure, errc::BadFrame, errDetail);
        return;
    }

    {
        std::ostringstream os;
        os << "推送简报 CmdId=" << req.cmdId << " 路径=" << req.fullPathFileName
           << " FileSize=" << req.fileSize << " FileCrc=" << req.fileCrc;
        log::gatewayInfo(os.str());
    }

    if ((busyChecker_ && busyChecker_()) || sessions_.hasActiveSession()) {
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::Busy, "gateway busy");
        return;
    }

    if (files_.validatePath(req.fullPathFileName) != FileOpenError::Ok) {
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::InvalidPath,
                            "path not allowed");
        return;
    }

    if (req.fileSize == 0) {
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::InvalidStartByte,
                            "empty file not allowed");
        return;
    }

    FileHandle handle;
    if (files_.openWriteCreate(req.fullPathFileName, handle) != FileOpenError::Ok) {
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::IoError,
                            "cannot create file");
        return;
    }

    PushSessionRecord session;
    session.cmdId = req.cmdId;
    session.fullPathFileName = req.fullPathFileName;
    session.expectedFileCrc = req.fileCrc;
    session.expectedFileSize = req.fileSize;
    session.bytesWritten = 0;
    session.nextSegNo = 1;
    session.state = PushSessionState::BriefOk;
    session.lastActivity = std::chrono::steady_clock::now();
    sessions_.upsert(session);
    openWriteHandles_.emplace(req.cmdId, std::move(handle));

    watchdog_.arm(req.cmdId, config_.timeoutSec);
    publishBriefConfirm(req.cmdId, BriefStatus::Success, "", "");
    log::gatewayInfo("推送简报确认成功，等待文件内容...");
}

void PushReceiveOrchestrator::onPushContent(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到推送文件内容段");

    PushContentSegment seg;
    std::string errDetail;
    if (!codec_.decodePushContent(jsonUtf8, seg, errDetail)) {
        log::gatewayError("推送内容解析失败: " + errDetail);
        publishContentConfirm(0, 0, BriefStatus::Failure, errc::BadFrame, errDetail);
        return;
    }

    auto rec = sessions_.getByCmdId(seg.cmdId);
    if (!rec) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::NoSession, "no active push session");
        return;
    }
    PushSessionRecord session = *rec;

    if (session.state != PushSessionState::BriefOk &&
        session.state != PushSessionState::ReceivingContent) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::NoSession, "invalid session state");
        return;
    }

    if (seg.fileSegNo != session.nextSegNo) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::InvalidSegNo, "unexpected FileSegNo");
        abortSession(session, "段序号错误");
        return;
    }

    auto hit = openWriteHandles_.find(seg.cmdId);
    if (hit == openWriteHandles_.end()) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::IoError, "file handle missing");
        abortSession(session, "文件句柄丢失");
        return;
    }

    size_t nWritten = 0;
    if (files_.writeAt(hit->second, session.bytesWritten, seg.contentRaw.data(),
                       seg.contentRaw.size(), nWritten) != FileOpenError::Ok ||
        nWritten != seg.contentRaw.size()) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure, errc::IoError,
                              "write failed");
        abortSession(session, "写入失败");
        return;
    }

    session.bytesWritten += nWritten;
    session.nextSegNo++;
    session.state = PushSessionState::ReceivingContent;
    session.lastActivity = std::chrono::steady_clock::now();
    sessions_.upsert(session);
    watchdog_.reset(seg.cmdId);

    if (!seg.continueFlag) {
        if (session.bytesWritten != session.expectedFileSize) {
            publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                                  errc::IoError, "size mismatch");
            abortSession(session, "大小不一致");
            return;
        }

        files_.close(hit->second);
        openWriteHandles_.erase(hit);

        Crc32Calculator crcCalc;
        uint32_t actualCrc = crcCalc.computeFile(session.fullPathFileName);
        uint32_t expectedCrc = 0;
        if (!parseFileCrcHex(session.expectedFileCrc, expectedCrc) ||
            actualCrc != expectedCrc) {
            publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                                  errc::CrcMismatch, "crc mismatch");
            abortSession(session, "CRC 校验失败");
            return;
        }

        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Success, "", "");
        sessions_.remove(session.cmdId);
        watchdog_.disarm(session.cmdId);
        log::gatewayInfo("推送文件接收完成 CmdId=" + std::to_string(seg.cmdId) + " 路径=" +
                         session.fullPathFileName + " 大小=" +
                         std::to_string(session.bytesWritten));
        return;
    }

    publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Success, "", "");
}

void PushReceiveOrchestrator::onTimeout(uint32_t cmdId) {
    auto rec = sessions_.getByCmdId(cmdId);
    if (!rec) return;
    PushSessionRecord session = *rec;
    abortSession(session, "超时未收到后续推送帧");
}

}  // namespace transfer
