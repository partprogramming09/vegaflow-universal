# Fase R2: puertos de archivos y de reloj

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Las fases 1 y 2 cambian cómo se escriben archivos y la fase 4 depende del tiempo (las pausas). Con los puertos, esas reglas se prueban con archivos y relojes falsos, sin tocar el disco ni esperar.

**Qué se hace:**
- `ports/file_system.hpp` (leer, escribir, copiar y borrar de forma segura) con su adaptador `windows/safe_file_system.cpp`, que es el `SandboxGuard` de hoy;
- `ports/clock.hpp` con un adaptador real y uno falso para las pruebas;
- `windows/unique_handle.hpp`: un cierre automático para claves de registro y handles (en lugar de WIL, que no soporta MinGW).

Los llamadores pasan a recibir el puerto en el constructor, en lugar de usar `SandboxGuard::` estático. Además, los 5 sitios de `optimize/` que vuelven a detectar el hardware reciben la política ya calculada. Así se corta la otra mitad del ciclo y se deja de llamar a DXGI y CPUID en cada uso.

**Terminado cuando:** la suite está igual de verde, hay al menos una prueba unitaria que usa el puerto falso y ningún archivo de `optimize/` incluye `detect/`.
