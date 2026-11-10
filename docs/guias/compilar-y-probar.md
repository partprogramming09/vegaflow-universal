# Compilar y probar

## 5. Compilación, Enlace y Verificación de Pruebas

VegaFlow Universal está escrito en **C++20 estándar** con enlace estático en Windows, sin dependencias pesadas de runtime externas (MSVCRT/VCRedist no requeridos con MinGW).

### 5.1. Toolchain Recomendada
- **Compilador:** GCC 13+ / 16.2.0 (MinGW-w64) con soporte C++20 completo.
- **Librerías del Sistema Requeridas:**
  `-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -ltdh`

### 5.2. Compilación Automatizada (PowerShell)

El proyecto incluye el script automatizado [`scripts/build.ps1`](../../scripts/build.ps1), el cual detecta automáticamente MinGW g++ (en PATH o en Scoop), gestiona las rutas modulares y permite compilar ambos ejecutables y opcionalmente ejecutar la suite de pruebas:

```powershell
# Compilar vegaflow_daemon.exe y vegaflow_tests.exe:
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1

# Compilar y ejecutar inmediatamente los 35 tests unitarios:
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```

#### Compilar desde un Mac o Linux (compilación cruzada)
```bash
brew install mingw-w64                      # una vez
rustup target add x86_64-pc-windows-gnu     # una vez
scripts/build-cross.sh                      # genera build/vegaflow_daemon.exe y build/vegaflow_tests.exe
```
Usa las mismas banderas y bibliotecas que `build.ps1`, y compila en paralelo (unos 15 segundos en un Mac con 10 núcleos). Los fuentes se buscan con un comodín sobre `src/`, así que un `.cpp` nuevo se incluye sin editar ninguna lista. Los `.exe` se prueban en un equipo Windows; no hace falta compilar allí.

#### Integración continua (GitHub Actions)
El archivo `.github/workflows/ci.yml` ejecuta, en los equipos de GitHub, las pruebas del núcleo Rust y la compilación cruzada del daemon y de la suite. Las 31 pruebas de C++ no corren allí: cambian el registro y el plan de energía de un Windows real y necesitan GPU, así que se ejecutan a mano en el laptop de pruebas. No se debe registrar ese laptop como runner propio de un repositorio público, porque cualquier fork podría ejecutar código en él.

#### Probar solo el núcleo Rust (en cualquier equipo, también en un Mac)
```bash
cargo test --manifest-path rust/vegaflow-core/Cargo.toml
```
Fuera de Windows no existe `ntdll`, así que las funciones que lo llaman devuelven error y el resto de la lógica (Tiers, topología de CPU, confinamiento, firmas anti-cheat) se prueba igual. Los casos de clasificación de Tiers y de la blacklist de rutas salen de `tests/vectors/apu_tier.tsv` y `tests/vectors/sandbox_blacklist.tsv`, que usan tanto estas pruebas como las pruebas 32 y 33 de C++: si las dos implementaciones difieren, una de las dos falla.

### 5.3. Compilación Manual con MinGW G++

#### Compilar la librería estática de Rust (Paso previo obligatorio):
```bash
cargo build --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu --release
```

#### Fuentes comunes al daemon y a las pruebas
Todo `src/` menos `src/app/` (las entradas del daemon). Con Git Bash o MSYS2:
```bash
COMMON="src/windows/*.cpp src/detect/*.cpp src/session/*.cpp src/optimize/*.cpp src/optimize/graphics/*.cpp src/optimize/steam/*.cpp src/safety/*.cpp src/ui/*.cpp"
FLAGS="-std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -Isrc"
RUST=rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a
LIBS="-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -ltdh -lws2_32 -luserenv -lbcrypt"
```

#### Compilar el Daemon de Fondo (`vegaflow_daemon.exe`):
```bash
g++ $FLAGS $COMMON src/app/*.cpp -o build/vegaflow_daemon.exe $RUST $LIBS
```

#### Compilar la Suite de Pruebas Unitarias (`vegaflow_tests.exe`):
```bash
g++ $FLAGS $COMMON tests/*.cpp tests/integration/*.cpp tests/unit/*.cpp -o build/vegaflow_tests.exe $RUST $LIBS
```

