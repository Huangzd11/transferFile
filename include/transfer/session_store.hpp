// 召唤上传会话存储接口
// 按 CmdId 管理 SessionRecord，支持活跃会话互斥判断。

#pragma once

#include "transfer/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace transfer {

class ISessionStore {
public:
    virtual ~ISessionStore() = default;

    virtual std::optional<SessionRecord> getByCmdId(uint32_t cmdId) const = 0;
    virtual void upsert(const SessionRecord& record) = 0;
    virtual void remove(uint32_t cmdId) = 0;
    virtual bool hasActiveSessionOtherThan(uint32_t cmdId) const = 0;
};

class MemorySessionStore : public ISessionStore {
public:
    std::optional<SessionRecord> getByCmdId(uint32_t cmdId) const override;
    void upsert(const SessionRecord& record) override;
    void remove(uint32_t cmdId) override;
    bool hasActiveSessionOtherThan(uint32_t cmdId) const override;

private:
    static bool isActive(const SessionRecord& r);

    std::vector<SessionRecord> sessions_;
};

}  // namespace transfer
