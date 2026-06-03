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
    void onTimeout(uint32_t cmdId);

private:
    void publishBriefFailure(uint32_t cmdId, const std::string& errorCode,
                             const std::string& note);
    void sendContentFrom(SessionRecord& session, FileHandle& handle);

    IProtocolCodec& codec_;
    IFileStore& files_;
    ISessionStore& sessions_;
    ITimeoutWatchdog& watchdog_;
    IMqttPublisher& mqtt_;
    ICrc32Calculator& crc_;
    TransferConfig config_;
    BusyChecker busyChecker_;
};

}  // namespace transfer
