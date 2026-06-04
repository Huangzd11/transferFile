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
std::string payloadForLog(std::string_view payload, size_t maxLen = 2048) {    // 截断显示
    if (payload.size() <= maxLen) {
        return std::string(payload);    // 返回原始字符串
    }
    std::string s(payload.substr(0, maxLen));    // 截断字符串
    s += "...[截断, 总长 ";    // 添加截断提示
    s += std::to_string(payload.size());    // 添加总长度
    s += " 字节]";    // 添加字节提示
    return s;    // 返回截断后的字符串
}

std::string fileErrorToCode(FileOpenError err) {
    switch (err) {    // 根据错误类型返回错误码
        case FileOpenError::NotFound:
            return errc::FileNotFound;    // 文件不存在
        case FileOpenError::PermissionDenied:
            return errc::PermissionDenied;    // 权限不足
        case FileOpenError::InvalidPath:
            return errc::InvalidPath;    // 无效路径
        case FileOpenError::IoError:
            return errc::IoError;    // IO错误
        default:
            return errc::IoError;    // 默认IO错误
    }
}    // 返回错误码

}  // namespace

TransferOrchestrator::TransferOrchestrator(IProtocolCodec& codec, IFileStore& files,    
                                         ISessionStore& sessions,
                                         ITimeoutWatchdog& watchdog, IMqttPublisher& mqtt,
                                         ICrc32Calculator& crc, TransferConfig config)    // 构造函数
    : codec_(codec),    // 编解码器
      files_(files),    // 文件存储
      sessions_(sessions),    // 会话存储
      watchdog_(watchdog),    // 看门狗
      mqtt_(mqtt),    // MQTT发布器
      crc_(crc),    // CRC计算器
      config_(std::move(config)) {}    // 配置

void TransferOrchestrator::setBusyChecker(BusyChecker checker) {
    busyChecker_ = std::move(checker);    // 设置繁忙检查器
}

void TransferOrchestrator::publishBriefFailure(uint32_t cmdId,
                                               const std::string& errorCode,
                                               const std::string& note) {    // 发布简报失败
    BriefResponse br;    // 简报响应
    br.cmdId = cmdId;    // 命令ID
    br.status = BriefStatus::Failure;    // 状态失败
    br.errorCode = errorCode;    // 错误码
    br.note = note;    // 备注
    mqtt_.publishBrief(codec_.encodeBrief(br));    // 发布简报
    std::ostringstream os;    // 输出流
    os << ">>> 已发布简报失败 CmdId=" << cmdId << " ErrorCode=" << errorCode;    // 添加错误码
    if (!note.empty()) os << " (" << note << ")";    // 添加备注
    log::gatewayInfo(os.str());    // 记录日志
    watchdog_.disarm(cmdId);    // 停止看门狗
    sessions_.remove(cmdId);    // 删除会话
    openReadHandles_.erase(cmdId);    // 删除文件句柄
}

void TransferOrchestrator::abortTransfer(uint32_t cmdId, const std::string& reason) {    // 中止传输
    auto it = openReadHandles_.find(cmdId);
    if (it != openReadHandles_.end()) {    // 如果文件句柄存在
        files_.close(it->second);    // 关闭文件
        openReadHandles_.erase(it);    // 删除文件句柄
    }
    sessions_.remove(cmdId);    // 删除会话
    watchdog_.disarm(cmdId);    // 停止看门狗
    log::gatewayWarn("召唤上传中止 CmdId=" + std::to_string(cmdId) + " " + reason);    // 记录日志
}

void TransferOrchestrator::completeTransfer(uint32_t cmdId) {
    auto it = openReadHandles_.find(cmdId);    // 查找文件句柄
    if (it != openReadHandles_.end()) {    // 如果文件句柄存在
        files_.close(it->second);    // 关闭文件
        openReadHandles_.erase(it);    // 删除文件句柄
    }
    auto rec = sessions_.getByCmdId(cmdId);    // 查找会话
    if (rec) {    // 如果会话存在
        log::gatewayInfo("传输完成 CmdId=" + std::to_string(cmdId) + " 总大小=" +
                         std::to_string(rec->fileSize) + " 字节");    // 记录日志
    }
    sessions_.remove(cmdId);    // 删除会话
    watchdog_.disarm(cmdId);    // 停止看门狗
}

