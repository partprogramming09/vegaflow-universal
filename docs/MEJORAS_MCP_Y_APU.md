# Mejoras pendientes: herramientas remotas (MCP) y rendimiento de la APU

Fecha: 2026-11-04. Recoge lo que falló, lo que hubo que hacer a mano y lo que conviene mejorar tras la sesión de medición remota sobre el laptop de pruebas (Ryzen 5 4600H, Windows 11 en español). Cada punto dice si está medido o es una propuesta.

## 1. Herramientas remotas

### vfjob (`scripts/remote/`, servidor `vfjob_mcp.py` y CLI `vfjob`)

Fallos que ocurrieron y quedaron corregidos:
- `vfjob_status` se colgó 30 minutos porque `ssh` se tragaba la entrada del cliente MCP. Corregido con `stdin=DEVNULL` y una prueba de regresión.
- El JSON de estado llegaba con BOM (`utf-8-sig`), el `tr` de macOS fallaba con bytes no válidos (`LC_ALL=C`) y el tiempo de espera total se reiniciaba en cada vuelta.
- `ExitCode` salía vacío hasta forzar `$null = $proc.Handle`.
- El hueco de permisos en Windows en español: `icacls` con nombres en inglés fallaba sin avisar y la carpeta quedó escribible por Usuarios. Ahora usa SID y comprueba el resultado.

Sigue pasando y conviene mejorarlo:
- **El trabajo `ab` falla si el juego no está abierto**, y `bg-factors` y `ab` cierran `drawstress` al terminar. Lo encontré tres veces (`drawstress.exe ya no esta corriendo`). Propuesta: un trabajo `session` que abra el juego, ejecute la medición y lo cierre, o que `ab` y `bg-factors` lo abran solos.
- **Cada análisis lo hice con un script de Python suelto** que leía las filas de salida. Propuesta: `vfjob stats ARCHIVO` que agregue por condición (media, mínimo, máximo) y compare dos prefijos.
- **No hay trabajo de sincronizar, compilar y probar.** Hice a mano: `git bundle`, `scp`, `fetch` y `merge --ff-only`, más la ruta de `g++` en el PATH. El repositorio del laptop está en `C:\Users\testuser\OneDrive\Escritorio\proyectos\direct` y no estaba anotado; lo localicé buscando carpetas `.git`. Propuesta: trabajos `sync` y `build-test` con esa ruta en la configuración, y la ruta en `scripts/remote/README.md`.
- **Salida de PowerShell por SSH con `-EncodedCommand`** incluye ruido CLIXML (`#< CLIXML`) que hay que filtrar con `grep`. Propuesta: que los trabajos devuelvan solo su salida.
- **La instalación inicial de las tareas programadas por SSH la denegó el clasificador de Claude Code** ("Unauthorized Persistence"); la hizo el usuario. Es un límite de diseño, no un fallo. Queda anotado en la memoria del proyecto.
- **El servidor MCP en ejecución usa el código viejo hasta reconectar con `/mcp`.** Tras cada cambio hay que pedirlo al usuario. Propuesta: el cliente `vfjob` (CLI) para todo y el MCP solo para listar y lanzar.
- **No se pueden hacer llamadas MCP concurrentes** a `vfjob`; las cadenas largas se lanzan con el CLI en segundo plano.
- **Medir requiere un reinicio previo y esperar unos minutos** (el sistema recién arrancado distorsiona). No hay forma de reiniciar el laptop de forma remota que vuelva sola a una sesión de escritorio: el autologon falló (4625 / 0xC000006D, cuenta Microsoft).
- **`vfjob run` se caía con `TimeoutExpired` cuando el laptop tardaba en responder por SSH mientras medía** (dos veces, en capturas largas). Dejó dos trabajos solapados: el segundo cerró el juego del primero y se perdió una medición. Parcheado: `ssh()` devuelve vacío al agotar el tiempo y el bucle reintenta. Propuesta de fondo: que el ejecutor impida lanzar `game` mientras hay un trabajo `bg-factors` o `ab` en curso.
- **El tiempo de espera de `vfjob run` (900 s por defecto) es menor que un trabajo largo** (capturas de 10 min). Dos llamadas encadenadas (`kill-game`, `doctor`) dieron "sin respuesta tras 900s" porque la tarea `VegaFlow-Run` pone en cola y espera al trabajo anterior; luego sí se ejecutaron. Propuesta: usar el límite de tiempo del trabajo del manifiesto como espera por defecto.
- **Los umbrales de los trabajos son valores sueltos** (60, 90, 3600 s). Propuesta: que el tiempo límite del trabajo viva en el manifiesto y se pueda ver con `vfjob_list`.

