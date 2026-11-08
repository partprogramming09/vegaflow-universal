#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>

namespace vegaflow {

struct PrivilegeStatus {
    bool is_elevated = false;
    bool profile_single_process = false; // SeProfileSingleProcessPrivilege
    bool increase_base_priority = false; // SeIncreaseBasePriorityPrivilege
    bool debug_privilege = false;        // SeDebugPrivilege (opcional)
};

class PrivilegeManager {
public:
    static PrivilegeStatus EnableRequiredPrivileges(bool enable_debug = false);
    static bool IsProcessElevated();

    // Relanza el ejecutable actual solicitando elevación UAC ("runas") con los argumentos indicados.
    // Si dry_run == true, solo verifica que puede obtener la ruta del ejecutable y devuelve true sin abrir ventana UAC.
    static bool RelaunchAsAdmin(const std::wstring& arguments = L"", bool dry_run = false);

private:
    static bool EnablePrivilege(HANDLE token, const wchar_t* privilege_name);
};

} // namespace vegaflow
