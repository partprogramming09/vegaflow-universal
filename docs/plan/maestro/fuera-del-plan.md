# Fuera del plan: descartes, ideas y después

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

## 6. Lo que se descarta y por qué

- **Lector de cabecera `.exe`** (saber si un juego es de 32 o 64 bits). No tiene fallo de memoria: un archivo raro solo hace que use el valor por defecto. Además, hoy no se ejecuta, porque los DLL de DXVK no vienen en el repo. Sí se corrigen dos cosas en C++ por si se vuelve a usar: comprobar que la copia de seguridad de los DLL salió bien antes de reemplazarlos, y no decir "éxito" en la prueba en seco si faltan los DLL.
- ~~**Leer `config.json` en Rust.**~~ **Revertido el 2026-11-10:** se lee en Rust con un lector propio, sin librería externa (fase 1b). Lo que decía antes: si se cambia el lector, se hace en C++ con json.h (sección 3c).
- **Del plan universal antiguo:** el "Tier 5 eGPU" (la conexión es un dato aparte, fase 3), `r.ShaderPipelineCache.AsyncCompileRate` (no documentada), `r.GTSyncType=1` por defecto (no medido), y la "prueba 32" única para todo el hardware (las pruebas se reparten por fase con sus ejemplos).
- **Cifras sin fuente del plan antiguo,** como los ~75 ns de latencia entre CCD: no se usan para decidir nada.
- **Resto del perfil eGPU antiguo:** `FlipQueueSize = 1` ya lo aplica la familia de driver AMD a cualquier GPU AMD (fase 8), no es propio de la eGPU. "Dejar el núcleo 0 libre para las interrupciones del controlador" no tiene fuente ni medición: no se adopta. El valor `40` de `MaxNumTexturesToStreamPerFrame` tampoco tiene fuente: se decide al comprobar la variable en el código del motor (fase 3). `PCIe ASPM = 0` ya se aplica hoy (`power_mode.cpp:82`). Una eGPU por **OCuLink** probablemente no aparece como extraíble en DXCore: queda como "dedicada", sin verificar.
- **Overlay de FPS y detección de cuello de botella** (pilares 2 y 4 antiguos): VegaFlow no es un medidor (decisión del 2026-11-09).
- **Auto-ajuste FSR/NIS del Pilar 2 antiguo:** la función de escalado de imagen ya se quitó del proyecto; no vuelve con este plan.

## Pendiente heredado de la fase 0

- **DLL de DXVK-GPLAsync:** ya no están en el repo. Falta decidir de dónde salen (lo previsto era un `scripts/fetch_dxvk.ps1` con versión y sha256 fijos, si existe una descarga estable de esa variante parcheada). Hasta entonces, `assets/dxvk/README.md` explica cómo colocarlas a mano. Investigado el 2026-11-09 en [`dxvk-descarga.md`](../../investigacion/dxvk-descarga.md): existe una URL de GPLAsync fijada a un commit, sin checksum ni licencia publicados; decisión pendiente del usuario.
- `scripts/remote/archive/` no tenía nada versionado (solo un `__pycache__` local, borrado).

## Pendiente heredado de la fase 1

- Un hardlink dentro de una carpeta permitida hacia un archivo protegido no se detecta (la ruta del handle es la del enlace). No se midió si un usuario sin permisos sobre el archivo puede crear ese hardlink.
- Las carpetas permitidas se reúnen y se resuelven en cada validación; el coste no se midió. Guardarlas en caché reabriría la carrera con junctions.
- ZZZ: `CanModifyGameDir` dice que sí porque los archivos de anti-cheat están en subcarpetas (solo se mira hacia arriba). Es anterior a la fase 1; el preset sigue bloqueado por `has_anticheat`.
- ~~La prueba de que `run_cycle` pasa la política estática~~ se implementó en la fase 3 (`RunGameSessionCycle` + Prueba 51).

## Pendiente heredado de la fase 1b

