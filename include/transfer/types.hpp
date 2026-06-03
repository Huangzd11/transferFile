#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace transfer {

// 简报状态：对应协议 Status 字段
enum class BriefStatus { Success = 0, Failure = 1 };

// 会话状态机
enum class SessionState {
    Idle,
    Validating,
    BriefFailed,
    BriefOk,
    SendingContent,
    Paused,
    WaitingContentConfirm,  // V0.0.4：已发内容段，等待平台确认
    Completed,
    Aborted
};

// 召唤解码结果
struct SummonRequest {
    uint32_t cmdId = 0;
    std::string fullPathFileName;
    uint64_t startByte = 1;   // 协议 1-based
    uint64_t fileOffset = 0;  // 内部 0-based
    bool rawValid = false;
};

// 简报编码输入
struct BriefResponse {
    uint32_t cmdId = 0;
    BriefStatus status = BriefStatus::Failure;
    std::string errorCode;
    std::string note;
    std::string fileCrc;
    uint64_t fileSize = 0;
    std::string modifyTime;
};

// 内容段编码输入
struct ContentSegment {
    uint32_t cmdId = 0;
    uint32_t fileSegNo = 1;
    std::vector<uint8_t> contentRaw;
    bool continueFlag = true;
};

// V0.0.4：平台 → 网关，召唤上传内容段确认
struct ContentConfirmRequest {
    uint32_t cmdId = 0;
    uint32_t fileSegNo = 0;
    BriefStatus status = BriefStatus::Failure;
    std::string errorCode;
    std::string note;
};

struct SessionRecord {
    uint32_t cmdId = 0;
    std::string fullPathFileName;
    uint64_t fileSize = 0;
    uint64_t nextFileOffset = 0;
    uint32_t nextSegNo = 1;
    uint32_t awaitingConfirmSegNo = 0;  // 0 表示未等待确认
    std::string fileCrc;
    std::string modifyTime;
    SessionState state = SessionState::Idle;
    std::chrono::steady_clock::time_point lastActivity{};
};

struct TransferConfig {
    uint32_t timeoutSec = 180;
    uint32_t chunkSize = 4096;
    std::vector<std::string> allowedPathRoots{"/"};
};

}  // namespace transfer
