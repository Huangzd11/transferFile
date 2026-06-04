// 召唤上传会话内存存储
// 进程内 vector 保存 SessionRecord；按状态判断活跃会话。

#include "transfer/session_store.hpp"

#include <algorithm>

namespace transfer {

bool MemorySessionStore::isActive(const SessionRecord& r) {    // 判断是否活跃
    switch (r.state) {
        case SessionState::Validating:    // 验证
        case SessionState::BriefOk:    // 简报成功
        case SessionState::SendingContent:    // 发送内容
        case SessionState::WaitingContentConfirm:    // 等待内容确认
        case SessionState::Paused:
            return true;    // 返回true
        default:
            return false;    // 返回false
    }
}

std::optional<SessionRecord> MemorySessionStore::getByCmdId(uint32_t cmdId) const {    // 根据CmdId获取会话     
    for (const auto& s : sessions_) {
        if (s.cmdId == cmdId) return s;    // 返回会话
    }
    return std::nullopt;    // 返回空
}

void MemorySessionStore::upsert(const SessionRecord& record) {    // 更新会话
    for (auto& s : sessions_) {
        if (s.cmdId == record.cmdId) {
            s = record;
            return;    // 返回
        }
    }
    sessions_.push_back(record);    // 添加会话
}

void MemorySessionStore::remove(uint32_t cmdId) {    // 删除会话
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [cmdId](const SessionRecord& s) { return s.cmdId == cmdId; }),
        sessions_.end());
}   

bool MemorySessionStore::hasActiveSessionOtherThan(uint32_t cmdId) const {    // 判断是否有活跃会话
    for (const auto& s : sessions_) {
        if (s.cmdId != cmdId && isActive(s)) return true;    // 返回true
    }
    return false;    // 返回false
}

}  // namespace transfer
