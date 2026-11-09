# Librerías y herramientas

**Lector:** personas y agentes. Comprobado el 2026-11-09. Reglas en el [README](README.md).

Restricciones del proyecto: se compila con **MinGW (g++)**, también de forma cruzada desde macOS; el núcleo Rust no tiene dependencias; el daemon queda por debajo de 15 MB. La decisión de cada una está en la sección 3c del plan ([`arquitectura.md`](../plan/maestro/arquitectura.md)).

| Librería | Dato | Fuente | Confianza | Decisión |
|---|---|---|---|---|
| WIL (Microsoft) | No soporta MinGW: la petición se cerró en 2023 y en 2025 sigue fallando con llvm-mingw | [wil#117](https://github.com/microsoft/wil/issues/117), [wil#548](https://github.com/microsoft/wil/issues/548) | Alta | No; RAII propio |
| cxx (dtolnay) | Su CI solo prueba MSVC en Windows: con MinGW no está probado | [ci.yml de cxx](https://github.com/dtolnay/cxx/blob/master/.github/workflows/ci.yml) | Alta | No |
| cbindgen | MPL-2.0; corre en la máquina que compila; avisa de que puede fallar en algún caso | [cbindgen](https://github.com/mozilla/cbindgen) | Alta | Sí, con prueba de tamaños como respaldo |
| GoogleTest | Pide C++17; la matriz de Google solo lista MSVC en Windows | [Matriz de soporte](https://github.com/google/oss-policies-info/blob/main/foundational-cxx-support-matrix.md) | Alta | No |
| doctest | MIT, una sola cabecera; nombra GCC, no MinGW | [doctest](https://github.com/doctest/doctest) | Media | Sí, si compila (se prueba en R1) |
| Catch2 v3 | Librería estática; menos coste de inclusión que v2 | [Migración v2 a v3](https://github.com/catchorg/Catch2/blob/devel/docs/migrate-v2-to-v3.md) | Alta | No: doctest es más ligero |
| krabsetw | MIT, solo cabeceras, se distribuye como solución de Visual Studio; MinGW sin confirmar | [krabsetw](https://github.com/microsoft/krabsetw) | Media | No (la fase 6 quita la captura) |
| json.h | Unlicense, una sola cabecera, un solo `malloc`, soporta GCC | [json.h](https://github.com/sheredom/json.h) | Alta | Sí (fase 1b) |
| nlohmann/json | MIT, una cabecera grande; MinGW solo con una libstdc++ completa | [nlohmann/json](https://github.com/nlohmann/json) | Alta | No: más grande |
| keyvalues-parser (Rust) | Lee VDF v1 pero arrastra `pest` | [docs.rs](https://docs.rs/keyvalues-parser/latest/keyvalues_parser/) | Alta | No: lector propio en Rust |
| hwloc | BSD, lista MinGW, pero es una dependencia compilada | [hwloc](https://www.open-mpi.org/projects/hwloc/) | Alta | No: Windows ya da la topología |
| proptest | MIT/Apache-2.0; las `dev-dependencies` no entran en el programa | [proptest](https://docs.rs/proptest/latest/proptest/), [Cargo Book](https://doc.rust-lang.org/cargo/reference/specifying-dependencies.html) | Alta | Sí |
| cargo-fuzz | Lista Windows; necesita Rust nightly | [rust-fuzz book](https://rust-fuzz.github.io/book/cargo-fuzz/setup.html) | Alta | Opcional |
| cargo-mutants | Soporte de Windows y licencia | — | Sin verificar | Sí, en el CI de Linux |
| PresentMon / PresentData | MIT (Intel); consume solo ETW | [LICENSE](https://github.com/GameTechDev/PresentMon/blob/main/LICENSE.txt) | Alta | Solo en `scripts/bench.ps1` y en la prueba A/B del modo desarrollador |

**Sin medir todavía:** el tamaño que añaden json.h y doctest; se mide al compilar.
