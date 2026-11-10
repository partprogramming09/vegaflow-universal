# Seguridad: anti-cheat y SandboxGuard

## 3. Seguridad Absoluta Frente a Sistemas Anti-Cheat y Subsistema SandboxGuard

VegaFlow Universal implementa una política de seguridad estricta e inviolable: **bajo ninguna circunstancia se inyectarán DLLs ni se modificarán archivos de juegos protegidos por sistemas Anti-Cheat.** Toda operación sobre el disco y los procesos se rige por un modelo de aislamiento y contención formalizado en el módulo [`SandboxGuard`](../../src/windows/sandbox_guard.hpp).

```
+------------------------------------------------------------------------------------+
|                         MATRIZ DE DECISIÓN DE SEGURIDAD                            |
+------------------------------------------------------------------------------------+
|                                                                                    |
|                     ¿Se detecta tecnología Anti-Cheat?                             |
|       (HoYoKProtect, Vanguard, EAC, BattlEye, ACE, mhypbase, etc.)                 |
|                                                                                    |
|                   /                                   \                            |
|                 SÍ                                     NO                          |
|                /                                         \                         |
|   +-----------------------------+        +-----------------------------------+     |
|   |   RUTA DE OPTIMIZACIÓN      |        |   RUTA DE OPTIMIZACIÓN COMPLETA   |     |
|   |         NO INVASIVA         |        |         (NATIVA + MOTOR)          |     |
|   +-----------------------------+        +-----------------------------------+     |
|   | 1. DXGI Flip Model vía      |        | 1. DXGI Flip Model (Registro).    |     |
|   |    registro de Windows.     |        | 2. CPU Sets cooperativos.         |     |
|   | 2. CPU Sets cooperativos.   |        | 3. DXVK-GPLAsync en carpeta.      |     |
|   | 3. Temporizador NT a 0.5 ms |        | 4. Presets UMA Engine.ini /       |     |
|   | 4. MMCSS Gaming GPU Prio 8. |        |    boot.config con .vegaflow.bak  |     |
|   | 5. PROHIBIDO MODIFICAR O    |        | 5. Purga de memoria inteligente.  |     |
|   |    INYECTAR ARCHIVOS        |        |                                   |     |
|   |    (LOCKDOWN TOTAL SANDBOX) |        |                                   |     |
|   +-----------------------------+        +-----------------------------------+     |
|                                                                                    |
+------------------------------------------------------------------------------------+
```

### Firmas Monitoreadas en Tiempo Real:
- **EasyAntiCheat**: `easyanticheat`, `EasyAntiCheat_EOS.dll`
- **BattlEye**: `battleye`, `beservice.exe`
- **Riot Vanguard**: `vanguard`, `vgk.sys`
- **HoYoverse Protect**: `hoyokprotect`, `mhyprot`, `mhypbase` (Zenless Zone Zero, Genshin Impact, Honkai: Star Rail)
- **Tencent AntiCheatExpert**: `anticheatexpert`, `randgrid.sys`, `acebase.dll`
- **Otras tecnologías de integridad**: `blackcipher`, `equ8`, `nprotect`, `gameguard`, `xigncode`

Cualquier intento de desplegar DXVK o sobreescribir archivos en un directorio con estas firmas es **rechazado en seco por el código** (`blocked_by_anticheat = true`), salvaguardando la cuenta del usuario de cualquier riesgo de baneo.

### 3.1. Arquitectura de Aislamiento y Contención: `SandboxGuard`
Implementado en [`src/windows/sandbox_guard.hpp`](../../src/windows/sandbox_guard.hpp) y [`src/windows/sandbox_guard.cpp`](../../src/windows/sandbox_guard.cpp), el subsistema `SandboxGuard` formaliza el paradigma de seguridad **Zero-Trust File Access**:

1. **Normalización Canónica (`CanonicalizePath`)**:
   - `GetFullPathNameW` vuelve absoluta la ruta y quita `.` y `..`.
   - La parte de la ruta que ya existe se abre y se cambia por su ruta real (`GetFinalPathNameByHandleW`): así se resuelven junctions, enlaces simbólicos y nombres cortos 8.3 (`PROGRA~1`). Hasta 2026-11-10 se usaba `weakly_canonical`, que con MinGW **no** resolvía junctions: un junction dentro de temp hacia `System32` pasaba la comprobación (medido en el laptop; ver `docs/plan/maestro/03-fase-1.md`).
   - `\\?\C:\...` se trata como `C:\...`. Cualquier otra ruta que empiece por `\\` (red, `\\.\`, otras formas de `\\?\`) se deja igual y la decisión la rechaza por no ser local.
   - Pone la letra de unidad en mayúscula y quita la barra final.

2. **Whitelist Estricta vs Blacklist de Sistema (Prioridad Inviolable)**:
   - **Blacklist (Máxima Prioridad)**:
     - Bloqueo categórico de escritura en la raíz de unidades de disco (`C:\`, `D:\`) y archivos hijo directos (`C:\bootmgr`, `C:\test.txt`) (regla `drive_root`).
     - Archivos críticos de arranque y paginación del sistema operativo (`bootmgr`, `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`, `ntldr`, `bootsect.bak`).
     - Directorios de Windows y del Sistema (`GetWindowsDirectoryPath()`, `GetSystemDirectoryPath()`, `C:\Windows\System32`, `SysWOW64`, `WinSxS` y equivalentes en cualquier volumen).
     - Carpetas de sistema compartidas: `Program Files\Common Files` y `System Volume Information`.
   - **Dónde vive esta regla:** la clasificación de la blacklist la decide el núcleo Rust ([`path_policy.rs`](../../rust/vegaflow-core/src/policy/path_policy.rs)), no C++. C++ canonicaliza la ruta, la pasa a minúsculas con `towlower` (igual que siempre, para no diferir con texto no ASCII) y recibe un veredicto; los mensajes de error siguen siendo los mismos. Rust y C++ se comprueban con el mismo archivo de casos, [`tests/vectors/sandbox_blacklist.tsv`](../../tests/vectors/sandbox_blacklist.tsv) (52 casos; los usan la prueba 33 de C++ y las pruebas de Rust). Dos comportamientos que parecen raros se conservan a propósito porque es lo que hacía el código: `C:\WindowsApps` se bloquea por prefijo de `C:\Windows`, y una ruta `\\servidor\recurso` no cuenta como raíz de disco.
   - **Lista blanca (carpetas permitidas)**, comparadas carpeta por carpeta (`...\Local` no permite `...\LocalOtraCosa`):
     - la carpeta del ejecutable de VegaFlow (ya no su carpeta padre ni la carpeta de trabajo);
     - `steamapps\common` de cada biblioteca de Steam listada en `libraryfolders.vdf` (ya no cualquier ruta que contenga `steamapps\common`). El archivo lo lee el núcleo Rust ([`steam_library.rs`](../../rust/vegaflow-core/src/policy/steam_library.rs)) una vez por ejecución;
     - `%LOCALAPPDATA%`, la carpeta temporal, `Documents\My Games` y `Saved Games`;
     - las carpetas registradas en ejecución (`AddAllowedPath`) y las de **solo lectura** (`AddReadOnlyPath`, por ejemplo las DLL de origen de DXVK: se pueden leer y copiar, no escribir).
     - Una carpeta de esta lista cuya ruta real no sea la ruta escrita (por ser un junction, un enlace simbólico o un volumen montado en carpeta, o estar debajo de uno) **no cuenta**. Se decide con una sola resolución (`CanonicalizePath` comparada con la ruta escrita con nombres largos, sin distinguir mayúsculas), así que cambiar el enlace a mitad no sirve. El usuario dueño de `Documents\My Games` o de una biblioteca de Steam podría hacerla apuntar a `C:\Program Files` y el daemon, que corre elevado, escribiría allí. Lo encontró la revisión de seguridad de la fase 1 (2026-11-10); la prueba 41 lo comprobó en rojo antes del arreglo.
     - Vale para las carpetas fijas, las de Steam y las de solo lectura. Las de `AddAllowedPath` (solo las usan las pruebas) se resuelven al registrarlas.
     - Coste: una biblioteca de Steam en un disco montado como carpeta, un perfil movido con un junction o un VegaFlow instalado bajo un enlace dejan de servir (se rechaza con `NotWhitelisted`).
   - **Una sola decisión en Rust:** [`file_access.rs`](../../rust/vegaflow-core/src/policy/file_access.rs) aplica, en este orden, la lista negra, "solo rutas locales" (sin prefijos de dispositivo o red, sin flujos `:`, y solo en la forma que da `CanonicalizePath`: sin `/`, sin `\\` doble ni barra final, sin nombres que acaben en `.` o espacio —eso incluye `.` y `..`— y sin NUL, porque Windows leería esas grafías como otra ruta y la lista negra no las reconocería), el anti-cheat (solo al escribir) y la lista blanca. Una carpeta permitida que sea la raíz de un disco, relativa o de red se ignora. C++ reúne las carpetas y mira el disco (anti-cheat); Rust decide. Casos compartidos: [`tests/vectors/file_access.tsv`](../../tests/vectors/file_access.tsv) (prueba 40 de C++ y pruebas de Rust); la prueba 41 comprueba cada fallo de la lista blanca anterior en Windows.
   - **Riesgo que queda:** `libraryfolders.vdf` y la ruta de Steam del registro (`HKCU`) los puede cambiar el usuario. Sumar una biblioteca falsa solo permite su `steamapps\common`, una carpeta que ese usuario ya puede escribir, y nunca a través de un junction (ver arriba). Un hardlink dentro de una carpeta permitida hacia un archivo protegido no se detecta (la ruta del handle es la del enlace); queda abierto y sin medir en el laptop si un usuario sin permisos sobre el archivo puede crear ese hardlink.

3. **Cuarentena y Lockdown Anti-Cheat**:
   - Si se detecta una tecnología de anti-cheat activa a través de `DirectoryContainsAntiCheat` (escaneo recursivo de hasta 4 niveles superiores en el árbol de carpetas) o `PathContainsAntiCheatSignature`, o si se pasa explícitamente el flag `is_anticheat_protected = true`, el sandbox activa un **Lockdown Inmediato**:
   - Se deniegan incondicionalmente las acciones de modificación, copia o borrado (`SandboxAction::Write`, `SandboxAction::Copy`, `SandboxAction::Delete`).
   - Solo se permite la lectura segura de metadatos (`SandboxAction::Read`).

4. **Wrappers Seguros de I/O** (`SafeWriteFile`, `SafeCopyFile`, `SafeRemoveFile`, `SafeReadFile`):
   - Primero validan la ruta. Después abren el archivo con `CreateFileW` y **vuelven a validar la ruta real del archivo abierto** (`GetFinalPathNameByHandleW`). Si esa ruta real coincide con la ya validada no se repite la decisión (ni el análisis de anti-cheat); si difiere, se decide de nuevo con ella. Solo entonces cambian el contenido, por el mismo handle. Así, una carpeta cambiada por un junction entre la comprobación y la apertura se detecta (prueba 42).
   - Escribir no vacía el archivo al abrirlo (`OPEN_ALWAYS`, no `CREATE_ALWAYS`): si la segunda comprobación falla, el contenido queda intacto, y un archivo recién creado se borra por su handle.
   - Copiar lee el origen por su handle (comprobado) y escribe el destino igual que `SafeWriteFile`; sin `overwrite`, un destino existente se rechaza (`CREATE_NEW`).
   - Borrar abre con `FILE_FLAG_OPEN_REPARSE_POINT` (un enlace se borra a sí mismo, nunca su destino) y borra por handle.
   - Lo que queda: las carpetas intermedias que faltan se crean antes de la segunda comprobación; si alguien cambia una carpeta en ese momento, pueden quedar carpetas vacías fuera, pero ningún archivo.

5. **Auditoría y Registro de Violaciones (`SandboxViolationRecord`)**:
   - Cada intento de acceso no autorizado registra un evento con marca de tiempo precisa en milisegundos (`timestamp_ms`), la acción solicitada (`SandboxAction`), el motivo técnico (`SandboxDenialReason`) y un mensaje explicativo para trazabilidad forense.