void TransferOrchestrator::sendNextContentSegment(uint32_t cmdId) {    // 发送下一个内容段
    auto rec = sessions_.getByCmdId(cmdId);    // 查找会话
    if (!rec) return;    // 如果会话不存在，则返回
    SessionRecord session = *rec;    // 会话记录

    auto hit = openReadHandles_.find(cmdId);    // 查找文件句柄
    if (hit == openReadHandles_.end()) {    // 如果文件句柄不存在
        abortTransfer(cmdId, "文件句柄丢失");    // 中止传输
        return;    // 返回
    }

    if (session.nextFileOffset >= session.fileSize) {    // 如果下一个文件偏移大于等于文件大小
        completeTransfer(cmdId);    // 完成传输
        return;
    }

    uint64_t remaining = session.fileSize - session.nextFileOffset;    // 剩余大小
    size_t toRead = static_cast<size_t>(std::min<uint64_t>(config_.chunkSize, remaining));    // 读取大小

    std::vector<uint8_t> buf(toRead);    // 缓冲区
    size_t nRead = 0;    // 读取大小
    if (files_.readAt(hit->second, session.nextFileOffset, buf.data(), buf.size(), nRead) != FileOpenError::Ok || nRead == 0) {    // 读取文件
        abortTransfer(cmdId, "读取文件失败");    // 中止传输
        return;    // 返回
    }
    buf.resize(nRead);    // 缓冲区大小
    const size_t segBytes = nRead;    // 段大小
    session.nextFileOffset += nRead;    // 下一个文件偏移

    ContentSegment seg;    // 内容段
    seg.cmdId = cmdId;    // 命令ID
    seg.fileSegNo = session.nextSegNo;    // 文件段序号
    seg.contentRaw = std::move(buf);    // 内容
    seg.continueFlag = (session.nextFileOffset < session.fileSize);    // 是否继续

    const std::string contentJson = codec_.encodeContent(seg);    // 编码内容
    mqtt_.publishContent(contentJson);    // 发布内容

    session.awaitingConfirmSegNo = seg.fileSegNo;    // 等待确认段序号
    session.nextSegNo = seg.fileSegNo + 1;    // 下一个段序号
    session.state = SessionState::WaitingContentConfirm;    // 状态等待内容确认
    session.lastActivity = std::chrono::steady_clock::now();    // 最近活动时间
    sessions_.upsert(session);    // 更新会话
    watchdog_.reset(cmdId);    // 重置看门狗

    std::ostringstream os;    // 输出流
    os << ">>> 已发布内容段 CmdId=" << cmdId << " SegNo=" << seg.fileSegNo    // 添加命令ID和段序号
       << " 字节=" << segBytes << " Continue=" << (seg.continueFlag ? "1" : "0")    // 添加继续标志
       << " 报文: " << payloadForLog(contentJson);    // 添加报文
    log::gatewayInfo(os.str());    // 记录日志
}

void TransferOrchestrator::onContentConfirm(std::string_view jsonUtf8) {    // 收到内容确认
    log::gatewayInfo("<<< 收到文件内容确认，长度 " + std::to_string(jsonUtf8.size()) +    // 添加长度
                     " 字节 报文: " + payloadForLog(jsonUtf8));    // 添加报文

    ContentConfirmRequest req;    // 内容确认请求
    std::string errDetail;    // 错误详情
    if (!codec_.decodeContentConfirm(jsonUtf8, req, errDetail)) {    // 解码内容确认
        log::gatewayError("内容确认解析失败: " + errDetail);    // 记录日志
        return;    // 返回
    }

    auto rec = sessions_.getByCmdId(req.cmdId);
    if (!rec) {
        log::gatewayWarn("内容确认无会话 CmdId=" + std::to_string(req.cmdId));
        return;
    }
    SessionRecord session = *rec;    // 会话记录

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
    session.state = SessionState::SendingContent;    // 状态发送内容    
    sessions_.upsert(session);    // 更新会话
    watchdog_.reset(req.cmdId);    // 重置看门狗

    log::gatewayInfo("内容确认成功 CmdId=" + std::to_string(req.cmdId) + " SegNo=" +    // 添加命令ID和段序号
                     std::to_string(req.fileSegNo));    // 添加段序号

    if (session.nextFileOffset >= session.fileSize) {
        completeTransfer(req.cmdId);    // 完成传输
        return;
    }
    sendNextContentSegment(req.cmdId);    // 发送下一个内容段   
}

