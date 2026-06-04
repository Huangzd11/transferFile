// 平台推送会话内存存储
// 进程内 vector 保存 PushSessionRecord。

#include "transfer/push_session_store.hpp"

#include <algorithm>

namespace transfer {

bool MemoryPushSessionStore::isActive(const PushSessionRecord& r) {    // 判断是否活跃      
    switch (r.state) {
        case PushSessionState::BriefOk:    // 简报成功
        case PushSessionState::ReceivingContent:    // 接收内容
            return true;    // 返回true
        default:
            return false;    // 返回false
    }
}

std::optional<PushSessionRecord> MemoryPushSessionStore::getByCmdId(
    uint32_t cmdId) const {    // 根据CmdId获取会话
    for (const auto& s : sessions_) {
        if (s.cmdId == cmdId) return s;    // 返回会话
    }
    return std::nullopt;    // 返回空
}

void MemoryPushSessionStore::upsert(const PushSessionRecord& record) {    // 更新会话
    for (auto& s : sessions_) {
        if (s.cmdId == record.cmdId) {
            s = record;
            return;    // 返回
        }
    }
    sessions_.push_back(record);    // 添加会话
}

void MemoryPushSessionStore::remove(uint32_t cmdId) {    // 删除会话
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [cmdId](const PushSessionRecord& s) { return s.cmdId == cmdId; }),
        sessions_.end());
}   

bool MemoryPushSessionStore::hasActiveSession() const {    // 判断是否有活跃会话
    for (const auto& s : sessions_) {
        if (isActive(s)) return true;    // 返回true
    }
    return false;    // 返回false
}

}  // namespace transfer
