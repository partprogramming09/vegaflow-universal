# Compilar y probar

**Lector:** personas

## 5. Compilación, Enlace y Verificación de Pruebas

VegaFlow Universal está escrito en **C++20 estándar** con enlace estático en Windows, sin dependencias pesadas de runtime externas (MSVCRT/VCRedist no requeridos con MinGW).

### 5.1. Toolchain Recomendada
- **Compilador:** GCC 13+ / 16.2.0 (MinGW-w64) con soporte C++20 completo.
- **Librerías del Sistema Requeridas:**
  `-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -lws2_32 -luserenv -lbcrypt`

### 5.2. Compilación Automatizada (PowerShell)

El proyecto incluye el script automatizado [`scripts/build.ps1`](../../scripts/build.ps1), el cual detecta automáticamente MinGW g++ (en PATH o en Scoop), gestiona las rutas modulares y permite compilar ambos ejecutables y opcionalmente ejecutar la suite de pruebas:

```powershell
# Compilar vegaflow_daemon.exe y vegaflow_tests.exe:
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1

# Compilar y ejecutar inmediatamente la suite completa de C++ (la última corrida con `scripts/remote/vfdev cross-test`, 2026-11-15, imprime `TEST RESULTS: 281 PASSED` sin fallos; el número crece con cada prueba nueva, no lo copies de aquí):
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
El archivo `.github/workflows/ci.yml` ejecuta, en los equipos de GitHub, las pruebas del núcleo Rust y la compilación cruzada del daemon y de la suite. Las pruebas de C++ no corren allí: cambian el registro y el plan de energía de un Windows real y necesitan GPU, así que se ejecutan a mano en el laptop de pruebas. No se debe registrar ese laptop como runner propio de un repositorio público, porque cualquier fork podría ejecutar código en él.

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
COMMON="src/windows/*.cpp src/detect/*.cpp src/session/*.cpp src/optimize/*.cpp src/optimize/graphics/*.cpp src/optimize/steam/*.cpp src/ui/*.cpp"
FLAGS="-std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -Isrc"
RUST=rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a
LIBS="-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -lws2_32 -luserenv -lbcrypt"
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

La suite imprime una línea `[PASS]` o `[FAIL]` por prueba y termina con el resumen. Lo que debe verse al final (laptop de pruebas, 2026-11-14):

```
TEST RESULTS: 57 PASSED, 0 FAILED
```

Dónde está cada prueba:
- `tests/integration/`: tocan Windows de verdad (registro, plan de energía, procesos), un archivo por módulo. Son las únicas que tocan el registro real.
- `tests/unit/`: usan puertos falsos y no escriben archivos de juegos ni el registro.
- `tests/test_main.cpp`: las corre en orden y, al final, un guardián devuelve el sistema a como estaba. No la interrumpas a mitad.

Las reglas escritas en Rust se prueban aparte con `cargo test` (252 pruebas el 2026-11-14), y las capas de `src/` con `scripts/check_layers.sh`.

---

## 7. El paquete de instalación

Se genera desde un Mac o Linux, con lo mismo que pide `scripts/build-cross.sh` (mingw-w64 y el objetivo de Rust) más `zip` y `shasum`:

```bash
scripts/package.sh                     # genera dist/VegaFlow-<versión>-win-x64.zip y su .sha256
```

Antes de compilar, el script exige que la versión coincida en tres sitios: `rust/vegaflow-core/Cargo.toml`, `src/ui/app_identity.hpp` y la entrada más reciente de `CHANGELOG.md`; si no, se detiene. Después compila con `build-cross.sh`, reúne en `dist/` el daemon, `install.ps1` y `uninstall.ps1` (fuente: `scripts/package/`), `config/config.json`, `VERSION`, la licencia, el `CHANGELOG.md` y los dos README, escribe `SHA256SUMS` con el hash de cada archivo, y comprime todo. `dist/` no se sube a git.

Para comprobar el paquete se usa `tests/package/package_tests.ps1`, que instala, aplica un cambio real, desinstala y compara el sistema; se corre como administrador en un Windows real. Cómo usarlo, qué comprueba y su resultado están en [`instalar-y-desinstalar.md`](instalar-y-desinstalar.md).

---

