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

inline constexpr const wchar_t* kHistoryTitle = L"Historial";
inline constexpr const wchar_t* kHistorySubtitle = L"Todo lo que VegaFlow cambió en tu equipo, con el valor que había antes.";
inline constexpr const wchar_t* kHistoryRestoreAll = L"Devolver todo a como estaba";
inline constexpr const wchar_t* kUndo = L"Deshacer";
inline constexpr const wchar_t* kBefore = L"Antes: ";
inline constexpr const wchar_t* kNow = L"  ·  Ahora: ";
inline constexpr const wchar_t* kNoUndoNeeded = L"Sin acción";

inline constexpr const wchar_t* kConfigTitle = L"Configuración";
inline constexpr const wchar_t* kConfigSubtitle =
    L"Cómo se comporta VegaFlow. Los valores de fábrica son los que se probaron en el equipo de pruebas. Cada cambio se guarda al momento en config.json.";
inline constexpr const wchar_t* kConfigReset = L"Restablecer esta configuración";
inline constexpr const wchar_t* kConfigBackupsTitle = L"Copias y restauración";
inline constexpr const wchar_t* kConfigRestoreAllName = L"Devolver todo a como estaba";
inline constexpr const wchar_t* kConfigRestoreAllHelp = L"Registro, plan de energía y archivos de juegos, con las copias guardadas.";
inline constexpr const wchar_t* kConfigRestoreAllButton = L"Devolver todo";
inline constexpr const wchar_t* kConfigOn = L"Activado";
inline constexpr const wchar_t* kConfigOff = L"Apagado";

inline constexpr const wchar_t* kProgTitle = L"Programas";
inline constexpr const wchar_t* kProgSubtitle =
    L"Elige qué hace VegaFlow con cada tipo de programa mientras juegas. Los programas se reconocen por su nombre y por la carpeta donde están instalados.";
inline constexpr const wchar_t* kProgLegend =
    L"Frenar: ahorro de energía, disco en prioridad baja y un núcleo apartado.   Solo ahorro: gasta menos CPU pero no se le quita disco ni núcleos; bueno para música.   No tocar: sigue igual que siempre; para grabar, transmitir o hablar.";
inline constexpr const wchar_t* kProgCategoriesTitle = L"Por tipo de programa";
inline constexpr const wchar_t* kProgExceptionsTitle = L"Excepciones";
inline constexpr const wchar_t* kProgExceptionsHelp = L"Un programa concreto que quieres tratar distinto de su tipo.";
inline constexpr const wchar_t* kProgExceptionRowHelp = L"Siempre se trata con la acción de la derecha.";
inline constexpr const wchar_t* kProgAddException = L"Añadir excepción";
inline constexpr const wchar_t* kProgAddExceptionField = L"Nombre del programa, por ejemplo telegram.exe";
inline constexpr const wchar_t* kProgGamesTitle = L"Juegos que no se detectan solos";
inline constexpr const wchar_t* kProgGamesHelp = L"Los de Steam se añaden solos. Aquí van los de otras tiendas o sin tienda.";
inline constexpr const wchar_t* kProgAddGame = L"Añadir un juego";
inline constexpr const wchar_t* kProgAddGameField = L"Nombre del juego, por ejemplo juego.exe";
inline constexpr const wchar_t* kProgRemove = L"Quitar";
inline constexpr const wchar_t* kProgSelected = L", seleccionado";
inline constexpr const wchar_t* kProgFooter = L"Los procesos de Windows nunca se tocan, y eso no se puede cambiar.";

inline constexpr const wchar_t* kEquipTitle = L"Tu equipo";
inline constexpr const wchar_t* kEquipThreadsTitle = L"Cómo se reparten los núcleos mientras juegas";
inline constexpr const wchar_t* kEquipLegendGame = L"El juego puede usar estos hilos";
inline constexpr const wchar_t* kEquipLegendBackground = L"Aquí se envían los programas de fondo";
inline constexpr const wchar_t* kEquipLegendSystem = L"Lo decide Windows";
inline constexpr const wchar_t* kEquipGraphicsTitle = L"Gráficas";
inline constexpr const wchar_t* kEquipTierTitle = L"Nivel de protección de memoria";
inline constexpr const wchar_t* kEquipYours = L" (el tuyo)";

inline constexpr const wchar_t* kReqTitle = L"Requisitos y advertencias";
inline constexpr const wchar_t* kReqBack = L"← Volver a Configuración";
inline constexpr const wchar_t* kReqNeedsTitle = L"Qué necesita";
inline constexpr const wchar_t* kReqWarnTitle = L"Conviene saber";
inline constexpr const wchar_t* kConfigRequirements = L"Requisitos y advertencias";
inline constexpr const wchar_t* kConfigRequirementsButton = L"Ver";
inline constexpr const wchar_t* kConfigRequirementsHelp = L"Qué necesita VegaFlow, si tu equipo lo cumple, y qué conviene saber.";


