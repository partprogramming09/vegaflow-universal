# Seguridad: anti-cheat y SandboxGuard

## 3. Seguridad Absoluta Frente a Sistemas Anti-Cheat y Subsistema SandboxGuard

VegaFlow Universal implementa una política de seguridad estricta e inviolable: **bajo ninguna circunstancia se inyectarán DLLs ni se modificarán archivos de juegos protegidos por sistemas Anti-Cheat.** Toda operación sobre el disco y los procesos se rige por un modelo de aislamiento y contención formalizado en el módulo [`SandboxGuard`](../../include/security/sandbox_guard.hpp).

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
|   | 5. FSR por Overlay externo  |        | 5. FSR Espacial Alt+F11.          |     |
|   |    (cero memoria de juego). |        | 6. Purga de memoria inteligente.  |     |
|   | 6. PROHIBIDO MODIFICAR O    |        |                                   |     |
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
Implementado en [`include/security/sandbox_guard.hpp`](../../include/security/sandbox_guard.hpp) y [`src/security/sandbox_guard.cpp`](../../src/security/sandbox_guard.cpp), el subsistema `SandboxGuard` formaliza el paradigma de seguridad **Zero-Trust File Access**:

1. **Normalización Canónica (`CanonicalizePath`)**:
   - Neutraliza de raíz vectores de ataque o errores de **Path Traversal** (`..`, `.`, dobles barras invertidas y symlinks).
   - Utiliza la API nativa Win32 `GetFullPathNameW` para resolver rutas relativas y el motor `std::filesystem::weakly_canonical` para resolver enlaces simbólicos y junctions en directorios existentes o en vías de creación.
   - Estandariza letras de unidad a mayúsculas y suprime separadores finales redundantes.

2. **Whitelist Estricta vs Blacklist de Sistema (Prioridad Inviolable)**:
   - **Blacklist (Máxima Prioridad)**:
     - Bloqueo categórico de escritura en la raíz de unidades de disco (`C:\`, `D:\`) y archivos hijo directos (`C:\bootmgr`, `C:\test.txt`) mediante `IsDriveRootOrDirectChild`.
     - Archivos críticos de arranque y paginación del sistema operativo (`bootmgr`, `pagefile.sys`, `hiberfil.sys`, `swapfile.sys`, `ntldr`, `bootsect.bak`).
     - Directorios de Windows y del Sistema (`GetWindowsDirectoryPath()`, `GetSystemDirectoryPath()`, `C:\Windows\System32`, `SysWOW64`, `WinSxS` y equivalentes en cualquier volumen).
     - Carpetas de sistema compartidas: `Program Files\Common Files` y `System Volume Information`.
   - **Whitelist Estricta (Límites Autorizados)**:
     - Raíz del binario de VegaFlow y directorio de trabajo del proyecto.
     - Bibliotecas de juegos de Steam (`steamapps\common\...`).
     - Almacenamiento local de aplicaciones del usuario (`%LOCALAPPDATA%`).
     - Carpeta de archivos temporales del sistema (`temp_directory_path`).
     - Carpetas de documentos y partidas del usuario (`Saved Games`, `Documents\My Games`).
     - Lista blanca dinámica (`AddAllowedPath`, `RemoveAllowedPath`, `ClearCustomAllowedPaths`) protegida con `std::mutex`.

3. **Cuarentena y Lockdown Anti-Cheat**:
   - Si se detecta una tecnología de anti-cheat activa a través de `DirectoryContainsAntiCheat` (escaneo recursivo de hasta 4 niveles superiores en el árbol de carpetas) o `PathContainsAntiCheatSignature`, o si se pasa explícitamente el flag `is_anticheat_protected = true`, el sandbox activa un **Lockdown Inmediato**:
   - Se deniegan incondicionalmente las acciones de modificación, copia o borrado (`SandboxAction::Write`, `SandboxAction::Copy`, `SandboxAction::Delete`).
   - Solo se permite la lectura segura de metadatos (`SandboxAction::Read`).

4. **Wrappers Seguros de I/O**:
   - `SafeWriteFile(path, content, is_anticheat_protected)`: Valida la ruta canónica contra la lista negra, whitelist y bloqueo anti-cheat antes de abrir el stream binario.
   - `SafeCopyFile(src, dst, overwrite, is_anticheat_protected)`: Valida el origen para lectura y el destino para copia segura, creando directorios intermedios necesarios.
   - `SafeRemoveFile(path, is_anticheat_protected)`: Verifica límites y protección anti-cheat antes de eliminar archivos o backups temporales.
   - `SafeReadFile(path, out_content)`: Comprueba la validez canónica antes de transferir datos a memoria.

5. **Auditoría y Registro de Violaciones (`SandboxViolationRecord`)**:
   - Cada intento de acceso no autorizado registra un evento con marca de tiempo precisa en milisegundos (`timestamp_ms`), la acción solicitada (`SandboxAction`), el motivo técnico (`SandboxDenialReason`) y un mensaje explicativo para trazabilidad forense.

