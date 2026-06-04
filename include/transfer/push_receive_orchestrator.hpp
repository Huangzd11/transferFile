// 平台推送接收编排（V0.0.3）
// 简报确认→按序写内容→逐段确认；末段 CRC 校验，不支持断点续传。

#pragma once

#include "transfer/push_protocol_codec.hpp"
#include "transfer/push_mqtt_responder.hpp"
#include "transfer/push_session_store.hpp"
#include "transfer/file_store.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/types.hpp"

#include <functional>
#include <string_view>
#include <unordered_map>

namespace transfer {

// V0.0.3：平台向网关推送文件（无断点续传）
class PushReceiveOrchestrator {
public:
    using BusyChecker = std::function<bool()>;

    PushReceiveOrchestrator(IPushProtocolCodec& codec, IFileStore& files,
                            IPushSessionStore& sessions, ITimeoutWatchdog& watchdog,
                            IPushMqttResponder& mqtt, TransferConfig config);

    void setBusyChecker(BusyChecker checker);

    void onPushBrief(std::string_view jsonUtf8);
    void onPushContent(std::string_view jsonUtf8);
    void onTimeout(uint32_t cmdId);

private:
    void publishBriefConfirm(uint32_t cmdId, BriefStatus status,
                             const std::string& errorCode, const std::string& note);
    void publishContentConfirm(uint32_t cmdId, uint32_t fileSegNo, BriefStatus status,
                               const std::string& errorCode, const std::string& note);
    void abortSession(PushSessionRecord& session, const std::string& reason);

    IPushProtocolCodec& codec_;
    IFileStore& files_;
    IPushSessionStore& sessions_;
    ITimeoutWatchdog& watchdog_;
    IPushMqttResponder& mqtt_;
    TransferConfig config_;
    BusyChecker busyChecker_;
    std::unordered_map<uint32_t, FileHandle> openWriteHandles_;
};

}  // namespace transfer
