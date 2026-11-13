#pragma once
// Port: the current time, so rules that wait (the memory purge cooldown) can be tested by moving a fake clock
// instead of sleeping. SteadyClock is the real one.

#include <chrono>

namespace vegaflow {

class Clock {
public:
    virtual ~Clock() = default;
    virtual std::chrono::steady_clock::time_point Now() const = 0;
    virtual std::chrono::system_clock::time_point WallNow() const { return std::chrono::system_clock::now(); }
};

class SteadyClock final : public Clock {
public:
    std::chrono::steady_clock::time_point Now() const override { return std::chrono::steady_clock::now(); }
    std::chrono::system_clock::time_point WallNow() const override { return std::chrono::system_clock::now(); }

    // The one real clock, for callers that do not inject another.
    static SteadyClock& Instance() {
        static SteadyClock clock;
        return clock;
    }
};

} // namespace vegaflow
