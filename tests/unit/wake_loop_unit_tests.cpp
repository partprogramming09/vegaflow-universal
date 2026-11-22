// E2.4 unit tests: the event-driven daemon loop (session/wake_loop) with a scripted EventSource and a fake clock.
// Called by tests/test_main.cpp; see tests/test_support.hpp.

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "session/wake_loop.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

using namespace vegaflow;
using namespace vegaflow::session;
using std::chrono::milliseconds;

struct FakeClock final : Clock {
    std::chrono::steady_clock::time_point now{};
    std::chrono::steady_clock::time_point Now() const override { return now; }
    int64_t Ms() const { return std::chrono::duration_cast<milliseconds>(now.time_since_epoch()).count(); }
};

// Events at fixed milliseconds; WaitForWake moves the fake clock to the next event or to the timeout.
// After the script ends it returns Stop (as many times as it is asked).
class ScriptedEvents final : public EventSource {
public:
    struct Ev { int64_t at_ms; WakeReason reason; };
    ScriptedEvents(FakeClock& c, std::vector<Ev> s, bool active = true) : clock_(c), script_(std::move(s)), active_(active) {}
    WakeReason WaitForWake(uint32_t timeout_ms) override {
        timeouts.push_back(timeout_ms);
        const int64_t deadline = clock_.Ms() + timeout_ms;
        if (next_ < script_.size() && script_[next_].at_ms <= deadline) {
            const Ev e = script_[next_++];
            if (e.at_ms > clock_.Ms()) clock_.now = std::chrono::steady_clock::time_point(milliseconds(e.at_ms));
            return e.reason;
        }
        if (next_ >= script_.size()) return WakeReason::Stop;
        clock_.now = std::chrono::steady_clock::time_point(milliseconds(deadline));
        return WakeReason::Timer;
    }
    bool EventsActive() const override { return active_; }
    std::vector<uint32_t> timeouts;
private:
    FakeClock& clock_;
    std::vector<Ev> script_;
    size_t next_ = 0;
    bool active_;
};

struct Run {
    int iterations = 0, cleanups = 0, device_checks = 0, power_seen = 0, device_seen = 0, adapter_reads = 0;
    std::vector<int64_t> at;
    std::vector<std::string> logs;
    bool game = false;
    bool stop_flag = false;
    bool close_game_on_second = false;
};

Run Drive(ScriptedEvents& ev, FakeClock& clock, uint32_t poll_ms, bool game_at_start = false, bool game_ends_after_first = false) {
    Run r;
    r.game = game_at_start;
    WakeLoopHooks h;
    h.should_stop = [&] { return r.stop_flag; };
    h.game_active = [&] { return r.game; };
    h.iterate = [&](const WakeSet& s, bool check) {
        ++r.iterations;
        r.at.push_back(clock.Ms());
        if (check) ++r.adapter_reads;
        if (s.device) ++r.device_seen;
        if (s.power) ++r.power_seen;
        if (game_ends_after_first) r.game = false;
    };
    h.cleanup = [&] { ++r.cleanups; };
    h.log = [&](const std::string& m) { r.logs.push_back(m); };
    RunWakeLoop(ev, clock, poll_ms, h);
    return r;
}

} // namespace