- `ACCESS_SYSTEM_SECURITY` no cuenta como permiso de escritura (solo cambia la auditoría y exige un privilegio de administrador).
- Si la carpeta del ejecutable hereda permisos de escritura a otras cuentas, la carpeta `config` o el archivo creados después no pasan la comprobación y se usan los valores de fábrica (falla en cerrado).
- Las carpetas por encima de la del ejecutable no se comprueban (quien pudiera cambiarlas también podría cambiar el ejecutable).
- Con el ejecutable en una ruta de más de 260 caracteres, SandboxGuard y `config.json` ya usan la misma `ExecutableDirectory()`, que admite rutas largas (2026-11-10); sin `LongPathsEnabled` Windows no arranca el `.exe` desde una ruta así, así que no está probado en el laptop.
- Anterior a la fase: si otra cuenta crea `C:\ProgramData\VegaFlow` antes de instalar y le añade un permiso explícito, `setup_tasks.ps1` no lo quita; podría cambiar el ejecutable.
- Fase 5: la lista de procesos de Windows existe en Rust (`config_rules.rs`) y en C++ (`kExcludedProcesses` de `process_detector.cpp`); unificarlas. Además, el paso 3 de `DetectActiveGame` construye un juego aunque `InspectProcess` lo excluya.
- El aviso de `config.json` ignorado o con valores rechazados solo sale por stderr en el modo daemon (oculto cuando lo lanza `ab_profile.ps1`). Redirigirlo a un log necesita comprobar en el escritorio que el daemon sigue sin ventana.
- Sospechas de la revisión sin confirmar: una excepción de C++ (`bad_alloc`) dentro del callback que llama Rust cruzaría frames `extern "C"`; `icacls /setowner /T /C` en `setup_tasks.ps1` puede devolver 0 aunque falle un archivo y no está comprobado si sigue junctions.
- Fase G: con un `config.json` corrupto, con un valor rechazado o de versión más nueva, la GUI solo avisa; el botón para que la persona decida (restaurar valores de fábrica o abrir el archivo) es de la fase G. Tras corregir el archivo hay que reiniciar VegaFlow.

## Pendiente heredado de la fase 2

- Prueba interactiva con un juego Unreal real en el laptop (Octopath Traveler II usa `Engine.ini` en UTF-8/CRLF; el editor nuevo se validó en el laptop con Prueba 48 y los vectores de `tests/vectors/ini/`, quedando pendiente la verificación en vivo con el juego abierto en sesión 1).

## Pendiente heredado de la fase 2b

- Punto de restauración de Windows (`SRSetRestorePointW` vía `srclient.dll`): sin verificar en el escritorio interactivo qué ocurre cuando la Protección del sistema está desactivada o dentro de la ventana de frecuencia de Windows (24 h); el diario de cambios, `--once`, el modo seguro tras dos cierres anormales y la tolerancia a diario corrupto sí se validaron en el laptop y en las Pruebas 49 y 50.
- Diálogos de `--gui` (`MessageBoxW` de recuperación y punto de restauración opcional) y pestaña Historial alimentada por `FormatJournalHistory`: sin prueba visual en sesión de escritorio (la lógica de decisión está cubierta en la Prueba 49).

## Pendiente heredado de la fase R3 y del paquete 0.1.0

- **Consumo en reposo:** el criterio de R3 (el daemon por debajo de 15 MB) se mide aparte, con el método de `docs/mediciones/`; hasta entonces no se afirma una cifra.
- **Capas en migración:** `scripts/check_layers.sh` todavía deja a `session/` incluir `detect/`, `optimize/` y `windows/` (marcado como «Migration» en su tabla). Esas entradas se quitan cuando cada pieza pase detrás de un puerto; `session/` ya no llama a Win32, pero sí incluye adaptadores concretos.
- **Dónde se publican el zip y su `.sha256`:** sin decidir. El hash solo protege si llega por un camino distinto al del zip.
- **La prueba del paquete** (`tests/package/package_tests.ps1`) corre a mano, como administrador, en un Windows real; no está en el CI.
- **El paquete no está firmado, ni es un MSI, ni se actualiza solo:** ver las tres primeras filas de la tabla de la sección 9.

## 9. Ideas sin fase todavía

Salieron de la ronda de ideas del 2026-11-09. Ninguna se empieza sin decidirla antes; las marcadas "sin verificar" necesitan investigación.