### WinBridge

- `RunTask` tiene un límite de 60 s, no admite argumentos y corre sin elevar; no sirve para mediciones largas. Para lanzar `drawstress` en la sesión de escritorio hubo que usar `Shortcut` (Win+R) y `Type`, a mano, antes de existir el trabajo `game`.
- Lo que sí aportó: `Snapshot` para ver la pantalla y comprobar que la sesión estaba desbloqueada.
- Pendiente de decisión: usarlo como pasarela (gateway) hacia los demás servidores MCP. No se hizo.

### laptop-trace

- Falló al conectar al inicio de la sesión y se reconectó con `/mcp`. No se investigó la causa.
- Sus instrucciones lo describen para Metal System Trace en Apple silicon; las mediciones de VegaFlow se hicieron con PresentMon vía `bench.ps1` y `scripts/remote/`, no con sus herramientas. Se decidió no meter `vfjob` dentro de `laptop-trace`, porque es más complejo y tiene cosas que VegaFlow no necesita.

### Otros puntos a mano que quedaron

- El usuario inició sesión en el laptop tras cada reinicio, y reconectó MCP con `/mcp` varias veces.
- El CI de GitHub no arranca (límite de gasto); el usuario decidió dejarlo así. Las pruebas corren en el laptop con `build.ps1 -RunTests`.

## 2. Rendimiento de la APU

### Medido (ver `docs/MEDICION_AB_DAEMON.md`)

- Quitado por costar FPS: `CPMINCORES=100` y confinar al juego a 8 de 12 hilos.
- Ganancia demostrada: aislar el fondo con el juego sin restringir. Crece con la carga de fondo: +14% con 1 worker, +45% con 4, 2.1 veces con 6.
- Confinar solo cuando el fondo consume al menos 0.5 procesadores lógicos (por debajo no ayuda y a 0.23 cuesta un 5%).
- Número de hilos para el fondo: 2 de 12 es lo mejor medido; en un equipo simulado de 4 hilos, 1 hilo supera a 2 por mucho. Regla actual: un núcleo físico completo con 4 o más núcleos, un hilo con menos (medida con simulación en 4, 6, 8 y 12 hilos).
- El perfil de energía ahora restaura los valores previos y las pruebas dejan el plan intacto.

### Por medir (propuestas, nada de esto está probado)

- **Carga limitada por GPU.** `drawstress` está limitado por CPU y driver. Con `DRAWSTRESS_DIM` mayor puede medirse un caso limitado por GPU, donde el aislamiento del fondo y el perfil de energía podrían comportarse distinto.
- **Térmico sostenido.** Las capturas duran 30 s. El perfil de energía y el tope de frecuencia pueden importar en 10 a 30 minutos; no hay medición de ese caso, ni de potencia ni de temperatura.
- **Contención de memoria UMA.** La carga de fondo fue de CPU pura. Una carga que sature el ancho de banda de memoria (CPU y GPU comparten ~40 GB/s) podría dañar al juego aunque esté confinada a pocos hilos.
- **Disco y prioridad de I/O.** `drawstress` no usa disco; la prioridad de I/O no se pudo medir. Falta una carga de juego con streaming de texturas.
- **Sin medición propia:** DirectFlip y MPO, `DisablePagingExecutive`, teselación 16x, AsyncCompute desactivado, DXVK-GPLAsync, FSR y purga de standby.
- **Hilos de fondo en otros tamaños de CPU.** La regla del núcleo completo se apoya en 4, 6, 8 y 12 hilos simulados; 16 hilos y más son extrapolación. En equipos con núcleos híbridos (E-cores) el fondo ya va a los eficientes y no se midió.
- **Umbrales del confinamiento (0.5 y 0.25).** Quedan respaldados por una carga sintética en un equipo; falta repetir con cargas reales de fondo (navegador, launcher) y con otro juego.
- **Latencia del daemon.** Confinar tarda hasta 1.5 s en aparecer una carga; no se midió su efecto en los tirones al inicio de la carga.
- **Restauración.** Los restauradores de `tweaks_state.ps1` siguen escribiendo valores fijos; falta guardar y devolver los valores previos del registro del driver de AMD y de GameDVR.
- **NPU.** No participa en esta optimización; si un proceso de IA en segundo plano usa CPU o iGPU, el confinamiento por consumo debería cubrirlo, pero no se probó.