// E2.4
void Test114() {
    using E = ScriptedEvents::Ev;
    using W = WakeReason;

    { // 1. no game, no events for 29 s: 0 iterations, one wait of 30 s at most
        FakeClock c; ScriptedEvents ev(c, {{29000, W::Stop}});
        const Run r = Drive(ev, c, 2500);
        RecordResult(r.iterations == 0 && r.cleanups == 1 && ev.timeouts.size() == 1 && ev.timeouts[0] == kIdleSafetyNetMs,
                     "Test 114.1: no game and no events for 29 s -> 0 iterations", std::to_string(r.iterations));
    }
    { // 2. Foreground -> one immediate iteration
        FakeClock c; ScriptedEvents ev(c, {{1000, W::Foreground}});
        const Run r = Drive(ev, c, 2500);
        RecordResult(r.iterations == 1 && r.at[0] == 1000, "Test 114.2: Foreground -> one iteration right away", std::to_string(r.at.empty() ? -1 : r.at[0]));
    }
    { // 3. game active -> the timer is poll_interval_ms (2500)
        FakeClock c; ScriptedEvents ev(c, {{2500, W::Timer}});
        const Run r = Drive(ev, c, 2500, true);
        RecordResult(!ev.timeouts.empty() && ev.timeouts[0] == 2500, "Test 114.3: game active -> 2500 ms timer", std::to_string(ev.timeouts.empty() ? 0 : ev.timeouts[0]));
    }
    { // 4. game closes -> back to the 30 s safety net
        FakeClock c; ScriptedEvents ev(c, {{500, W::Foreground}, {40000, W::Stop}});
        const Run r = Drive(ev, c, 2500, true, true);
        RecordResult(ev.timeouts.size() >= 2 && ev.timeouts[0] == 2500 && ev.timeouts[1] == kIdleSafetyNetMs,
                     "Test 114.4: game closed -> 30 s safety net", std::to_string(ev.timeouts.size() > 1 ? ev.timeouts[1] : 0));
    }
    { // 5. storm: 50 Foreground in 1 s -> at most one iteration per 250 ms, the events are not lost
        std::vector<E> s;
        for (int i = 0; i < 50; ++i) s.push_back({i * 20, W::Foreground});
        s.push_back({990, W::Device});
        s.push_back({5000, W::Stop});
        FakeClock c; ScriptedEvents ev(c, s);
        const Run r = Drive(ev, c, 2500);
        bool gaps_ok = true;
        for (size_t i = 1; i < r.at.size(); ++i) gaps_ok = gaps_ok && r.at[i] - r.at[i - 1] >= kMinWakeGapMs;
        RecordResult(gaps_ok && r.iterations >= 1 && r.iterations <= 5 && r.device_seen == 1,
                     "Test 114.5: 50 Foreground in 1 s collapse to <= 1 iteration per 250 ms", std::to_string(r.iterations) + " iterations");
    }
    { // 6. Device -> the iteration reads the adapters; a Timer-only wake with a game does not
        FakeClock c; ScriptedEvents ev(c, {{1000, W::Device}, {3500, W::Timer}, {5000, W::Stop}});
        const Run r = Drive(ev, c, 2500, true);
        RecordResult(r.iterations == 2 && r.device_seen == 1 && r.adapter_reads == 1,
                     "Test 114.6a: Device -> adapters re-read; the game timer tick does not read them", std::to_string(r.adapter_reads));
    }
    { // 7. Power reaches the iteration (daemon_loop rebuilds the static policy from it)
        FakeClock c; ScriptedEvents ev(c, {{1000, W::Power}, {2000, W::Stop}});
        const Run r = Drive(ev, c, 2500);
        RecordResult(r.iterations == 1 && r.power_seen == 1, "Test 114.7: Power -> iteration flagged as power change");
    }
    { // 8. Stop while waiting -> exit with one cleanup, no iteration
        FakeClock c; ScriptedEvents ev(c, {{100, W::Stop}});
        const Run r = Drive(ev, c, 2500);
        RecordResult(r.iterations == 0 && r.cleanups == 1, "Test 114.8: Stop while waiting -> cleanup runs");
        // Stop arriving inside the coalescing gap also ends the loop and cleans up once.
        FakeClock c2; ScriptedEvents ev2(c2, {{0, W::Foreground}, {50, W::Foreground}, {100, W::Stop}});
        const Run r2 = Drive(ev2, c2, 2500);
        RecordResult(r2.iterations == 1 && r2.cleanups == 1, "Test 114.8b: Stop inside the 250 ms gap -> one cleanup");
    }
    { // 9. hooks failed -> 1500 ms polling, one warning, adapters read every tick
        FakeClock c; ScriptedEvents ev(c, {{4600, W::Stop}}, false);
        const Run r = Drive(ev, c, 2500);
        RecordResult(r.iterations == 3 && r.adapter_reads == 3 && r.logs.size() == 1 && ev.timeouts[0] == kPollFallbackMs,
                     "Test 114.9: hook failure -> 1500 ms polling with a log warning", std::to_string(r.iterations) + " ticks");
    }
    { // 10. session 0 warns, a desktop session does not
        RecordResult(NoDesktopWarning(0) != nullptr && NoDesktopWarning(1) == nullptr, "Test 114.10: session 0 warning");
    }
    { // 11. Stop twice in a row -> one cleanup
        FakeClock c; ScriptedEvents ev(c, {{100, W::Stop}, {100, W::Stop}});
        const Run r = Drive(ev, c, 2500);
        RecordResult(r.cleanups == 1 && ev.timeouts.size() == 1, "Test 114.11: Stop twice -> one cleanup");
        // The console handler (should_stop) ends the loop the same way.
        FakeClock c2; ScriptedEvents ev2(c2, {});
        Run r2; r2.stop_flag = true;
        WakeLoopHooks h; h.should_stop = [&] { return r2.stop_flag; }; h.game_active = [] { return false; };
        h.iterate = [](const WakeSet&, bool) {}; h.cleanup = [&] { ++r2.cleanups; };
        RunWakeLoop(ev2, c2, 2500, h);
        RecordResult(r2.cleanups == 1 && ev2.timeouts.empty(), "Test 114.11b: should_stop set -> no wait, one cleanup");
    }
    { // 12. poll_interval_ms at the minimum (250), and below it, is never a 0 ms wait
        FakeClock c; ScriptedEvents ev(c, {{250, W::Timer}, {500, W::Timer}, {600, W::Stop}});
        Drive(ev, c, 250, true);
        FakeClock c2; ScriptedEvents ev2(c2, {{10, W::Stop}});
        Drive(ev2, c2, 0, true);
        RecordResult(ev.timeouts[0] == 250 && ev2.timeouts[0] == kMinWakeGapMs, "Test 114.12: poll 250 respected, never a 0 ms wait");
    }
}

} // namespace vftest
