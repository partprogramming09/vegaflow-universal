# Fase R1: mover sin cambiar lógica

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Antes de cambiar reglas, cada cosa tiene que estar en su sitio. Este paso solo mueve código, así que se comprueba con facilidad: las mismas pruebas deben dar el mismo resultado.

**Qué se hace, en commits separados:**
1. Llevar los tipos compartidos (`ApuOptimizationPolicy`, `SystemHardwareProfile`, `DetectedGameProcess`...) a `src/model/`, y sacar de `detect/system_info.cpp` los dos `#include` de optimizadores. Eso corta la dirección `detect/` → `optimize/`. La otra dirección no se corta solo moviendo: 5 sitios de `optimize/` vuelven a detectar el hardware entero cada vez que se usan (`dxvk_manager.cpp:256` y `:380`, `engine_presets.cpp:100`, `power_mode.cpp:78`, `amd_driver_tweaks.cpp:19`). Pasarles la política desde fuera es cambiar lógica y va en R2.
2. Llevar `run_cycle`, `reports` y la lógica de sesión de `gui_mode` a `src/session/`. En `app/` quedan solo las entradas.
3. Repartir `tests/test_main.cpp` en un archivo por módulo con **doctest**, con un `main` común que los ejecuta en el mismo orden. **El guardián que restaura el registro y el plan de energía se queda en ese `main` común y se ejecuta aunque falle un módulo.** Lo primero es compilar doctest con MinGW; si no compila, el reparto se hace igual con el `main` propio de hoy.
4. Ordenar el crate de Rust en `policy/` y `ffi.rs`, y generar `rust_bridge.h` con **cbindgen** (o, si no puede con algún caso, dejarla a mano y añadir una prueba que compare los tamaños de los structs en los dos lenguajes).
5. Actualizar la lista de archivos de `scripts/build.ps1`: la escribe a mano, mientras que `build-cross.sh` los busca solo.
6. Añadir `scripts/check_layers.sh` (la tabla de dependencias de la sección 3b) al CI.

**Terminado cuando:**
- el script de capas pasa en el CI: `detect/` ya no incluye nada de `optimize/`;
- la suite da en el laptop exactamente el mismo resultado que la línea base de la fase 0;
- el ejecutable funciona igual (los modos `--system-info` y `--restore-registry` dan la misma salida);
- el CI de GitHub sigue en verde.
