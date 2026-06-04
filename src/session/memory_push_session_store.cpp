// 平台推送会话内存存储
// 进程内 vector 保存 PushSessionRecord。

#include "transfer/push_session_store.hpp"

#include <algorithm>

namespace transfer {

bool MemoryPushSessionStore::isActive(const PushSessionRecord& r) {
    switch (r.state) {
        case PushSessionState::BriefOk:
        case PushSessionState::ReceivingContent:
            return true;
        default:
            return false;
    }
}

std::optional<PushSessionRecord> MemoryPushSessionStore::getByCmdId(
    uint32_t cmdId) const {
    for (const auto& s : sessions_) {
        if (s.cmdId == cmdId) return s;
    }
    return std::nullopt;
}

void MemoryPushSessionStore::upsert(const PushSessionRecord& record) {
    for (auto& s : sessions_) {
        if (s.cmdId == record.cmdId) {
            s = record;
            return;
        }
    }
    sessions_.push_back(record);
}

void MemoryPushSessionStore::remove(uint32_t cmdId) {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [cmdId](const PushSessionRecord& s) { return s.cmdId == cmdId; }),
        sessions_.end());
}

bool MemoryPushSessionStore::hasActiveSession() const {
    for (const auto& s : sessions_) {
        if (isActive(s)) return true;
    }
    return false;
}

}  // namespace transfer
