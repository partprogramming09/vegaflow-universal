// Unit tests: what the window says about itself. No Windows calls: the header only holds names and a pure
// formatter, so the three cases of the old hard-coded laptop line (long name, empty CPU, empty GPU) run anywhere.
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#include <algorithm>
#include <cwctype>
#include <string>

#include "ui/app_identity.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

// The machine the tests were written on. Showing it for any other machine is a false statement.
bool NamesTestLaptop(const std::string& text) { return Has(text, "4600H") || Has(text, "Vega 6"); }

} // namespace

void Test58() {
    // -------------------------------------------------------------------------
    // Test 58: the hardware line shows this machine, never the test laptop (long name, empty CPU, empty GPU)
    // -------------------------------------------------------------------------
    using vegaflow::ui::FormatHardwareLine;

    const std::string normal = FormatHardwareLine("Intel(R) Core(TM) Ultra 7 155H", "Intel(R) Arc(TM) Graphics");
    const bool normal_ok = Has(normal, "Ultra 7 155H") && Has(normal, "Arc") && !NamesTestLaptop(normal);

    const std::string long_cpu(70, 'X');
    const std::string cut = FormatHardwareLine("Intel " + long_cpu, "Some GPU");
    const bool long_ok = cut.size() <= 65 && Has(cut, "Intel XXXX") && !NamesTestLaptop(cut);

    const std::string no_cpu = FormatHardwareLine("", "NVIDIA GeForce RTX 4060 Laptop GPU");
    const bool no_cpu_ok = Has(no_cpu, "RTX 4060") && Has(no_cpu, "no identificad") && !NamesTestLaptop(no_cpu);

    const std::string no_gpu = FormatHardwareLine("Intel(R) Core(TM) i5-1235U", "");
    const bool no_gpu_ok = Has(no_gpu, "i5-1235U") && Has(no_gpu, "no identificad") && !NamesTestLaptop(no_gpu);

    // 59 ASCII bytes then two-byte letters: the cut at 62 bytes falls inside the second letter and must back off.
    const std::string accented = FormatHardwareLine(std::string(59, 'A') + "\xC3\xA1\xC3\xA1\xC3\xA1", "G");
    bool utf8_ok = accented.size() <= 65 && accented.size() >= 3 && accented.substr(accented.size() - 3) == "...";
    for (size_t i = 0; utf8_ok && i + 3 < accented.size();) {
        const unsigned char c = static_cast<unsigned char>(accented[i]);
        const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : 0; // only 1- and 2-byte sequences appear here
        utf8_ok = n != 0 && i + n <= accented.size() - 3;
        i += n;
    }

    const std::string none = FormatHardwareLine("", "");
    const bool none_ok = !none.empty() && !NamesTestLaptop(none);

    auto pass = [](bool b) { return std::string(b ? "PASS" : "FAIL"); };
    RecordResult(normal_ok && long_ok && utf8_ok && no_cpu_ok && no_gpu_ok && none_ok,
                 "Test 58: the hardware line shows this machine, never the test laptop",
                 "normal: " + pass(normal_ok) + ", name over 65 chars cut, not replaced: " + pass(long_ok) +
                     ", cut never splits a UTF-8 letter: " + pass(utf8_ok) + ", empty CPU: " + pass(no_cpu_ok) + ", empty GPU: " + pass(no_gpu_ok) +
                     ", both empty: " + pass(none_ok));
}

void Test59() {
    // -------------------------------------------------------------------------
    // Test 59: the window class, title and version carry VegaFlow's own name (no other program's)
    // -------------------------------------------------------------------------
    auto lower = [](std::wstring s) {
        std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return s;
    };
    const std::wstring cls = lower(vegaflow::ui::kWindowClass);
    const std::wstring title = lower(vegaflow::ui::kWindowTitle);
    const bool names_ok = cls.find(L"uxtu") == std::wstring::npos && title.find(L"uxtu") == std::wstring::npos &&
                          title.find(L"tuning utility") == std::wstring::npos &&
                          cls.find(L"vegaflow") != std::wstring::npos && title.find(L"vegaflow") != std::wstring::npos;
    const std::string version = vegaflow::ui::kVersion;
    const bool version_ok = version.size() >= 5 && version[0] >= '0' && version[0] <= '9' &&
                            std::count(version.begin(), version.end(), '.') == 2;
    RecordResult(names_ok && version_ok, "Test 59: window class, title and version are VegaFlow's own",
                 "names: " + std::string(names_ok ? "PASS" : "FAIL") + ", version " + version +
                     " is MAJOR.MINOR.PATCH: " + (version_ok ? "PASS" : "FAIL"));
}

} // namespace vftest
