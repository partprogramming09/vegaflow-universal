// Unit test: the flags handed to the elevated instance keep each argument whole (Windows command-line quoting rules).

#include <initializer_list>
#include <string>
#include <vector>

#include "windows/command_line.hpp"
#include "windows/text_convert.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

std::wstring Forward(std::initializer_list<const char*> args) {
    std::vector<char*> argv;
    for (const char* a : args) argv.push_back(const_cast<char*>(a));
    return vegaflow::BuildForwardedArgs(static_cast<int>(argv.size()), argv.data());
}

} // namespace

void Test97() {
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
    RecordResult(ok, "Test 97: --elevate forwards each argument whole, quoted when needed",
                 "spaces=[" + vegaflow::app::WideToUtf8(spaces) + "] trailing=[" + vegaflow::app::WideToUtf8(trailing) + "] plain=[" +
                     vegaflow::app::WideToUtf8(plain) + "]");
}

} // namespace vftest
