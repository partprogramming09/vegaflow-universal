# Plan de ejecución autónoma

Fecha: 2026-11-04. Rama `feat/ab-automation`, PR #5. Plan para terminar lo pendiente sin intervención del usuario salvo lo marcado en "Cuándo pedir algo". Se ejecuta en orden; cada fase termina con un commit y su casilla marcada aquí.

## Reglas

- Solo se toca el laptop por SSH (elevado) y por el ejecutor `vfjob`; nada permanente en el sistema (ni autologon, ni instalar software, ni tareas nuevas).
- Sin juegos reales. Sin cambios al registro o al plan de energía sin comprobar antes/después.
- Un solo trabajo de medición a la vez (`vfjob busy` debe estar vacío antes de lanzar).
- Medición antes de afirmar: cada cifra va a `docs/MEDICION_AB_DAEMON.md` con su límite. Lo no medido se dice como tal.
- Español, sin atribución de IA en commits ni PR, comentarios solo del qué.
- Tras 2 intentos fallidos de lo mismo: llamar al asesor antes de un tercero.

## Fases

### Fase 0: cerrar la medición térmica en curso
- [x] Esperar el fin de la cadena `therm.out` (sostenida sin carga) y leer el estado del doctor.
- [x] Analizar con `vfjob stats` (tiene en cuenta la lectura `perf_min=0`, que es una muestra anómala del contador).
- [x] Documentar en `docs/MEDICION_AB_DAEMON.md`: temperatura y frecuencia efectiva con y sin daemon, con y sin carga; sin potencia (el equipo no la expone).
- Hecho cuando: el documento tiene la tabla y su límite y está commiteado.

### Fase 1: desplegar y verificar las mejoras de `vfjob`
- [x] `vfjob update` (instala `ab_loop_task.ps1` y `bg_factors.ps1` nuevos y el manifiesto).
- [x] Probar: `ab` y `bg-factors` abren `drawstress` solos (lanzarlos sin `game` previo, 1 ronda corta de 30 s), `vfjob busy`, rechazo de un envío con un trabajo en curso, `VFJOB_QUEUE=1`, `vfjob sync` (sin cambios nuevos: "Already up to date"), `vfjob build-test` (29 de 29), `vfjob stats` sobre una salida real, espera por defecto del manifiesto.
- [x] Comprobar que el registro y el plan de energía quedan idénticos tras `build-test` (instantánea antes/después).
- Hecho cuando: todas las pruebas anteriores dan el resultado esperado y `doctor` sale limpio.

### Fase 2: subir
- [x] `git push` y actualizar la descripción del PR #5 con lo hecho desde la última versión.
- Hecho cuando: `git status` limpio (salvo `concepts_brief.md`, que es ajeno) y el PR refleja los cambios.

### Fase 3: validar la regla de hilos de fondo en 6 y 8 hilos (simulado)
- [x] Añadir las topologías `6c` (6 hilos, uno por núcleo) y `4c8t` (4 núcleos con SMT) a `bg_factors.ps1 -Mode sim` con condiciones base, 1 hilo y 2 hilos.
- [x] Medir con cargas de 1, 2 y 4 workers (3 rondas, 30 s por captura).
- [x] Comparar con la regla actual (un sexto de los hilos, mínimo 1: 6 hilos → 1, 8 hilos → 1). Si 2 hilos gana de forma clara en alguna, cambiar la regla con un caso nuevo en la Prueba 28 y repetir Fase 1 (compilar y probar).
- Estado: hecha el 2026-11-04. La regla de una sexta parte quedó contradicha (8 hilos con SMT prefieren un núcleo completo) y se cambió a un núcleo físico completo con 4 o más núcleos y un hilo con menos; 29 de 29 en el laptop.
- Hecho cuando: hay tabla documentada y la regla del código coincide con los datos, o queda dicho que no.

