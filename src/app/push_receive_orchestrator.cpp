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
                                                 TransferConfig config)    // 构造函数
    : codec_(codec),    // 设置编码器
      files_(files),    // 设置文件存储
      sessions_(sessions),    // 设置会话存储
      watchdog_(watchdog),    // 设置看门狗
      mqtt_(mqtt),    // 设置MQTT发布器
      config_(std::move(config)) {}    // 构造函数

void PushReceiveOrchestrator::setBusyChecker(BusyChecker checker) {
    busyChecker_ = std::move(checker);    // 设置忙检查器
}

void PushReceiveOrchestrator::publishBriefConfirm(uint32_t cmdId, BriefStatus status,
                                                  const std::string& errorCode,
                                                  const std::string& note) {    // 发布简报确认
    ProtocolConfirmResponse resp;    // 设置协议确认响应
    resp.cmdId = cmdId;    // 设置CmdId
    resp.status = status;    // 设置状态
    resp.errorCode = errorCode;    // 设置错误码
    resp.note = note;    // 设置备注
    mqtt_.publishPushBriefConfirm(codec_.encodePushBriefConfirm(resp));    // 发布简报确认
    std::ostringstream os;
    os << ">>> 已发布推送简报确认 CmdId=" << cmdId
       << (status == BriefStatus::Success ? " 成功" : " 失败");    // 设置状态
    if (!errorCode.empty()) os << " ErrorCode=" << errorCode;    // 设置错误码
    log::gatewayInfo(os.str());    // 打印日志
}

void PushReceiveOrchestrator::publishContentConfirm(uint32_t cmdId, uint32_t fileSegNo,
                                                    BriefStatus status,
                                                    const std::string& errorCode,
                                                    const std::string& note) {    // 发布内容确认
    ProtocolConfirmResponse resp;    // 设置协议确认响应
    resp.cmdId = cmdId;    // 设置CmdId
    resp.fileSegNo = fileSegNo;    // 设置文件段序号
    resp.status = status;    // 设置状态
    resp.errorCode = errorCode;    // 设置错误码
    resp.note = note;    // 设置备注
    mqtt_.publishPushContentConfirm(codec_.encodePushContentConfirm(resp));    // 发布内容确认
    std::ostringstream os;    // 设置输出流
    os << ">>> 已发布推送内容确认 CmdId=" << cmdId << " SegNo=" << fileSegNo    // 设置CmdId和文件段序号
       << (status == BriefStatus::Success ? " 成功" : " 失败");    // 设置状态
    log::gatewayInfo(os.str());    // 打印日志
}

void PushReceiveOrchestrator::abortSession(PushSessionRecord& session,
                                           const std::string& reason) {    // 中止会话
    auto it = openWriteHandles_.find(session.cmdId);    // 查找文件句柄
    if (it != openWriteHandles_.end()) {
        files_.close(it->second);    // 关闭文件
        openWriteHandles_.erase(it);    // 删除文件句柄
    }
    session.state = PushSessionState::Aborted;    // 设置会话状态为中止
    sessions_.remove(session.cmdId);    // 删除会话
    watchdog_.disarm(session.cmdId);    // 停止看门狗
    log::gatewayWarn("推送接收中止 CmdId=" + std::to_string(session.cmdId) + " " + reason);    // 打印日志
}

void PushReceiveOrchestrator::onPushBrief(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到推送文件简报");    // 打印日志

    PushBriefRequest req;    // 设置推送简报请求
    std::string errDetail;    // 设置错误详情
    if (!codec_.decodePushBrief(jsonUtf8, req, errDetail)) {    // 解析推送简报
        log::gatewayError("推送简报解析失败: " + errDetail);
        publishBriefConfirm(0, BriefStatus::Failure, errc::BadFrame, errDetail);    // 发布简报确认
        return;
    }

    {
        std::ostringstream os;    // 设置输出流
        os << "推送简报 CmdId=" << req.cmdId << " 路径=" << req.fullPathFileName << " FileSize=" << req.fileSize << " FileCrc=" << req.fileCrc;    // 设置输出内容
        log::gatewayInfo(os.str());    // 打印日志
    }

    if ((busyChecker_ && busyChecker_()) || sessions_.hasActiveSession()) {    // 检查忙状态和活动会话
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::Busy, "gateway busy");    // 发布简报确认
        return;
    }

    if (files_.validatePath(req.fullPathFileName) != FileOpenError::Ok) {    // 验证路径
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::InvalidPath,
                            "path not allowed");    // 发布简报确认
        return;
    }

    if (req.fileSize == 0) {
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::InvalidStartByte,
                            "empty file not allowed");    // 发布简报确认
        return;
    }

    FileHandle handle;    // 设置文件句柄
    if (files_.openWriteCreate(req.fullPathFileName, handle) != FileOpenError::Ok) {
        publishBriefConfirm(req.cmdId, BriefStatus::Failure, errc::IoError,
                            "cannot create file");
        return; 
    }

    PushSessionRecord session;    // 设置会话记录   
    session.cmdId = req.cmdId;    // 设置CmdId
    session.fullPathFileName = req.fullPathFileName;    // 设置文件路径
    session.expectedFileCrc = req.fileCrc;    // 设置文件CRC
    session.expectedFileSize = req.fileSize;    // 设置文件大小
    session.bytesWritten = 0;    // 设置已写入字节数
    session.nextSegNo = 1;    // 设置下一个段序号
    session.state = PushSessionState::BriefOk;    // 设置会话状态为简报成功
    session.lastActivity = std::chrono::steady_clock::now();    // 设置最近活动时间
    sessions_.upsert(session);    // 更新会话记录
    openWriteHandles_.emplace(req.cmdId, std::move(handle));    // 插入文件句柄

    watchdog_.arm(req.cmdId, config_.timeoutSec);    // 设置看门狗
    publishBriefConfirm(req.cmdId, BriefStatus::Success, "", "");    // 发布简报确认
    log::gatewayInfo("推送简报确认成功，等待文件内容...");    // 打印日志
}

