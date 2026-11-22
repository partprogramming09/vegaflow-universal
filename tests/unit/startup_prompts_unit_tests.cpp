// Unit tests: the two startup questions (restore point, revert pending changes) only reach a person on a visible
// console, and "nobody answered" (hidden console, pipe, file, timeout, end of input) always takes the safe default.

#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "session/startup_prompts.hpp"

#include "../test_support.hpp"

namespace vftest {
namespace {

using std::chrono::milliseconds;
using KR = vegaflow::KeyRead;

struct FakeClock final : vegaflow::Clock {
    std::chrono::steady_clock::time_point now{};
    std::chrono::steady_clock::time_point Now() const override { return now; }
};

// Events at fixed milliseconds. Next moves the fake clock to the next event when it falls within max_wait (an event
// already past is delivered at once), otherwise to the end of max_wait and returns Timeout; a spent script just waits.
class ScriptedKeys final : public vegaflow::KeyInput {
public:
    struct Ev { int64_t at_ms; KR key; };
    ScriptedKeys(FakeClock& c, std::vector<Ev> s) : clock_(c), script_(std::move(s)) {}
    KR Next(milliseconds max_wait) override {
        const int64_t now = std::chrono::duration_cast<milliseconds>(clock_.now.time_since_epoch()).count();
        if (next_ < script_.size() && script_[next_].at_ms <= now + max_wait.count()) {
            const Ev e = script_[next_++];
            if (e.at_ms > now) clock_.now = std::chrono::steady_clock::time_point(milliseconds(e.at_ms));
            return e.key;
        }
        clock_.now = std::chrono::steady_clock::time_point(milliseconds(now + max_wait.count()));
        return KR{KR::Kind::Timeout, 0};
    }
    void Discard() override { ++discards; }
    int discards = 0;
private:
    FakeClock& clock_;
    std::vector<Ev> script_;
    size_t next_ = 0;
};

KR Key(char16_t c) { return KR{KR::Kind::Key, c}; }
KR Kind(KR::Kind k) { return KR{k, 0}; }
// Types text one key per millisecond starting at t0, then Enter.
std::vector<ScriptedKeys::Ev> Typed(const std::u16string& text, int64_t t0 = 100) {
    std::vector<ScriptedKeys::Ev> v;
    for (char16_t c : text) v.push_back({t0++, Key(c)});
    v.push_back({t0, Key(u'\r')});
    return v;
}

struct Outcome {
    std::optional<std::string> line;
    int discards = 0;
    int64_t end_ms = 0;
    std::string out;
};
Outcome Ask(std::vector<ScriptedKeys::Ev> script, unsigned timeout_ms = vegaflow::session::kStartupPromptTimeoutMs) {
    FakeClock clock;
    ScriptedKeys keys(clock, std::move(script));
    std::ostringstream out;
    Outcome r;
    r.line = vegaflow::session::ReadLineWithDeadline(keys, clock, out, "? ", timeout_ms);
    r.discards = keys.discards;
    r.end_ms = std::chrono::duration_cast<milliseconds>(clock.now.time_since_epoch()).count();
    r.out = out.str();
    return r;
}

} // namespace

void Test136() {
    namespace s = vegaflow::session;
    using K = vegaflow::StdinKind;

    // Only a visible console has someone who can type; a hidden console (task, Start-Process -WindowStyle Hidden),
    // a pipe, a file or no stdin at all never answers.
    const bool who = s::CanAskPerson(K::VisibleConsole) && !s::CanAskPerson(K::HiddenConsole) &&
                     !s::CanAskPerson(K::Pipe) && !s::CanAskPerson(K::File) && !s::CanAskPerson(K::None);

    // Revert pending changes [S/n]: no answer never reverts; an answered empty line keeps today's "yes".
    const bool revert = !s::ShouldRevertPending(std::nullopt) && s::ShouldRevertPending(std::string("")) &&
                        s::ShouldRevertPending(std::string(" S ")) && s::ShouldRevertPending(std::string("sí")) &&
                        !s::ShouldRevertPending(std::string("n")) && !s::ShouldRevertPending(std::string("no"));

    // Restore point [s/N]: no answer means "not asked" (nullopt, the journal stays unmarked); empty or "n" is a real no.
    const bool rp = !s::RestorePointAnswer(std::nullopt).has_value() &&
                    s::RestorePointAnswer(std::string("")) == std::optional<bool>(false) &&
                    s::RestorePointAnswer(std::string("n")) == std::optional<bool>(false) &&
                    s::RestorePointAnswer(std::string("yes")) == std::optional<bool>(true) &&
                    s::RestorePointAnswer(std::string("Si")) == std::optional<bool>(true);

    RecordResult(who && revert && rp, "Test 136: startup questions ask only a visible console; no answer is the safe default",
                 std::string("Who=") + (who ? "1" : "0") + ", Revert=" + (revert ? "1" : "0") + ", RestorePoint=" + (rp ? "1" : "0"));
}

// Test 137: what a typed line becomes, key by key.
void Test137() {
    namespace s = vegaflow::session;
    std::string bad;
    const auto expect = [&](const char* name, const Outcome& o, const char* want) {
        if (o.line != std::optional<std::string>(want)) bad += std::string(" ") + name + "=" + (o.line ? "'" + *o.line + "'" : "none");
    };
    expect("n", Ask(Typed(u"n")), "n");
    expect("s", Ask(Typed(u"s")), "s");
    expect("Si-upper-spaces", Ask(Typed(u"  SI  ")), "  SI  "); // trimming is IsAffirmative's job
    expect("accent", Ask(Typed(u"sí")), "s\xC3\xAD");       // UTF-8 for "sí"
    expect("enter-only", Ask(Typed(u"")), "");
    expect("backspace", Ask(Typed(u"sx\b\bn")), "n");
    expect("backspace-on-empty", Ask(Typed(u"\bn")), "n");
    expect("control-keys-ignored", Ask(Typed(u"s\t\x1b\x1a")), "s");
    expect("surrogates-ignored", Ask(Typed(u"n\xD83D\xDE00")), "n");
    expect("non-key-events-ignored",
           Ask({{10, Kind(KR::Kind::Ignored)}, {20, Key(u'n')}, {30, Kind(KR::Kind::Ignored)}, {40, Key(u'\r')}}), "n");
    // Line cap: 1000 characters keep the first kMaxPromptLine and still return at Enter.
    const Outcome big = Ask(Typed(std::u16string(1000, u'a')));
    if (!big.line || big.line->size() != s::kMaxPromptLine) bad += " long-line";
    // Echo: prompt, typed text, one erase for the real Backspace, none on an empty line, a newline at Enter.
    const Outcome echo = Ask(Typed(u"s\b\bn"));
    if (echo.out != "? s\b \bn\n") bad += " echo";
    // Enter keeps the input: nothing is discarded.
    if (Ask(Typed(u"n")).discards != 0) bad += " discard-on-answer";
    RecordResult(bad.empty(), "Test 137: a startup question reads one line key by key (Enter, Backspace, UTF-8, long line)",
                 bad.empty() ? "all cases" : bad);
}

// Test 138: the 30 s limit, on a fake clock.
void Test138() {
    namespace s = vegaflow::session;
    constexpr int64_t kLimit = s::kStartupPromptTimeoutMs;
    std::string bad;
    // A key just before the limit and Enter exactly at it: the answer counts.
    if (Ask({{kLimit - 1, Key(u's')}, {kLimit, Key(u'\r')}}).line != std::optional<std::string>("s")) bad += " enter-at-limit";
    // Enter a moment later: no answer, and the "s" typed before is not one either.
    const Outcome late = Ask({{kLimit - 1, Key(u's')}, {kLimit + 1, Key(u'\r')}});
    if (late.line || late.discards != 1 || late.end_ms != kLimit) bad += " enter-after-limit";
    if (Ask({{kLimit + 1, Key(u'n')}}).line) bad += " key-after-limit";
    // Nobody types: nullopt exactly at the limit, with no real waiting.
    const Outcome silent = Ask({});
    if (silent.line || silent.end_ms != kLimit || silent.discards != 1) bad += " silent";
    // The deadline is absolute: neither a slow typist nor non-key events extend it.
    std::vector<ScriptedKeys::Ev> slow, noise;
    for (int64_t t = 1000; t < 40000; t += 1000) {
        slow.push_back({t, Key(u'a')});
        noise.push_back({t, Kind(KR::Kind::Ignored)});
    }
    const Outcome slowly = Ask(slow), noisy = Ask(noise);
    if (slowly.line || slowly.end_ms != kLimit) bad += " slow-typist";
    if (noisy.line || noisy.end_ms != kLimit) bad += " noise";
    if (Ask({{50, Key(u'n')}, {60, Key(u'\r')}}, 100).line != std::optional<std::string>("n")) bad += " short-limit";
    if (Ask(Typed(u"n"), 0).line) bad += " zero-limit";
    RecordResult(bad.empty(), "Test 138: the startup question limit is absolute and a key after it does not count",
                 bad.empty() ? "all cases" : bad);
}

// Test 139: end of input and failed reads give no answer, and unread keys are thrown away.
void Test139() {
    std::string bad;
    const Outcome eof = Ask({{100, Key(u's')}, {200, Kind(KR::Kind::Eof)}});
    if (eof.line || eof.discards != 1 || eof.end_ms != 200 || eof.out != "? s\n") bad += " eof-with-text";
    const Outcome eof_first = Ask({{5, Kind(KR::Kind::Eof)}});
    if (eof_first.line || eof_first.discards != 1) bad += " eof-first";
    const Outcome failed = Ask({{100, Key(u'n')}, {200, Kind(KR::Kind::Failed)}, {300, Key(u'\r')}});
    if (failed.line || failed.discards != 1 || failed.end_ms != 200) bad += " read-failed";
    RecordResult(bad.empty(), "Test 139: end of input or a failed read is no answer and discards pending keys",
                 bad.empty() ? "all cases" : bad);
}

} // namespace vftest
