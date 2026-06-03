#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace transfer {

// 可注入时钟，便于单元测试
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};

class SteadyClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override;
};

class ITimeoutWatchdog {
public:
    virtual ~ITimeoutWatchdog() = default;
    virtual void setCallback(std::function<void(uint32_t)> cb) = 0;
    virtual void arm(uint32_t cmdId, uint32_t timeoutSec) = 0;
    virtual void reset(uint32_t cmdId) = 0;
    virtual void disarm(uint32_t cmdId) = 0;
    virtual void tick() = 0;  // 由测试或主循环驱动检查超时
};

class TimeoutWatchdog : public ITimeoutWatchdog {
public:
    explicit TimeoutWatchdog(IClock& clock);

    void setCallback(std::function<void(uint32_t)> cb) override;
    void arm(uint32_t cmdId, uint32_t timeoutSec) override;
    void reset(uint32_t cmdId) override;
    void disarm(uint32_t cmdId) override;
    void tick() override;

private:
    struct Entry {
        uint32_t timeoutSec = 0;
        std::chrono::steady_clock::time_point deadline{};
    };

    IClock& clock_;
    std::function<void(uint32_t)> callback_;
    std::unordered_map<uint32_t, Entry> entries_;
    std::mutex mutex_;
};

}  // namespace transfer
