#include "session/requirements_screen.hpp"

namespace vegaflow::session {

namespace {

constexpr uint32_t kWindows10_1709 = 16299; // Windows 10 version 1709: the first build with the process power-throttling (EcoQoS) call

bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

} // namespace

const char* StateLabel(RequirementState state) {
    switch (state) {
        case RequirementState::Meets: return "Cumple";
        case RequirementState::Missing: return "Falta";
        case RequirementState::Limited: return "Limitado";
        case RequirementState::Unknown: return "No se pudo leer";
        case RequirementState::Optional: return "Opcional";
    }
    return "";
}

RequirementsScreenModel BuildRequirementsScreen(const SystemInfoSnapshot& s, bool elevated, bool steam_found) {
    RequirementsScreenModel m;

    RequirementRow win{"windows", "Windows", "", RequirementState::Unknown};
    const std::string version = s.os_kernel.full_version_string.empty() ? "versión no leída" : s.os_kernel.full_version_string;
    if (s.os_kernel.build_number == 0) {
        win.detail = "Hace falta Windows 10 versión 1709 o posterior, de 64 bits. No se pudo leer qué versión tienes.";
    } else if (s.os_kernel.build_number < kWindows10_1709) {
        // Limited, not Missing: that VegaFlow starts on 1709 is not measured (docs/measurements/verification-status.md), and the build is
        // below the one that has the energy-saving call, so some things will not work.
        win.state = RequirementState::Limited;
        win.detail = "Hace falta Windows 10 versión 1709 o posterior, de 64 bits. Tienes " + version + ", que es anterior: VegaFlow no se probó ahí y le faltan llamadas de Windows que usa.";
    } else {
        win.state = RequirementState::Meets;
        win.detail = "Windows 10 versión 1709 o posterior, de 64 bits. Tienes " + version + ". Con Windows 11 el modo ahorro de los programas de fondo es más completo.";
    }
    m.rows.push_back(win);

    RequirementRow perm{"permissions", "Permisos", "", elevated ? RequirementState::Meets : RequirementState::Missing};
    perm.detail = elevated ? "VegaFlow se ejecuta como administrador, que hace falta para cambiar prioridades y ajustes del sistema."
                           : "Sin permisos de administrador VegaFlow no puede cambiar la mayoría de las prioridades ni de los ajustes del sistema. Vuelve a abrirlo como administrador.";
    m.rows.push_back(perm);

    RequirementRow cpu{"processor", "Procesador", "", RequirementState::Unknown};
    const std::string& arch = s.os_kernel.system_arch;
    if (arch.empty() || Has(arch, "No se pudo")) {
        cpu.detail = "Hace falta un procesador de 64 bits (x64). No se pudo leer cuál tienes.";
    } else if (arch == "x64") {
        cpu.state = RequirementState::Meets;
        cpu.detail = "Procesador de 64 bits (x64).";
    } else if (Has(arch, "ARM")) {
        cpu.state = RequirementState::Limited;
        cpu.detail = "Tu procesador es " + arch + ": VegaFlow corre emulado y no aplica los ajustes de driver de la gráfica.";
    } else {
        cpu.state = RequirementState::Missing;
        cpu.detail = "Hace falta un procesador de 64 bits (x64). Tu equipo informa: " + arch + ".";
    }
    m.rows.push_back(cpu);

    RequirementRow gpu{"graphics", "Gráfica", "", RequirementState::Unknown};
    const std::string& conn = s.gpu.connection;
    if (conn.empty() || Has(conn, "No se pudo")) {
        gpu.detail = "Sirve cualquiera, pero no se pudo leer cuál tienes: solo se ofrece lo que vale para todas.";
    } else {
        gpu.state = RequirementState::Meets;
        gpu.detail = "Sirve cualquiera. La tuya: " + (s.gpu.adapter_name.empty() ? std::string("sin nombre") : s.gpu.adapter_name) + " (" + conn +
                     "). Lo que se puede activar depende de si es integrada, dedicada o externa.";
    }
    m.rows.push_back(gpu);

    RequirementRow steam{"steam", "Steam", "", steam_found ? RequirementState::Meets : RequirementState::Optional};
    steam.detail = steam_found ? "Se encontraron juegos de Steam: se preparan solos."
                               : "No se encontraron juegos de Steam. Es opcional: los juegos de otras tiendas se añaden a mano en Programas.";
    m.rows.push_back(steam);

    m.warnings = {
        "El resultado depende de cada equipo. VegaFlow no promete una mejora concreta, y marca como recomendado solo lo que se midió en equipos parecidos al tuyo.",
        "Si usas otro programa de optimización (por ejemplo, el del fabricante de tu portátil), los dos pueden cambiar los mismos ajustes.",
        "Las funciones experimentales pueden no hacer nada o causar efectos raros, como parpadeos.",
        "Los cambios en el registro, el plan de energía y los archivos de juegos guardan el valor original y se deshacen desde Historial, o con «Devolver todo» en Configuración. Vaciar la memoria no se puede deshacer: no hay nada que devolver.",
    };
    return m;
}

} // namespace vegaflow::session