| Idea | Para qué | Coste | Riesgo o duda |
|---|---|---|---|
| Ejecutable y scripts firmados | Quitar avisos de SmartScreen y poder comprobar el origen. Hoy el zip 0.1.0 no está firmado y solo trae SHA-256 | Medio-alto | Coste del certificado, sin verificar |
| Instalador MSI o MSIX en lugar del zip con PowerShell | Doble clic, sin consola de administrador | Medio-alto | Sin verificar qué exige cada formato; habría que repetir la prueba de instalar y desinstalar |
| Actualizaciones automáticas (descargar e instalar solas) | No quedarse en una versión vieja | Alto | Exige firma y un canal de red nuevo; distinto del «aviso de actualizaciones» de abajo, que no instala nada |
| Perfil de portátil de mano (ROG Ally, Legion Go, Steam Deck) | Ajustes prudentes sin que la persona sepa qué es un nivel | Medio | Sin verificar que el nombre del fabricante sea estable entre modelos |
| Preguntar por programas desconocidos | Un programa nuevo que gasta CPU se pregunta una vez, fuera del juego | Medio | Que resulte pesado |
| Avisar si otro optimizador toca lo mismo (Armoury Crate, Legion Space) | Evita dos programas peleando por un valor | Medio | Sin verificar qué toca cada uno |
| Juegos de Epic, GOG, EA, Ubisoft y Xbox | Más juegos sin añadirlos a mano | Alto | Rutas de Xbox con permisos especiales; más anti-cheat |
| Modo simple y modo detallado | No abrumar | Medio | Dos vistas que mantener |
| Aviso de actualizaciones (sin instalar solo) | Saber que hay versión nueva | Medio | Conexión de red nueva; mejor en la GUI y opcional |
| Hashes publicados en un sitio aparte del zip | Poder comprobar el original aunque el zip no esté firmado | Bajo | Dónde publicarlos (ver «Pendiente heredado de la fase R3») |
| GUI sin permisos de administrador, separada del daemon | Un fallo de la GUI no ejecuta nada como administrador | Alto | Más piezas y un canal local nuevo |

## 10. Después del plan: sacar el máximo del hardware

**No forma parte de este plan.** Es lo que se estudia cuando la fase R3 y la G estén terminadas: palancas que hoy no se tocan porque necesitan acceso más profundo al hardware (la R3 ya terminó, falta la G). Todas entrarían como **experimentales**, nunca por defecto, y cada una necesita su propio ADR.

| Tema | Qué podría dar | Por qué no está en el plan | Por investigar |
|---|---|---|---|
| Límites de potencia de la APU (STAPM, PPT, temperatura) | Más vatios sostenidos en portátiles y consolas portátiles | Hoy VegaFlow solo limita la frecuencia de la CPU con el plan de energía (`power_mode.cpp:80`). Cambiar los límites reales suele requerir un driver de kernel, que es una superficie de ataque nueva para un programa de administrador | Qué métodos oficiales existen por fabricante, y si las herramientas conocidas dependen de drivers que Windows marca como vulnerables (sin verificar) |
| Frecuencia y voltaje de la GPU | Más rendimiento o menos consumo | Cada fabricante tiene su SDK (AMD ADLX, NVIDIA NVAPI, Intel IGCL) y el riesgo de inestabilidad es real | Qué expone cada SDK sin drivers propios, su licencia y si funciona con MinGW (sin verificar) |
| Memoria reservada para la gráfica integrada | Más VRAM para juegos exigentes | Se cambia en la BIOS, no desde Windows | Una guía por fabricante, sin tocar nada desde VegaFlow |
| Ventiladores y perfiles del fabricante | Menos ruido o más margen térmico | Cada marca lo hace distinto, y choca con sus propios programas (Armoury Crate, Legion Space) | Si hay interfaces documentadas |

**Condición para empezar cualquiera de estos:** que el diario de cambios (2b), el modo desarrollador (1f) y la revisión de resultados de la comunidad ya funcionen, porque son lo que permite probar algo así sin dejar un equipo roto.

---
