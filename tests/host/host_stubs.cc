// Link-time stand-ins for the few Windows-only functions that host-compilable code still names (and, at the end,
// a portable text_convert).
// (.cc, not .cpp, so the Windows builds that glob tests/**/*.cpp do not pick it up.) Every stub aborts with its own name: a test that passes on the host never reaches one. If a test does, the
// abort says which function it needed, and that test file belongs in the exclusion list (tests/host/allowlist.txt).

#include "detect/hardware_profiler.hpp"
#include "ports/text_convert.hpp"
#include "windows/change_journal.hpp"
#include "windows/memory.hpp"
#include "windows/system_counters.hpp"

#include <locale.h>
#include <wctype.h>
#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void Unavailable(const char* name) {
    std::fprintf(stderr, "[HOST STUB] %s needs Windows; exclude this test file from the host list\n", name);
    std::abort();
}
} // namespace

namespace vegaflow {

SystemHardwareProfile HardwareProfiler::DetectHardware(bool) { Unavailable("HardwareProfiler::DetectHardware"); }
ApuOptimizationPolicy HardwareProfiler::BuildOptimizationPolicy(const SystemHardwareProfile&, uint64_t) {
    Unavailable("HardwareProfiler::BuildOptimizationPolicy");
}

MemoryProbe& DefaultWindowsMemoryProbe() { Unavailable("DefaultWindowsMemoryProbe"); }
MemoryControl& DefaultWindowsMemoryControl() { Unavailable("DefaultWindowsMemoryControl"); }
uint64_t QueryCurrentProcessPrivateWorkingSetBytes() { Unavailable("QueryCurrentProcessPrivateWorkingSetBytes"); }

// No journal is active on the host: callers already handle nullptr (they just skip journaling).
ChangeJournal* GetActiveChangeJournal() { return nullptr; }

uint32_t CountEcoQoSProcesses() { Unavailable("CountEcoQoSProcesses"); }
uint32_t CountDirectFlipConfiguredGames() { Unavailable("CountDirectFlipConfiguredGames"); }

} // namespace vegaflow

// ports/text_convert.hpp for the host. The real one is windows/text_convert.cpp (MultiByteToWideChar); this one is a plain
// UTF-8 <-> code point converter that also rejects overlongs, surrogates and values above U+10FFFF, and turns a bad
// byte into U+FFFD, like Windows. It is a stand-in for tests only, so unlike the stubs above it really works.
namespace vegaflow::app {

namespace {

// Decodes one code point at s[i]; advances i. Returns false (and eats one byte) when the bytes are not well-formed.
bool NextCodePoint(const std::string& s, size_t& i, char32_t& cp) {
    const auto b = static_cast<unsigned char>(s[i]);
    size_t n = 0;
    char32_t min = 0;
    if (b < 0x80) { cp = b; ++i; return true; }
    if (b >= 0xC2 && b <= 0xDF) { n = 1; cp = b & 0x1F; min = 0x80; }
    else if (b >= 0xE0 && b <= 0xEF) { n = 2; cp = b & 0x0F; min = 0x800; }
    else if (b >= 0xF0 && b <= 0xF4) { n = 3; cp = b & 0x07; min = 0x10000; }
    else { ++i; return false; }
    if (i + n >= s.size()) { ++i; return false; } // truncated sequence
    char32_t v = cp;
    for (size_t k = 1; k <= n; ++k) {
        const auto c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) { ++i; return false; }
        v = (v << 6) | (c & 0x3F);
    }
    if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { ++i; return false; }
    cp = v;
    i += n + 1;
    return true;
}

} // namespace

std::wstring Utf8ToWide(const std::string& str) {
    std::wstring out;
    for (size_t i = 0; i < str.size();) {
        char32_t cp = 0;
        out.push_back(NextCodePoint(str, i, cp) ? static_cast<wchar_t>(cp) : L'\uFFFD');
    }
    return out;
}

std::string WideToUtf8(const std::wstring& wstr) {
    std::string out;
    for (const wchar_t w : wstr) {
        const auto cp = static_cast<char32_t>(w);
        if (cp < 0x80) out.push_back(static_cast<char>(cp));
        else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    // A UTF-8 locale only for this call (a global setlocale would change other host tests): non-ASCII letters fold like on Windows.
    static const locale_t utf8 = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", nullptr);
    for (size_t i = 0; i < a.size(); ++i) {
        if (towupper_l(static_cast<wint_t>(a[i]), utf8) != towupper_l(static_cast<wint_t>(b[i]), utf8)) return false;
    }
    return true;
}

bool IsValidUtf8(const std::string& str) {
    for (size_t i = 0; i < str.size();) {
        char32_t cp = 0;
        if (!NextCodePoint(str, i, cp)) return false;
    }
    return true;
}

} // namespace vegaflow::app
