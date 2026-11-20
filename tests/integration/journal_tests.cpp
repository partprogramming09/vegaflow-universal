// Integration tests: real disk FileChangeJournal + WindowsSystemUndo restoring real HKCU registry & game file.
// Called by tests/test_main.cpp; see tests/test_support.hpp.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>

#include "optimize/graphics/graphics_optimizer.hpp"
#include "session/restore.hpp"
#include "windows/registry.hpp"
#include "windows/change_journal.hpp"
#include "ports/clock.hpp"
#include "windows/safe_file_system.hpp"
#include "../test_support.hpp"

namespace vftest {

namespace {

std::string Pass(bool b) { return b ? "PASS" : "FAIL"; }

} // namespace

void Test50() {
    using namespace vegaflow;
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / L"vegaflow_journal_integration";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const std::wstring jpath = (root / L"change_journal.vfj").wstring();
    const std::wstring ini_path = (root / L"Engine.ini").wstring();
    const std::wstring bak_path = ini_path + L".vegaflow.bak";
    const std::wstring reg_subkey = L"Software\\VegaFlow\\TestJournalIntegration";
    const std::wstring reg_val_name = L"ProbeOverlay";

    RegDeleteTreeW(HKEY_CURRENT_USER, reg_subkey.c_str());

    SafeFileSystem fs;
    SteadyClock clock;
    WindowsSystemUndo undo(fs);

    // Prepare real HKCU registry value (originally 123) and real Engine.ini + backup (originally "OrigLine=1\n").
    HKEY hk = nullptr;
    bool reg_setup_ok = false;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, reg_subkey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_READ | KEY_SET_VALUE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        const DWORD orig = 123;
        reg_setup_ok = (RegSetValueExW(hk, reg_val_name.c_str(), 0, REG_DWORD,
                                       reinterpret_cast<const BYTE*>(&orig), sizeof(orig)) == ERROR_SUCCESS);
        RegCloseKey(hk);
    }
    const std::string orig_ini = "[SystemSettings]\nOrigLine=1\n";
    const bool file_setup_ok = fs.Write(ini_path, orig_ini, false) && fs.Copy(ini_path, bak_path, true, false);

    // Session 1 applies real mutations via journal and "crashes" (no EndSessionClean)
    {
        FileChangeJournal j1(fs, clock, jpath);
        j1.Load();
        j1.BeginSession();

        JournalEntry e_reg;
        e_reg.kind = JournalTargetKind::RegistryValue;
        e_reg.target = "HKCU\\Software\\VegaFlow\\TestJournalIntegration\\ProbeOverlay";
        e_reg.reason = "Integration test reg";
        e_reg.before_value = "123";
        e_reg.after_value = "999";
        e_reg.hive = 1; // HKCU
        e_reg.subkey = reg_subkey;
        e_reg.value_name = reg_val_name;
        e_reg.orig_present = true;
        e_reg.orig_type = REG_DWORD;
        // Little-endian 123 = 0x7B 00 00 00
        e_reg.orig_data_hex = "7b000000";
        const uint64_t r_id = j1.RecordPending(e_reg);

        if (RegOpenKeyExW(HKEY_CURRENT_USER, reg_subkey.c_str(), 0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
            const DWORD mutated = 999;
            RegSetValueExW(hk, reg_val_name.c_str(), 0, REG_DWORD,
                           reinterpret_cast<const BYTE*>(&mutated), sizeof(mutated));
            RegCloseKey(hk);
        }
        j1.MarkCommitted(r_id);

        JournalEntry e_file;
        e_file.kind = JournalTargetKind::GameConfigFile;
        e_file.target = "GameFile:Engine.ini";
        e_file.reason = "Integration test INI";
        e_file.before_value = "OrigLine=1";
        e_file.after_value = "MutatedLine=2";
        e_file.file_path = ini_path;
        e_file.backup_path = bak_path;
        const uint64_t f_id = j1.RecordPending(e_file);
        fs.Write(ini_path, "[SystemSettings]\nMutatedLine=2\n", false);
        j1.MarkCommitted(f_id);
        // Simulate abrupt kill: return without EndSessionClean()
    }

    // Session 2 reloads real disk journal, sees unclean exit, and restores both real targets!
    FileChangeJournal j2(fs, clock, jpath);
    const bool loaded = j2.Load();
    const auto eval = session::EvaluateStartupJournal(j2);
    const auto rep = session::RestoreAllRecordedChanges(j2, undo, false);

    DWORD restored_dword = 0;
    DWORD type = 0, size = sizeof(restored_dword);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, reg_subkey.c_str(), 0, KEY_READ, &hk) == ERROR_SUCCESS) {
        RegQueryValueExW(hk, reg_val_name.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(&restored_dword), &size);
        RegCloseKey(hk);
    }
    std::string reread_ini;
    fs.Read(ini_path, reread_ini);

    const bool real_restore_ok = reg_setup_ok && file_setup_ok && loaded &&
                                 eval.previous_unclean_exit && eval.unclosed_or_unreverted_count == 2 &&
                                 rep.all_succeeded && rep.reverted_count == 2 && rep.total_count == 2 &&
                                 rep.summary_message == "Se devolvieron 2 de 2." &&
                                 restored_dword == 123 && reread_ini == orig_ini && !fs.Exists(bak_path);

    // Cleanup registry & temp dir
    RegDeleteTreeW(HKEY_CURRENT_USER, reg_subkey.c_str());
    std::filesystem::remove_all(root, ec);

    RecordResult(real_restore_ok,
                 "Test 50: Real disk FileChangeJournal + WindowsSystemUndo restores HKCU registry and game INI after crash",
                 "setup: " + Pass(reg_setup_ok && file_setup_ok) + ", restored 2/2: " + Pass(rep.all_succeeded) +
                     ", HKCU==123: " + Pass(restored_dword == 123) + ", INI exact: " + Pass(reread_ini == orig_ini));
}

