// Unit test for E1.5: a RegBackup record can only be restored into a key VegaFlow edits, in the hive that holds it.

#include "windows/reg_restore_scope.hpp"

#include "../test_support.hpp"

namespace vftest {

void Test89() {
    using vegaflow::IsAllowedRestoreTarget;
    const bool real_targets =
        IsAllowedRestoreTarget(0, 0, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl") &&
        IsAllowedRestoreTarget(1, 1, L"Software\\Microsoft\\DirectX\\UserGpuPreferences") &&
        IsAllowedRestoreTarget(0, 0, L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0001") &&
        IsAllowedRestoreTarget(1, 1, L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers");
    // The attack: a record planted in HKCU that names HKLM.
    const bool cross_hive_refused = !IsAllowedRestoreTarget(0, 1, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl");
    const bool unknown_key_refused = !IsAllowedRestoreTarget(0, 0, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run") &&
                                     !IsAllowedRestoreTarget(0, 0, L"SYSTEM\\CurrentControlSet\\Services\\Evil");
    const bool lookalike_refused = !IsAllowedRestoreTarget(0, 0, L"SOFTWARE\\Microsoft\\DirectXEvil") &&
                                   !IsAllowedRestoreTarget(1, 1, L"System\\GameConfigStoreX");
    // A key VegaFlow edits in HKLM must not be restorable in HKCU (and the reverse), and '/' is a literal in the registry.
    const bool wrong_hive_refused = !IsAllowedRestoreTarget(1, 1, L"SYSTEM\\CurrentControlSet\\Control\\PriorityControl") &&
                                    !IsAllowedRestoreTarget(0, 0, L"System\\GameConfigStore");
    const bool slash_refused = !IsAllowedRestoreTarget(1, 1, L"Software/Microsoft/DirectX/UserGpuPreferences");
    const bool dotdot_refused = !IsAllowedRestoreTarget(0, 0, L"SOFTWARE\\Microsoft\\DirectX\\..\\Windows\\CurrentVersion\\Run");
    const bool empty_and_bad_hive_refused = !IsAllowedRestoreTarget(0, 0, L"") && !IsAllowedRestoreTarget(2, 2, L"System\\GameConfigStore");

    const bool ok = real_targets && cross_hive_refused && wrong_hive_refused && slash_refused && unknown_key_refused && lookalike_refused && dotdot_refused &&
                    empty_and_bad_hive_refused;
    RecordResult(ok, "Test 89: RegBackup restore is limited to VegaFlow's own keys in the record's hive",
                 std::string("Real=") + (real_targets ? "OK" : "FAIL") + ", CrossHive=" + (cross_hive_refused ? "OK" : "FAIL") +
                     ", Unknown=" + (unknown_key_refused ? "OK" : "FAIL") + ", Lookalike=" + (lookalike_refused ? "OK" : "FAIL") +
                     ", WrongHive=" + (wrong_hive_refused ? "OK" : "FAIL") + ", Slash=" + (slash_refused ? "OK" : "FAIL") + ", DotDot=" + (dotdot_refused ? "OK" : "FAIL") + ", Empty=" + (empty_and_bad_hive_refused ? "OK" : "FAIL"));
}

} // namespace vftest
