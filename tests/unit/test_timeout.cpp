#include "transfer/timeout_watchdog.hpp"

#include "../test_compat.hpp"

class FakeClock : public transfer::IClock {
public:
    std::chrono::steady_clock::time_point now() const override { return now_; }
    void advance(std::chrono::seconds s) { now_ += s; }
    std::chrono::steady_clock::time_point now_{std::chrono::steady_clock::now()};
};

TEST(TimeoutWatchdogTest, FiresAfterDeadline) {
    FakeClock clock;
    transfer::TimeoutWatchdog wd(clock);
    uint32_t fired = 0;
    wd.setCallback([&](uint32_t id) {
        EXPECT_EQ(id, 42u);
        fired = id;
    });
    wd.arm(42, 180);
    clock.advance(std::chrono::seconds(179));
    wd.tick();
    EXPECT_EQ(fired, 0u);
    clock.advance(std::chrono::seconds(2));
    wd.tick();
    EXPECT_EQ(fired, 42u);
}

TEST(TimeoutWatchdogTest, ResetExtendsDeadline) {
    FakeClock clock;
    transfer::TimeoutWatchdog wd(clock);
    uint32_t fired = 0;
    wd.setCallback([&](uint32_t id) { fired = id; });
    wd.arm(7, 60);
    clock.advance(std::chrono::seconds(50));
    wd.reset(7);
    clock.advance(std::chrono::seconds(20));
    wd.tick();
    EXPECT_EQ(fired, 0u);
    clock.advance(std::chrono::seconds(50));
    wd.tick();
    EXPECT_EQ(fired, 7u);
}
