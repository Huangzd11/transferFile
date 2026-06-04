// 传输超时看门狗实现
// arm 设置截止时间；reset 按原 timeoutSec 顺延；tick 扫描并回调。

#include "transfer/timeout_watchdog.hpp"

namespace transfer {

std::chrono::steady_clock::time_point SteadyClock::now() const {
    return std::chrono::steady_clock::now();
}

TimeoutWatchdog::TimeoutWatchdog(IClock& clock) : clock_(clock) {}

void TimeoutWatchdog::setCallback(std::function<void(uint32_t)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(cb);
}

void TimeoutWatchdog::arm(uint32_t cmdId, uint32_t timeoutSec) {
    std::lock_guard<std::mutex> lock(mutex_);
    Entry e;
    e.timeoutSec = timeoutSec;
    e.deadline = clock_.now() + std::chrono::seconds(timeoutSec);
    entries_[cmdId] = e;
}

void TimeoutWatchdog::reset(uint32_t cmdId) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(cmdId);
    if (it == entries_.end()) return;
    it->second.deadline = clock_.now() + std::chrono::seconds(it->second.timeoutSec);
}

void TimeoutWatchdog::disarm(uint32_t cmdId) {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.erase(cmdId);
}

void TimeoutWatchdog::tick() {
    std::function<void(uint32_t)> cb;
    std::vector<uint32_t> expired;
  {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = clock_.now();
        for (const auto& kv : entries_) {
            if (now >= kv.second.deadline) expired.push_back(kv.first);
        }
        for (uint32_t id : expired) entries_.erase(id);
        cb = callback_;
    }
    if (cb) {
        for (uint32_t id : expired) cb(id);
    }
}

}  // namespace transfer