void TransferOrchestrator::onSummon(std::string_view jsonUtf8) {
    log::gatewayInfo("<<< 收到召唤报文，长度 " + std::to_string(jsonUtf8.size()) +    // 添加长度
                     " 字节 报文: " + payloadForLog(jsonUtf8));

    SummonRequest req;    // 召唤请求
    std::string errDetail;    // 错误详情
    if (!codec_.decodeSummon(jsonUtf8, req, errDetail)) {    // 解码召唤
        log::gatewayError("召唤解析失败: " + errDetail);    // 记录日志
        publishBriefFailure(0, errc::BadFrame, errDetail);
        return;
    }

    {
        std::ostringstream os;
        os << "解析召唤 CmdId=" << req.cmdId << " 文件=" << req.fullPathFileName
           << " StartByte=" << req.startByte;
        if (req.startByte > 1) {
            os << "（断点续传，0-based 偏移 " << req.fileOffset << "）";    // 添加断点续传提示
        }
        log::gatewayInfo(os.str());
    }

    watchdog_.reset(req.cmdId);    // 重置看门狗
    watchdog_.arm(req.cmdId, config_.timeoutSec);    // 设置看门狗

    if ((busyChecker_ && busyChecker_()) ||
        sessions_.hasActiveSessionOtherThan(req.cmdId)) {
        publishBriefFailure(req.cmdId, errc::Busy, "gateway busy");    // 发布简报失败
        return;
    }

    if (files_.validatePath(req.fullPathFileName) != FileOpenError::Ok) {
        publishBriefFailure(req.cmdId, errc::InvalidPath, "path not allowed");    // 发布简报失败
        return;
    }

    FileHandle handle;
    FileOpenError openErr = files_.openReadOnly(req.fullPathFileName, handle);
    if (openErr != FileOpenError::Ok) {
        publishBriefFailure(req.cmdId, fileErrorToCode(openErr), "cannot open file");    // 发布简报失败
        return;
    }

    uint64_t fileSize = 0;
    if (files_.getSize(handle, fileSize) != FileOpenError::Ok) {
        files_.close(handle);
        publishBriefFailure(req.cmdId, errc::IoError, "get size failed");    // 发布简报失败
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

    BriefResponse br;    // 简报响应
    br.cmdId = req.cmdId;    // 命令ID
    br.status = BriefStatus::Success;    // 状态成功
    br.fileSize = fileSize;    // 文件大小
    br.fileCrc = crcHex;    // 文件CRC
    br.modifyTime = modifyTime;    // 修改时间
    const std::string briefJson = codec_.encodeBrief(br);    // 编码简报
    mqtt_.publishBrief(briefJson);    // 发布简报
    {
        std::ostringstream os;
        os << ">>> 已发布简报成功 CmdId=" << req.cmdId << " FileSize=" << fileSize    // 添加文件大小
           << " FileCrc=" << crcHex << " ModifyTime=" << modifyTime    // 添加修改时间
           << " 报文: " << briefJson;    // 添加报文
        log::gatewayInfo(os.str());
    }

    SessionRecord session;    // 会话记录
    session.cmdId = req.cmdId;  
    session.fullPathFileName = req.fullPathFileName;    // 文件路径
    session.fileSize = fileSize;    // 文件大小
    session.nextFileOffset = req.fileOffset;    // 下一个文件偏移
    session.nextSegNo = 1;    // 下一个段序号
    session.awaitingConfirmSegNo = 0;    // 等待确认段序号
    session.fileCrc = crcHex;    // 文件CRC
    session.modifyTime = modifyTime;    // 修改时间
    session.state = SessionState::BriefOk;    // 状态简报成功
    session.lastActivity = std::chrono::steady_clock::now();    // 最近活动时间
    sessions_.upsert(session);    // 更新会话
    openReadHandles_.emplace(req.cmdId, std::move(handle));    // 添加文件句柄

    if (req.fileOffset >= fileSize) {
        log::gatewayInfo("无需发送内容（StartByte 已达文件末尾）CmdId=" +
                         std::to_string(req.cmdId));
        completeTransfer(req.cmdId);
        return;
    }

    log::gatewayInfo("开始发送文件内容（等待平台逐段确认）...");
    sendNextContentSegment(req.cmdId);    // 发送下一个内容段
}

void TransferOrchestrator::onTimeout(uint32_t cmdId) {    // 超时
    auto rec = sessions_.getByCmdId(cmdId);    // 查找会话
    if (!rec) return;    // 如果会话不存在，则返回
    abortTransfer(cmdId, "超时未收到平台内容确认或新召唤");    // 中止传输
}

}  // namespace transfer
