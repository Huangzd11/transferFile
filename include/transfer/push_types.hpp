// 平台推送（V0.0.3）核心类型定义
// 含推送会话状态、简报/内容段及网关确认应答结构。

#pragma once

#include "transfer/types.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace transfer {

// 平台 → 网关：推送文件简报
struct PushBriefRequest {
    uint32_t cmdId = 0;
    std::string fullPathFileName;
    std::string fileCrc;
    uint64_t fileSize = 0;
    std::string modifyTime;
};

// 平台 → 网关：推送文件内容段
struct PushContentSegment {
    uint32_t cmdId = 0;
    uint32_t fileSegNo = 1;
    std::vector<uint8_t> contentRaw;
    bool continueFlag = true;
};

// 网关 → 平台：简报确认 / 内容确认（内容确认含 FileSegNo）
struct ProtocolConfirmResponse {
    uint32_t cmdId = 0;
    uint32_t fileSegNo = 0;  // 简报确认为 0；内容确认必填
    BriefStatus status = BriefStatus::Failure;
    std::string errorCode;
    std::string note;
};

enum class PushSessionState {
    Idle,
    BriefOk,
    ReceivingContent,
    Completed,
    Aborted
};

struct PushSessionRecord {
    uint32_t cmdId = 0;
    std::string fullPathFileName;
    std::string expectedFileCrc;
    uint64_t expectedFileSize = 0;
    uint64_t bytesWritten = 0;
    uint32_t nextSegNo = 1;
    PushSessionState state = PushSessionState::Idle;
    std::chrono::steady_clock::time_point lastActivity{};
};

}  // namespace transfer
