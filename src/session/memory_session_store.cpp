// 召唤上传会话内存存储
// 进程内 vector 保存 SessionRecord；按状态判断活跃会话。

#include "transfer/session_store.hpp"

#include <algorithm>

namespace transfer {

bool MemorySessionStore::isActive(const SessionRecord& r) {
    switch (r.state) {
        case SessionState::Validating:
        case SessionState::BriefOk:
        case SessionState::SendingContent:
        case SessionState::WaitingContentConfirm:
        case SessionState::Paused:
            return true;
        default:
            return false;
    }
}

std::optional<SessionRecord> MemorySessionStore::getByCmdId(uint32_t cmdId) const {
    for (const auto& s : sessions_) {
        if (s.cmdId == cmdId) return s;
    }
    return std::nullopt;
}

void MemorySessionStore::upsert(const SessionRecord& record) {
    for (auto& s : sessions_) {
        if (s.cmdId == record.cmdId) {
            s = record;
            return;
        }
    }
    sessions_.push_back(record);
}

void MemorySessionStore::remove(uint32_t cmdId) {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [cmdId](const SessionRecord& s) { return s.cmdId == cmdId; }),
        sessions_.end());
}

bool MemorySessionStore::hasActiveSessionOtherThan(uint32_t cmdId) const {
    for (const auto& s : sessions_) {
        if (s.cmdId != cmdId && isActive(s)) return true;
    }
    return false;
}

}  // namespace transfer
