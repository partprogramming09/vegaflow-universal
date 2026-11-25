// Unit test: the flags handed to the elevated instance keep each argument whole (Windows command-line quoting rules).

#include <initializer_list>
#include <string>
#include <vector>

#include "windows/command_line.hpp"
#include "ports/text_convert.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

std::wstring Forward(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    for (const char* a : args) argv.push_back(const_cast<char*>(a));
    return vegaflow::BuildForwardedArgs(static_cast<int>(argv.size()), argv.data());
}

} // namespace

void Test105() {
    const std::wstring spaces = Forward({"vf.exe", "--elevate", "--dxvk-install", "C:\\My Game\\bin", "--dry-run"});
    const std::wstring plain = Forward({"vf.exe", "-e", "--status"});
    // A trailing backslash before the closing quote would escape it: it must be doubled.
    const std::wstring trailing = Forward({"vf.exe", "--dxvk-install", "C:\\My Game\\"});
    const std::wstring nothing = Forward({"vf.exe", "--elevate"});
    // An empty argument must survive as "" and an embedded quote as \" (backslashes before it doubled).
    const std::wstring empty_arg = Forward({"vf.exe", "--dxvk-install", ""});
    const std::wstring quote = Forward({"vf.exe", "a\\\"b"});
    const bool ok = spaces == L"--dxvk-install \"C:\\My Game\\bin\" --dry-run" && plain == L"--status" &&
                    trailing == L"--dxvk-install \"C:\\My Game\\\\\"" && nothing.empty() &&
                    empty_arg == L"--dxvk-install \"\"" && quote == L"\"a\\\\\\\"b\"";
    RecordResult(ok, "Test 105: --elevate forwards each argument whole, quoted when needed",
                 "spaces=[" + vegaflow::app::WideToUtf8(spaces) + "] trailing=[" + vegaflow::app::WideToUtf8(trailing) + "] plain=[" +
                     vegaflow::app::WideToUtf8(plain) + "]");
}

// Windows itself (CommandLineToArgvW, through Utf8Args) must read back every forwarded argument unchanged, and a
// path with "ñ" must reach the parser as UTF-8.
void Test106() {
    const std::vector<const char*> cases = {"C:\\My Game\\bin", "C:\\My Game\\", "", "a\\\"b", "C:\\Juegos\\A\xC3\xB1o", "--dry-run"};
    std::vector<char*> argv = {const_cast<char*>("vf.exe"), const_cast<char*>("--elevate")};
    for (const char* c : cases) argv.push_back(const_cast<char*>(c));
    const std::wstring line = L"vf.exe " + vegaflow::BuildForwardedArgs(static_cast<int>(argv.size()), argv.data());
    const std::vector<std::string> back = vegaflow::Utf8Args(line.c_str());
    bool same = back.size() == cases.size() + 1;
    for (size_t i = 0; same && i < cases.size(); ++i) same = back[i + 1] == cases[i];
    std::string got;
    for (const auto& a : back) got += "[" + a + "]";
    RecordResult(same, "Test 106: the elevated instance reads back each argument unchanged, a path with an accent included",
                 "read back " + std::to_string(back.size()) + " of " + std::to_string(cases.size() + 1) + ": " + got);
}

} // namespace vftest
