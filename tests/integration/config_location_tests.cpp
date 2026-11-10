// Integration tests: where config.json lives and whether its folder is trusted, on real folders and permissions.
// Called in order by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <filesystem>
#include <string>

#include "session/config.hpp"
#include "windows/config_location.hpp"
#include "windows/safe_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

bool SetSddl(const std::wstring& path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    const BOOL ok = SetFileSecurityW(path.c_str(), OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, sd);
    LocalFree(sd);
    return ok != FALSE;
}

std::string Pass(bool b) { return b ? "PASS" : "FAIL"; }

} // namespace

void Test46() {
    // -------------------------------------------------------------------------
    // Test 46: config.json lives next to the executable and is only trusted in an administrators-only folder
    // -------------------------------------------------------------------------
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::temp_directory_path(ec) / L"vegaflow_config_trust";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::wstring d = dir.wstring();

    // Owner Administrators; SYSTEM and Administrators full control, Users read and execute (like C:\ProgramData\VegaFlow).
    const bool admin_only = SetSddl(d, L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)") &&
                            vegaflow::OnlyAdminsCanWrite(d);
    // Users may modify: not trusted.
    const bool users_modify = SetSddl(d, L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;BU)") &&
                              !vegaflow::OnlyAdminsCanWrite(d);
    // An entry that only applies to children does not count for the folder itself.
    const bool inherit_only = SetSddl(d, L"O:BAD:P(A;OICI;FA;;;BA)(A;OICIIO;FA;;;BU)") && vegaflow::OnlyAdminsCanWrite(d);
    // A NULL DACL lets everyone write.
    const bool null_dacl = SetSddl(d, L"O:BAD:NO_ACCESS_CONTROL") && !vegaflow::OnlyAdminsCanWrite(d);
    // A path that does not exist is not trusted.
    const bool missing = !vegaflow::OnlyAdminsCanWrite(d + L"\\does_not_exist");

    SetSddl(d, L"O:BAD:(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)(A;OICI;FA;;;OW)");
    std::filesystem::remove_all(dir, ec);

    // The path is fixed: <folder of the executable>\config\config.json, whatever the working folder is.
    wchar_t exe[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::filesystem::path expected = std::filesystem::path(exe).parent_path() / L"config" / L"config.json";
    const bool path_ok = vegaflow::ConfigFilePath() == expected.wstring();

    RecordResult(admin_only && users_modify && inherit_only && null_dacl && missing && path_ok,
                 "Test 46: config.json lives next to the executable and is only trusted in an administrators-only folder",
                 "administrators only, trusted: " + Pass(admin_only) + ", Users may modify, not trusted: " +
                     Pass(users_modify) + ", inherit-only entry ignored: " + Pass(inherit_only) +
                     ", NULL DACL not trusted: " + Pass(null_dacl) + ", missing path not trusted: " + Pass(missing) +
                     ", path next to the executable: " + Pass(path_ok));
}

void Test47() {
    // -------------------------------------------------------------------------
    // Test 47: saving on a real disk replaces config.json whole and the next start trusts and reads the same file
    // -------------------------------------------------------------------------
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / L"vegaflow_config_save";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    // Stands for the executable's folder, set up like C:\ProgramData\VegaFlow by setup_tasks.ps1.
    const bool prepared = SetSddl(root.wstring(), L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;BU)");
    const std::filesystem::path file = root / L"config" / L"config.json";
    const std::wstring path = file.wstring();

    // The config folder and file do not exist yet: trusted, and the first save creates them.
    const bool missing_trusted = vegaflow::ConfigLocationTrusted(path);
    vegaflow::SafeFileSystem fs;
    vegaflow::app::ConfigStore first(fs, path, vegaflow::ConfigLocationTrusted(path));
    vegaflow::app::AppConfig cfg = first.Load();
    cfg.memory.cooldown_seconds = 120;
    std::string why;
    const bool saved = first.Save(cfg, false, &why);
    cfg.timer_forced_manual = true;
    const bool saved_again = first.Save(cfg, false, &why);
    const bool no_temp_left = !std::filesystem::exists(path + L".vegaflow.tmp", ec);

    // Next start: the saved file is still trusted (owner and inherited permissions) and reads back the same values.
    const bool still_trusted = vegaflow::ConfigLocationTrusted(path);
    vegaflow::app::ConfigStore next(fs, path, still_trusted);
    const vegaflow::app::AppConfig reread = next.Load();
    const bool same = reread.memory.cooldown_seconds == 120 && reread.timer_forced_manual && next.Report().Warning().empty();

    // A config.json that Users may modify, inside a trusted folder, makes the location untrusted.
    const bool open_file = SetSddl(path, L"O:BAD:P(A;;FA;;;BA)(A;;0x1301bf;;;BU)") && !vegaflow::ConfigLocationTrusted(path);

    SetSddl(path, L"O:BAD:(A;;FA;;;BA)");
    SetSddl((root / L"config").wstring(), L"O:BAD:(A;OICI;FA;;;BA)");
    SetSddl(root.wstring(), L"O:BAD:(A;OICI;FA;;;BA)");
    std::filesystem::remove_all(root, ec);

    RecordResult(prepared && missing_trusted && saved && saved_again && no_temp_left && still_trusted && same && open_file,
                 "Test 47: saving on a real disk replaces config.json whole and the next start trusts and reads the same file",
                 "missing folder and file trusted: " + Pass(missing_trusted) + ", saved twice: " +
                     Pass(saved && saved_again) + (why.empty() ? "" : " (" + why + ")") + ", no temporary file left: " +
                     Pass(no_temp_left) + ", still trusted after saving: " + Pass(still_trusted) +
                     ", same values on the next start: " + Pass(same) + ", file Users may modify not trusted: " +
                     Pass(open_file));
}

} // namespace vftest
