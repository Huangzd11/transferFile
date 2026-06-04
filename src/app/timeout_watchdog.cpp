// 传输超时看门狗实现
// arm 设置截止时间；reset 按原 timeoutSec 顺延；tick 扫描并回调。

#include "transfer/timeout_watchdog.hpp"

namespace transfer {

std::chrono::steady_clock::time_point SteadyClock::now() const {    // 获取当前时间
    return std::chrono::steady_clock::now();
}

TimeoutWatchdog::TimeoutWatchdog(IClock& clock) : clock_(clock) {}    // 构造函数

void TimeoutWatchdog::setCallback(std::function<void(uint32_t)> cb) {
    std::lock_guard<std::mutex> lock(mutex_);    // 加锁    
    callback_ = std::move(cb);    // 设置回调函数
}

void TimeoutWatchdog::arm(uint32_t cmdId, uint32_t timeoutSec) {
    std::lock_guard<std::mutex> lock(mutex_);    // 加锁            
    Entry e;
    e.timeoutSec = timeoutSec;    // 设置超时时间
    e.deadline = clock_.now() + std::chrono::seconds(timeoutSec);    // 设置截止时间
    entries_[cmdId] = e;    // 设置条目
}

void TimeoutWatchdog::reset(uint32_t cmdId) {
    std::lock_guard<std::mutex> lock(mutex_);    // 加锁            
    auto it = entries_.find(cmdId);    // 查找条目
    if (it == entries_.end()) return;
    it->second.deadline = clock_.now() + std::chrono::seconds(it->second.timeoutSec);    // 设置截止时间
}

void TimeoutWatchdog::disarm(uint32_t cmdId) {
    std::lock_guard<std::mutex> lock(mutex_);    // 加锁            
    entries_.erase(cmdId);    // 删除条目
}

void TimeoutWatchdog::tick() {
    std::function<void(uint32_t)> cb;    // 回调函数
    std::vector<uint32_t> expired;
  {
        std::lock_guard<std::mutex> lock(mutex_);    // 加锁            
        auto now = clock_.now();    // 获取当前时间
        for (const auto& kv : entries_) {
            if (now >= kv.second.deadline) expired.push_back(kv.first);    // 添加过期条目
        }
        for (uint32_t id : expired) entries_.erase(id);    // 删除过期条目
        cb = callback_;    // 设置回调函数
    }
    if (cb) {
        for (uint32_t id : expired) cb(id);    // 调用回调函数
    }
}

}  // namespace transfer