// E1.4: the real Flip write keeps the user's GPU choice and Layers flags, journals once, and the journal puts both back exactly.
void Test95() {
    using namespace vegaflow;
    std::error_code ec;
    const std::filesystem::path root = std::filesystem::temp_directory_path(ec) / L"vegaflow_flip_journal";
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    const std::wstring exe = L"C:\\Games\\VegaFlowTest\\FlipJournal.exe";
    const wchar_t* ugp_key = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
    const wchar_t* layers_key = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    const std::wstring ugp_orig = L"GpuPreference=1;AutoHDREnable=1;";
    const std::wstring layers_orig = L"~ RUNASADMIN";

    WindowsRegistryPort reg;
    const bool seeded = reg.WriteValue(RegHive::CurrentUser, ugp_key, exe, RegValue::MakeString(ugp_orig), false) &&
                        reg.WriteValue(RegHive::CurrentUser, layers_key, exe, RegValue::MakeString(layers_orig), false);

    SafeFileSystem fs;
    SteadyClock clock;
    WindowsSystemUndo undo(fs);
    FileChangeJournal journal(fs, clock, (root / L"change_journal.vfj").wstring());
    journal.Load();
    journal.BeginSession();
    ChangeJournal* previous = GetActiveChangeJournal();
    SetActiveChangeJournal(&journal);

    DxgiFlipTweaks flip(reg);
    const bool applied = flip.ApplyFlipModelUpgrade(exe, false) && flip.ApplyFlipModelUpgrade(exe, false);
    SetActiveChangeJournal(previous);

    RegValue ugp_now, layers_now;
    reg.ReadValue(RegHive::CurrentUser, ugp_key, exe, ugp_now);
    reg.ReadValue(RegHive::CurrentUser, layers_key, exe, layers_now);
    const bool merged = ugp_now.str == ugp_orig + L"SwapEffectUpgradeEnable=1;" && layers_now.str == L"~ RUNASADMIN HIGHDPIAWARE";
    const bool one_entry = journal.Entries().size() == 1;

    const auto rep = session::RestoreAllRecordedChanges(journal, undo, false);
    RegValue ugp_back, layers_back;
    reg.ReadValue(RegHive::CurrentUser, ugp_key, exe, ugp_back);
    reg.ReadValue(RegHive::CurrentUser, layers_key, exe, layers_back);
    const bool reverted = rep.all_succeeded && ugp_back.str == ugp_orig && layers_back.str == layers_orig;

    reg.DeleteValue(RegHive::CurrentUser, ugp_key, exe, false);
    reg.DeleteValue(RegHive::CurrentUser, layers_key, exe, false);
    std::filesystem::remove_all(root, ec);

    RecordResult(seeded && applied && merged && one_entry && reverted,
                 "Test 95: Real Flip write keeps user prefs and flags, journals once, and reverts exactly",
                 "seed: " + Pass(seeded) + ", applied: " + Pass(applied) + ", merged: " + Pass(merged) +
                     ", entries=" + std::to_string(journal.Entries().size()) + ", reverted: " + Pass(reverted));
}

} // namespace vftest