void PushReceiveOrchestrator::onPushContent(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到推送文件内容段");

    PushContentSegment seg;    // 设置推送内容段
    std::string errDetail;    // 设置错误详情
    if (!codec_.decodePushContent(jsonUtf8, seg, errDetail)) {    // 解析推送内容
        log::gatewayError("推送内容解析失败: " + errDetail);
        publishContentConfirm(0, 0, BriefStatus::Failure, errc::BadFrame, errDetail);    // 发布内容确认
        return;
    }

    auto rec = sessions_.getByCmdId(seg.cmdId);    // 获取会话记录
    if (!rec) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::NoSession, "no active push session");    // 发布内容确认
        return;
    }
    PushSessionRecord session = *rec;    // 获取会话记录

    if (session.state != PushSessionState::BriefOk &&    // 检查会话状态
        session.state != PushSessionState::ReceivingContent) {    // 检查会话状态
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::NoSession, "invalid session state");    // 发布内容确认
        return;
    }

    if (seg.fileSegNo != session.nextSegNo) {    // 检查段序号
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::InvalidSegNo, "unexpected FileSegNo");    // 发布内容确认
        abortSession(session, "段序号错误");
        return;
    }

    auto hit = openWriteHandles_.find(seg.cmdId);    // 查找文件句柄
    if (hit == openWriteHandles_.end()) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                              errc::IoError, "file handle missing");    // 发布内容确认
        abortSession(session, "文件句柄丢失");
        return;
    }

    size_t nWritten = 0;    // 设置已写入字节数
    if (files_.writeAt(hit->second, session.bytesWritten, seg.contentRaw.data(),
                       seg.contentRaw.size(), nWritten) != FileOpenError::Ok ||
        nWritten != seg.contentRaw.size()) {
        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure, errc::IoError,
                              "write failed");    // 发布内容确认
        abortSession(session, "写入失败");
        return;
    }

    session.bytesWritten += nWritten;    // 设置已写入字节数
    session.nextSegNo++;    // 设置下一个段序号
    session.state = PushSessionState::ReceivingContent;    // 设置会话状态为接收内容
    session.lastActivity = std::chrono::steady_clock::now();    // 设置最近活动时间
    sessions_.upsert(session);    // 更新会话记录
    watchdog_.reset(seg.cmdId);    // 重置看门狗

    if (!seg.continueFlag) {    // 检查是否为最后一帧
        if (session.bytesWritten != session.expectedFileSize) {
            publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                                  errc::IoError, "size mismatch");    // 发布内容确认
            abortSession(session, "大小不一致");
            return;
        }

        files_.close(hit->second);    // 关闭文件
        openWriteHandles_.erase(hit);    // 删除文件句柄

        Crc32Calculator crcCalc;
        uint32_t actualCrc = crcCalc.computeFile(session.fullPathFileName);    // 计算文件CRC
        uint32_t expectedCrc = 0;    // 设置期望CRC
        if (!parseFileCrcHex(session.expectedFileCrc, expectedCrc) ||
            actualCrc != expectedCrc) {
            publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Failure,
                                  errc::CrcMismatch, "crc mismatch");    // 发布内容确认
            abortSession(session, "CRC 校验失败");
            return;
        }

        publishContentConfirm(seg.cmdId, seg.fileSegNo, BriefStatus::Success, "", "");    // 发布内容确认
        sessions_.remove(session.cmdId);    // 删除会话记录
        watchdog_.disarm(session.cmdId);    // 停止看门狗
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