Si se añade una carpeta nueva dentro de `src/`, hay que añadirla a `COMMON`. `build.ps1` y `build-cross.sh` la encuentran solos.

---

## 6. La Suite de Pruebas Integrada (Obligatoria para Todo Commit)

Cualquier cambio en el repositorio debe ser validado ejecutando `build/vegaflow_tests.exe`. Todos los tests deben pasar en verde sin excepciones:

```
[PASS] Test 1: PrivilegeManager elevation check and privilege adjustment
[PASS] Test 2: HardwareProfiler universal hardware & CPU set detection
[PASS] Test 3: MemoryManager telemetry, dry-run optimization, and Zero-Overhead KPI (< 15 MB)
[PASS] Test 4: ProcessDetector anti-cheat safe inspection, process filtering, and API mapping
[PASS] Test 5: GraphicsOptimizer timer resolution manipulation, shader cache inspection, and DXGI flip upgrade dry-run
[PASS] Test 6: CpuOrchestrator dry-run optimization and state restoration
[PASS] Test 7: FrameTelemetryMonitor mathematical rigor, 1% Low, circular buffer, and shutdown latency
[PASS] Test 8: Phase 1-2 Enhancements & System Tweaks (Admin relaunch, process liveness, MPO, manual purge)
[PASS] Test 10: Phase 4 System Tweaks & Session Report Persistence
[PASS] Test 11: Stability, Memory Stress & CpuOrchestrator Deduplication
[PASS] Test 13: SteamEngineOptimizer Engine Detection, Safe Backup & Restore
[PASS] Test 14: Single-Instance Guard, Non-Blocking MMAgent Dry-Run & Steam Exe Sync
[PASS] Test 15: DX11/DX12 FPS Boost Suite (MMCSS GPU Priority=8, Win32PrioritySeparation Quantum & AMD Driver Boost Dry-Run)
[PASS] Test 16: DXVK Manager Module (Deployment, Async GPL, AntiCheat Safety, Backup & Removal)
[PASS] Test 17: SandboxGuard Boundary Validation, Canonicalization, Anti-Cheat Lockdown & Safe I/O
[PASS] Test 18: SystemInfo Telemetry, Hardware Specs Gathering & JSON Serialization
[PASS] Test 19: Modern 3D Gaming Suite (EcoQoS Background Throttling, I/O Priority Hierarchy, DirectFlip & Low-Latency D3D11 Queues)
[PASS] Test 20: APU Silicon & UMA Bus Optimization Suite (DisablePagingExecutive Kernel Shield & Smart Tessellation 16x)
[PASS] Test 21: MMCSS Audio & Gaming Priority Shield
[PASS] Test 22: DXVK APU Profile Configuration & Shader Pipeline Pre-warming
[PASS] Test 23: Memory Manager Standby List Purge & UMA Bus Prioritizer
[PASS] Test 24: APU Shadow & Cascades Optimizer (CSM 2-Splits & Half-Res AO)
[PASS] Test 25: Universal APU Tier Classification, DX11/DX12 Policy & Advanced APU Safety Guards
[PASS] Test 26: Live APU Guard Telemetry, Multi-CCX / Hybrid L3 Partitioning & Dynamic Pool Sync
[PASS] Test 27: Background confinement follows its CPU use
[PASS] Test 28: Background CPU set is one physical core (one thread below 4 cores)
[PASS] Test 29: Registry changes restore the original values
[PASS] Test 30: HKLM tweaks restore the original values
[PASS] Test 31: Hybrid Rust FFI Core (Zero-Allocation UMA Guard, L3 Topology, Hysteresis & Anti-Cheat Scanner)
[PASS] Test 32: APU tier classification matches the shared vector file
[PASS] Test 33: SandboxGuard blacklist matches the shared vector file
[PASS] Test 34: rust_bridge.h structs have the same layout in C++ and Rust
[PASS] Test 35: UniqueHandle and UniqueRegKey close what they own exactly once
[PASS] Test 36: DxvkManager changes game files only through the FileSystem port (fake file system)
[PASS] Test 37: the purge cooldown follows the injected clock

================================================================================
TEST RESULTS: 35 PASSED, 0 FAILED
================================================================================
```

---

