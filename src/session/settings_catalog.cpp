#include "session/settings_catalog.hpp"

namespace vegaflow {

namespace {

// Evidence: docs/measurements/verification-status.md. Nothing is Gain yet: the only measured gain is process
// isolation, which is not a row here. shortcut: evidence is per machine, not per class of machine; per class when
// there is more than one measured class. A row that bundles several writes is NoEffect only if every write was measured.
// target/change are checked against what the optimizer writes (src/optimize/graphics/*.cpp); keep them in step.
const std::vector<SettingSpec> kCatalog = {
    {"memory_guard", SettingFamily::SharedMemory, "Vigilar la memoria compartida",
     "Si la gráfica se queda sin memoria, libera la de los programas de fondo mientras hay un juego abierto.", false,
     Evidence::NotMeasured, false, true,
     "Vigilancia del presupuesto de memoria de la gráfica y de la RAM libre",
     "Al llegar al 85 % del presupuesto de la gráfica, o con menos de 1,5 GB de RAM libre, recorta la memoria de los programas de fondo, con una pausa mínima entre dos recortes",
     false, false, false},
    {"vram_reporting", SettingFamily::AmdDriver, "Informar más memoria de vídeo a los juegos",
     "Algunos juegos creen que hay muy poca memoria de vídeo y bajan las texturas.", true, Evidence::NotMeasured, true, true,
     "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000: HardwareInformation.MemorySize y HardwareInformation.qwMemorySize; HKLM\\SOFTWARE\\Microsoft\\DirectX\\<subclave>: DedicatedVideoMemory",
     "Un valor calculado según el nivel de tu equipo y el presupuesto de la gráfica; la memoria real no cambia",
     true, true},
    {"amd_driver_profile", SettingFamily::AmdDriver, "Perfil del driver de AMD para juegos",
     "Ajusta varias opciones del driver de gráficas AMD de una vez: caché de shaders, cola de cuadros y teselación.", true,
     Evidence::NotMeasured, true, false,
     "En cada adaptador AMD de HKLM\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\0000 a \\0003: ShaderCache (también en su subclave UMD), y en esa subclave UMD: SurfaceFormatReplacements, FlipQueueSize, TFQ, Tessellation_OPTION, Tessellation, Main3D_DEF, TurboSync y DisableFlipQueueOptimization",
     "ShaderCache 32 00; SurfaceFormatReplacements 1, FlipQueueSize 1, TFQ 2, Tessellation_OPTION 1, Main3D_DEF 1, TurboSync 0, DisableFlipQueueOptimization 0; Tessellation con el tope de tu nivel (8, 16, 32 o 64)",
     true, false},
    {"priority_separation", SettingFamily::Windows, "Más tiempo de CPU para la ventana activa",
     "Cambia cómo reparte Windows los turnos entre programas.", true, Evidence::NoEffect, false, false,
     "HKLM\\SYSTEM\\CurrentControlSet\\Control\\PriorityControl: Win32PrioritySeparation",
     "Con un juego abierto: 0x28 con DirectX 11 o anterior, 0x26 con DirectX 12 o Vulkan. El interruptor de esta pantalla pone 0x26. De fábrica es 2", true, true},
    {"paging_shield", SettingFamily::Windows, "Mantener el núcleo de Windows en la RAM",
     "Evita que partes de Windows se manden al disco. En una APU con poca RAM le quita memoria a la gráfica y pide reiniciar.", true, Evidence::NotMeasured, false, false,
     "HKLM\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management: DisablePagingExecutive y LargeSystemCache",
     "DisablePagingExecutive 1, LargeSystemCache 0", true, false},
    {"mpo_disabled", SettingFamily::Windows, "Desactivar la superposición de planos (MPO)",
     "Puede quitar parpadeos en algunos drivers. Microsoft no documenta este valor.", true, Evidence::NotMeasured, false, false,
     "HKLM\\SOFTWARE\\Microsoft\\Windows\\Dwm: OverlayTestMode", "5", true, false},
    {"mmcss_gaming", SettingFamily::Windows, "Prioridad de multimedia para juegos",
     "Pide a Windows que trate los juegos como tarea multimedia de prioridad alta.", true, Evidence::NotMeasured, false, false,
     "HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Multimedia\\SystemProfile: SystemResponsiveness; y en Tasks\\Games: Priority, Scheduling Category y SFIO Priority",
     "SystemResponsiveness 10, Priority 6, Scheduling Category High, SFIO Priority High", true, false},
    {"game_dvr", SettingFamily::Windows, "Apagar la grabación de Xbox Game Bar en juegos",
     "Quita la captura en segundo plano mientras juegas.", true, Evidence::NoEffect,
     false, false,
     "HKCU\\System\\GameConfigStore: GameDVR_Enabled; y en HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR: AppCaptureEnabled",
     "GameDVR_Enabled 0, AppCaptureEnabled 0",
     true, true},
    {"fse_compat", SettingFamily::Windows, "Compatibilidad: apagar las optimizaciones de pantalla completa",
     "Solo para juegos DirectX 9 o 10 que fallan a pantalla completa. En Windows 11 puede subir la latencia.",
     true, Evidence::NotMeasured, false, false,
     "HKCU\\System\\GameConfigStore: GameDVR_FSEBehaviorMode, GameDVR_HonorUserFSEBehaviorMode, GameDVR_DXGIHonorFSEWindowsCompatible y GameDVR_EFSEFeatureFlags",
     "GameDVR_FSEBehaviorMode 2, GameDVR_HonorUserFSEBehaviorMode 1, GameDVR_DXGIHonorFSEWindowsCompatible 1, GameDVR_EFSEFeatureFlags 0",
     true, false},
    {"memory_compression", SettingFamily::Windows, "Compresión de memoria de Windows",
     "Activa o apaga la compresión de memoria del sistema (el proceso MemCompression).", true, Evidence::NotMeasured, false,
     false, "MMAgent: MemoryCompression", "Se alterna entre activada y desactivada", true, false},
    {"power_profile", SettingFamily::Windows, "Perfil de energía del equipo para juegos",
     "Ajusta varias opciones del plan de energía activo para dar más margen a la gráfica.", true, Evidence::NotMeasured, false,
     false,
     "Plan de energía activo (enchufado y con batería): administración de energía del vínculo PCI Express, modo de refuerzo del procesador, preferencia de rendimiento energético, frecuencia máxima del procesador, aparcado de núcleos y acelerador del procesador",
     "Vínculo PCI Express Desactivado, modo de refuerzo 4, preferencia de rendimiento 25, frecuencia máxima según el nivel de tu equipo, aparcado de núcleos 50, acelerador 100",
     true, true},
};

} // namespace

const std::vector<SettingSpec>& SettingsCatalog() { return kCatalog; }

const SettingSpec* FindSetting(const std::string& id) {
    for (const auto& s : kCatalog) {
        if (id == s.id) return &s;
    }
    return nullptr;
}

bool Applies(const SettingSpec& spec, const DeviceFacts& device) {
    if (spec.needs_amd && (device.vendor != GpuVendor::AMD || !device.driver_tweaks_allowed)) return false;
    if (spec.needs_integrated && device.connection != GpuConnection::Integrated) return false;
    return true;
}

bool IsRecommended(const SettingSpec& spec) { return spec.evidence == Evidence::Gain; }

bool IsNotRecommended(const SettingSpec& spec) { return spec.evidence == Evidence::Loss; }

std::vector<FamilyView> VisibleFamilies(const DeviceFacts& device, bool show_experimental) {
    struct Header {
        SettingFamily family;
        const char* title;
        const char* scope;
    };
    static const Header kHeaders[] = {
        {SettingFamily::SharedMemory, "Memoria compartida con la gráfica", "Solo gráficas integradas"},
        {SettingFamily::AmdDriver, "Driver de AMD", "Solo gráficas AMD"},
        {SettingFamily::Windows, "Windows", "Todos los equipos"},
    };
    std::vector<FamilyView> out;
    for (const auto& h : kHeaders) {
        FamilyView view;
        view.family = h.family;
        view.title = h.title;
        view.scope = h.scope;
        for (const auto& s : kCatalog) {
            if (s.family != h.family || !Applies(s, device)) continue;
            if (s.experimental && !show_experimental) continue;
            view.rows.push_back(&s);
        }
        if (!view.rows.empty()) out.push_back(std::move(view));
    }
    return out;
}

std::string ChangeDescription(const SettingSpec& spec) {
    if (!spec.undoable) {
        return std::string(spec.target) + ". " + spec.change + ". No guarda ningún valor original: es una acción puntual, no hay nada que deshacer.";
    }
    return std::string("Cambia ") + spec.target + " a: " + spec.change +
           ". Se guarda el valor original y se puede deshacer desde Historial.";
}

} // namespace vegaflow