### Límites de todo lo anterior

Una sola máquina (Zen 2, 6 núcleos), una sola carga de juego sintética (`drawstress`, D3D11 salvo donde se indica D3D12), ninguna prueba con un juego real y ninguna con otro tipo de equipo. Los equipos más pequeños se simularon con afinidad en el mismo laptop; no son equipos reales.

## 3. Comparación del ejecutor de WinBridge con `vfjob` (2026-11-04)

`bg-factors` (confinamiento, 4 workers, 3 rondas de 30 s, D3D11), mismos scripts en los dos ejecutores, orden vieja-nueva-nueva-vieja, 6 capturas por condición y ejecutor.

- Primer intento inválido: en el ejecutor nuevo Chrome no arrancaba (`procesos=0`) porque su `TEMP` protegido (`C:\ProgramData\WinBridgeJobs\tmp`) no sirve de perfil. Se arregló usando la carpeta temporal del usuario en `bg_factors.ps1` y `ab_loop_task.ps1`.
- Resultado válido (FPS medio [rango]): viejo sin aislar 75.3 [73.3-76.8], viejo confinado 104.0 [102.2-105.5]; nuevo sin aislar 78.2 [76.6-80.5], nuevo confinado 117.3 [114.1-120.1]. Sin aislar el nuevo da +3.9% (rangos que se tocan); confinado da +12.7% (rangos que no se solapan). Mismo modo de presentación (`Hardware: Independent Flip`) en las 24 capturas.
- Conclusión: el ejecutor nuevo todavía no es equivalente al viejo en la condición confinada, y no se sabe por qué. Sin aislar está dentro del ruido. `vfjob` y `VegaFlow-Run` se quedan.
- Siguiente experimento posible (lo decide el usuario): volcar el entorno (`Get-ChildItem Env:`) dentro de un trabajo en cada ejecutor y comparar, o repetir con el `PATH` y `PSModulePath` completos del usuario en el paquete nuevo.
- Experimentos posteriores de la sesión de WinBridge, 8 pasadas intercaladas (Mode=confine, 4 workers, 3 rondas de 30 s, D3D11), con el diagnóstico `Diag=1` (y 2 controles del viejo con `Diag=0`): la condición confinada es bimodal en los dos ejecutores. Cada pasada da ~100-105 FPS con ~44 s de CPU del fondo (modo bajo) o ~107-114 FPS con ~59 s (modo alto), y el modo es constante dentro de la pasada (las 3 rondas coinciden) y cambia entre pasadas. Nuevo: 2 altas y 1 baja; viejo con `Diag=1`: 1 alta y 2 bajas (una de ellas con una ronda atípica); viejo con `Diag=0`: 1 alta y 1 baja. Con todas las pasadas de la otra sesión, el nuevo dio alto en ~7 de 10 y el viejo en ~3 de 10, una diferencia que con tan pocas pasadas no es concluyente. Prioridad, afinidad, EcoQoS y CPU Sets del juego, de PresentMon y de Chrome son idénticos en los dos ejecutores. En las capturas del modo alto los hilos lógicos 10 y 11 están al 100% (Chrome confinado ahí) y los hilos 6 a 9 casi libres.
- Conclusión: el ejecutor no causa la bimodalidad; ni el `TEMP` ni el `PATH` la explican. La causa más probable (no probada) es dónde coloca el planificador los hilos del juego respecto a los hilos 10 y 11 al arrancar el juego de cada trabajo. El ejecutor nuevo sirve para medir si cada condición se repite en varias pasadas y se compara dentro de la misma pasada. No se ha decidido retirar `vfjob`.
- Consecuencia para las mediciones de VegaFlow: comparar condiciones solo dentro del mismo trabajo (el mismo juego abierto). Entre trabajos distintos la condición confinada puede variar hasta ~10% sin que cambie nada. Las comparaciones de este repositorio (umbrales, hilos, simulaciones, daemon encendido contra apagado) rotan sus condiciones dentro de un mismo trabajo, así que no dependen de esto; sí dependen las cifras absolutas de un trabajo frente a las de otro.
