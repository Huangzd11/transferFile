#pragma once

#include "transfer/push_types.hpp"

#include <cstdint>
#include <optional>

namespace transfer {

class IPushSessionStore {
public:
    virtual ~IPushSessionStore() = default;
    virtual std::optional<PushSessionRecord> getByCmdId(uint32_t cmdId) const = 0;
    virtual void upsert(const PushSessionRecord& record) = 0;
    virtual void remove(uint32_t cmdId) = 0;
    virtual bool hasActiveSession() const = 0;
};

class MemoryPushSessionStore : public IPushSessionStore {
public:
    std::optional<PushSessionRecord> getByCmdId(uint32_t cmdId) const override;
    void upsert(const PushSessionRecord& record) override;
    void remove(uint32_t cmdId) override;
    bool hasActiveSession() const override;

private:
    static bool isActive(const PushSessionRecord& r);
    std::vector<PushSessionRecord> sessions_;
};

}  // namespace transfer
