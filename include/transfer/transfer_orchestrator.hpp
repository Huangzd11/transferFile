// 召唤上传业务编排（V0.0.4）
// 处理召唤→简报→分段上传→平台内容确认；支持断点续传与超时中止。

#pragma once

#include "transfer/crc32.hpp"
#include "transfer/file_store.hpp"
#include "transfer/mqtt_publisher.hpp"
#include "transfer/protocol_codec.hpp"
#include "transfer/session_store.hpp"
#include "transfer/timeout_watchdog.hpp"
#include "transfer/types.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace transfer {

class TransferOrchestrator {
public:
    using BusyChecker = std::function<bool()>;

    TransferOrchestrator(IProtocolCodec& codec, IFileStore& files,
                         ISessionStore& sessions, ITimeoutWatchdog& watchdog,
                         IMqttPublisher& mqtt, ICrc32Calculator& crc,
                         TransferConfig config);

    void setBusyChecker(BusyChecker checker);
    void onSummon(std::string_view jsonUtf8);
    void onContentConfirm(std::string_view jsonUtf8);
    void onTimeout(uint32_t cmdId);

private:
    void publishBriefFailure(uint32_t cmdId, const std::string& errorCode,
                             const std::string& note);
    void sendNextContentSegment(uint32_t cmdId);
    void completeTransfer(uint32_t cmdId);
    void abortTransfer(uint32_t cmdId, const std::string& reason);

    IProtocolCodec& codec_;
    IFileStore& files_;
    ISessionStore& sessions_;
    ITimeoutWatchdog& watchdog_;
    IMqttPublisher& mqtt_;
    ICrc32Calculator& crc_;
    TransferConfig config_;
    BusyChecker busyChecker_;
    std::unordered_map<uint32_t, FileHandle> openReadHandles_;
};

}  // namespace transfer