// Inicio
inline constexpr const wchar_t* kHomeTitle = L"Inicio";
inline constexpr const wchar_t* kHomeEquipmentTitle = L"Tu equipo, según VegaFlow";
inline constexpr const wchar_t* kHomeEquipmentButton = L"Ver todo el detalle";
inline constexpr const wchar_t* kHomePurgeButton = L"Liberar memoria ahora";
inline constexpr const wchar_t* kHomeBackgroundTitle = L"Programas en segundo plano";
inline constexpr const wchar_t* kHomeProgramsButton = L"Elegir qué se frena";
inline constexpr const wchar_t* kHomeTroubleTitle = L"¿Algo va mal?";
inline constexpr const wchar_t* kHomeTroubleBody =
    L"Si un juego o Windows se comporta raro después de un cambio de VegaFlow, devuelve todo a como estaba. Si quieres ayuda, copia el informe: "
    L"es texto con tu equipo y los últimos cambios; léelo antes de enviarlo.";
inline constexpr const wchar_t* kHomeUndoAllButton = L"Devolver todo a como estaba";
inline constexpr const wchar_t* kHomeReportButton = L"Copiar informe";
inline constexpr const wchar_t* kTrayReport = L"Copiar informe";
inline constexpr const wchar_t* kHomeReportCopied = L"Informe copiado: pégalo donde pidas ayuda. Revísalo antes de enviarlo.";
inline constexpr const wchar_t* kHomeReportFailed = L"No se pudo copiar el informe al portapapeles.";
// E2.7: pause offer, diagnosis, exit notice and welcome (the dialogs are the window's own, see ui/choice_dialog.hpp)
inline constexpr const wchar_t* kHomePauseButton = L"Pausar VegaFlow en este juego";
inline constexpr const wchar_t* kHomeDoctorButton = L"Diagnóstico";
inline constexpr const wchar_t* kDoctorTitle = L"Diagnóstico de VegaFlow";
inline constexpr const wchar_t* kDoctorCopy = L"Copiar informe";
inline constexpr const wchar_t* kDoctorClose = L"Cerrar";
inline constexpr const wchar_t* kExitTitle = L"Cerrar VegaFlow";
inline constexpr const wchar_t* kExitRestore = L"Devolver todo a como estaba";
inline constexpr const wchar_t* kExitKeep = L"Dejarlos";
inline constexpr const wchar_t* kExitCancel = L"Cancelar";
inline constexpr const wchar_t* kWelcomeTitle = L"Bienvenido a VegaFlow";
inline constexpr const wchar_t* kWelcomeBody =
    L"VegaFlow detecta tus juegos y le da prioridad al juego mientras está abierto. Cada cambio que hace queda en Historial y puedes deshacerlo.\r\n\r\n"
    L"Si algo va mal, usa «Devolver todo a como estaba» en Inicio o el atajo Ctrl+Alt+Mayús+R.";
inline constexpr const wchar_t* kWelcomeButton = L"Entendido";
// F4.4: config.json broken; the body is ConfigLoadReport::Warning(), then this question
inline constexpr const wchar_t* kConfigProblemTitle = L"config.json necesita tu decisión";
inline constexpr const wchar_t* kConfigProblemAsk = L"¿Qué quieres hacer? «Valores de fábrica» guarda antes una copia junto al archivo (.vegaflow.bak).";
inline constexpr const wchar_t* kConfigProblemAskOpenOnly = L"VegaFlow no puede guardar una copia de este archivo, así que no lo reemplaza. Puedes abrirlo y corregirlo.";
inline constexpr const wchar_t* kConfigFactoryButton = L"Valores de fábrica";
inline constexpr const wchar_t* kConfigOpenButton = L"Abrir el archivo";
inline constexpr const wchar_t* kConfigLaterButton = L"Decidir después";
inline constexpr const wchar_t* kHotkeyTitle = L"Atajo de pánico";

// Juegos
inline constexpr const wchar_t* kGamesTitle = L"Juegos";
inline constexpr const wchar_t* kGamesSubtitle = L"Tus juegos de Steam y lo que VegaFlow escribe para cada uno. Aquí puedes aplicar un perfil, volver al archivo original o excluir un juego.";
inline constexpr const wchar_t* kGamesRescan = L"Buscar juegos de nuevo";
inline constexpr const wchar_t* kGamesShow = L"Ver cambios";
inline constexpr const wchar_t* kGamesHide = L"Ocultar cambios";
inline constexpr const wchar_t* kGamesApply = L"Aplicar perfil";
inline constexpr const wchar_t* kGamesRestore = L"Volver al archivo original";
inline constexpr const wchar_t* kGamesBad = L"Me fue mal en este juego";
inline constexpr const wchar_t* kGamesInclude = L"Volver a incluir";
inline constexpr const wchar_t* kGamesDxvkRemove = L"Quitar DXVK";
inline constexpr const wchar_t* kGamesFile = L"Archivo que cambia: ";
inline constexpr const wchar_t* kGamesChanges = L"Lo que se escribe, línea por línea";
inline constexpr const wchar_t* kGamesTooMany = L"Se muestran los primeros 200 juegos.";

} // namespace vegaflow::ui::text