### Fase 4: térmico con serie de tiempo
- [x] Guardar la serie completa de muestras (CSV por captura) en `ab_loop_task.ps1 -Sensors 1`.
- [x] Repetir sostenido con capturas de 5 minutos, descartando el primer minuto, daemon encendido y apagado, 3 rondas, con carga de 4 workers.
- [x] Informar si la temperatura o la frecuencia cambian con el tiempo, no solo la media.
- Estado: hecha; la serie se guardó como media por minuto en vez de CSV por captura. Hecho cuando: documentado, con la serie como evidencia.

### Fase 5: umbrales y D3D12
- [x] Repetir el umbral de confinamiento (cargas de 0,23, 0,46 y 0,92 procesadores lógicos) en D3D12, 5 rondas.
- [ ] (no hecho) Una carga de fondo real: una compilación de `g++` (`build.ps1`) como fondo mientras corre `drawstress`, con y sin daemon, 5 rondas.
- [ ] Si el umbral de 0,5 o el de 0,25 no se sostiene, cambiar las constantes de `cpu_orchestrator.cpp`, ampliar la Prueba 27 y repetir Fase 1.
- Estado: umbrales D3D12 hechos y documentados (no cambian las constantes); la compilación como fondo no se hizo: sus procesos son efímeros y la afinidad por proceso no llega a los que nacen después, y medir su consumo necesita otro método.
- Hecho cuando: tabla en `docs/MEDICION_AB_DAEMON.md` y constantes respaldadas.

### Fase 6: restauración del registro en los flujos del daemon
- [x] Revisar `src/main.cpp`: los cierres limpios y `--apu-balanced` / restauración deben llamar a `RestoreAllRegistryBackups()` además de lo que ya restauran.
- [x] Añadir una opción de línea de comandos `--restore-registry` y una prueba que la ejecute.
- [x] `tweaks_state.ps1 -Restore`: guardar y devolver los valores previos (hoy escribe valores fijos); probar con la instantánea antes/después.
- Estado: hecha el 2026-11-04. Cubre GameDVR, UMD de AMD, MPO, MMCSS, `Win32PrioritySeparation` y `DisablePagingExecutive`/`LargeSystemCache`; 30 de 30 en el laptop, `--restore-registry` sale con 0 y el registro queda idéntico antes y después. El cierre limpio del daemon no restaura el registro (decisión de diseño pendiente).
- Hecho cuando: 29 de 29 (o más) en verde y el registro idéntico tras aplicar y restaurar.

### Fase 7: cierre
- [ ] Actualizar `docs/MEJORAS_MCP_Y_APU.md` (marcar lo hecho, lo que sigue abierto), `CLAUDE.md` §9, descripción del PR y la memoria del proyecto.
- [ ] Verificación final: `build-test` en verde, registro y plan de energía idénticos a la instantánea inicial, `doctor` limpio, laptop sin `drawstress` ni daemon abiertos, todo subido.
- Hecho cuando: el informe final nombra lo medido, lo no medido y lo que sigue en manos del usuario.

## Qué no entra

- DirectFlip, MPO, `DisablePagingExecutive`, teselación, DXVK, FSR y prioridad de I/O: necesitan un juego real o una carga de disco que no hay.
- Potencia en vatios y temperatura por componente: el equipo no expone los contadores.
- Equipos reales distintos: los prueba el usuario más adelante.
- Autologon y facturación de GitHub: del usuario; el CI queda sin correr.

## Cuándo pedir algo al usuario

Solo si ocurre una de estas y no hay forma indirecta:
- El laptop no responde por SSH más de 30 minutos (Tailscale caído, suspendido o apagado).
- La pantalla está bloqueada o no hay sesión de escritorio y hace falta (el doctor lo muestra).
- Hace falta reiniciar o iniciar sesión físicamente.
- El clasificador deniega una acción: no se rodea; se explica qué hacía falta.
- Una prueba falla de forma que no se entiende tras 2 intentos y el asesor no lo resuelve.
- El disco libre baja de 20 GB.

## Estimación

| Fase | Tiempo aprox. |
| --- | --- |
| 0 | 20 min |
| 1 | 30 min |
| 2 | 5 min |
| 3 | 90 min |
| 4 | 60 min |
| 5 | 90 min |
| 6 | 60 min |
| 7 | 20 min |

Total: unas 6 a 7 horas, casi todo medición en el laptop.
