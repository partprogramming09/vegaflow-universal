#pragma once
// User-facing texts of the new screens, in one place so they can be translated without touching the drawing code.
// Plain Spanish; technical names only as secondary detail (the (i) text). No text here promises a gain.

namespace vegaflow::ui::text {

inline constexpr const wchar_t* kSettingsTitle = L"Ajustes del sistema";
inline constexpr const wchar_t* kSettingsIntro =
    L"Cada cambio guarda el valor original y se puede deshacer desde Historial. «Sin medir» quiere decir que no hay una "
    L"medición de este ajuste en un equipo como el tuyo.";
inline constexpr const wchar_t* kSettingsRecommended =
    L"«Recomendado» significa que VegaFlow lo activaría en un equipo como el tuyo. El resultado depende de cada equipo, "
    L"por eso no se promete ninguna mejora: pulsa ⓘ en cada ajuste para ver exactamente qué cambia.";
inline constexpr const wchar_t* kYourGpu = L"Tu gráfica";
inline constexpr const wchar_t* kGpuIntegrated = L"integrada (comparte la RAM del equipo)";
inline constexpr const wchar_t* kGpuDiscrete = L"dedicada (tiene memoria propia)";
inline constexpr const wchar_t* kGpuExternal = L"externa (por cable)";
inline constexpr const wchar_t* kGpuUnknown = L"no identificada: solo se muestran los ajustes que sirven en cualquier equipo";
inline constexpr const wchar_t* kDriverBlocked = L"VegaFlow no toca el driver de esta gráfica en este equipo.";
inline constexpr const wchar_t* kShowExperimental = L"Mostrar funciones experimentales";
inline constexpr const wchar_t* kExperimentalTitle = L"Funciones experimentales a la vista";
inline constexpr const wchar_t* kExperimentalBody =
    L"No están probadas en equipos como el tuyo o Microsoft no las documenta. Pueden no hacer nada o causar efectos raros "
    L"(parpadeos, tirones). Se pueden deshacer desde Historial.";
inline constexpr const wchar_t* kExperimentalChip = L"Experimental";
inline constexpr const wchar_t* kWhatChanges = L"Qué cambia exactamente";
inline constexpr const wchar_t* kOn = L"Activado";
inline constexpr const wchar_t* kFactory = L"De fábrica";
inline constexpr const wchar_t* kAutomatic = L"Automático";
inline constexpr const wchar_t* kOnlyApplicable = L"Solo se muestran los ajustes que sirven en este equipo.";
inline constexpr const wchar_t* kNothingToShow = L"No hay ajustes que mostrar en este equipo. Activa las funciones experimentales para ver más.";

} // namespace vegaflow::ui::text
