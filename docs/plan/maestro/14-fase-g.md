# Fase G: GUI nueva

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Estado (2026-11-14).** Las correcciones de «Terminado cuando» que no necesitaban pantallas nuevas **están hechas** (commit `eb0cf5a`, fase R3, versión 0.1.0): la GUI ya no escribe el nombre fijo del laptop de pruebas (la línea «procesador | gráfica» sale de `ui/app_identity.hpp`, con «no identificado» si falta un dato), ya no dice «UXTU» (clase de ventana `VegaFlowUniversalWindow`, título «VegaFlow Universal») y ya no tiene textos que prometan mejoras (se quitaron «0 % OVERHEAD», «Cero VRAM», «0ms» y «4.0 GHz»). **Las 7 pantallas pasan a la versión 0.2.0**; lo que sigue es lo que queda pendiente.

**Actualización (2026-11-15): las pantallas están construidas** (ocho más la de Requisitos, que no estaba entre las siete del diseño; detalle y lo que falta en «Estado de la construcción», más abajo). Esta página conserva el diseño original; la tabla de abajo manda sobre él.

**Por qué.** La GUI de 0.1.0 funciona, pero le quedan dos problemas:
- **Jerga y mezcla de idiomas:** "Combo Gaming 2B", "Quantum de kernel", "TIMER KERNEL", "AVG FPS" (sin revisar si siguen todos en pantalla). La persona no sabe qué hace cada botón ni si sirve.
- **Falta de diseño, no fallo:** no dice qué está medido. Un ajuste sin medir se ve igual que uno con ganancia demostrada, aunque la regla del proyecto es que lo no medido no va por defecto.

**Cómo quedaría:** hay un diseño en un canvas privado (no se enlaza desde este repositorio público). Datos de ejemplo. Siete pantallas, con el menú en cuatro grupos: **Ahora** (Inicio), **Optimizar** (Ajustes del sistema, Juegos, Programas), **Tu PC** (Tu equipo) y **VegaFlow** (Historial, Configuración).

| Pantalla | Qué responde | Qué cambia respecto a hoy |
|---|---|---|
| Inicio | ¿Está funcionando? ¿Qué sabe de mi equipo? | Un estado grande ("Optimizando ahora: juego" o "En espera"), el perfil del equipo con lo que hace VegaFlow con cada dato, la barra de memoria con la línea del 85 % y qué programas se frenaron. Sin FPS |
| Ajustes del sistema | ¿Qué cambia en Windows? | Agrupado por las familias de la fase 8; cada ajuste con una frase de para qué sirve, "Recomendado" cuando corresponde y un ⓘ con el cambio exacto. Solo aparecen las familias que sirven en ese equipo |
| Juegos | ¿Qué le hace VegaFlow a mis juegos? | Juegos con anti-cheat marcados como protegidos; para los demás, qué archivo cambia, dónde está la copia y qué valores escribe; botón "Volver al archivo original" |
| Tu equipo | ¿Cómo ve VegaFlow mi PC? | Mapa de núcleos (juego, fondo, Windows), gráficas con su tipo de conexión (fase 3) y el nivel de 1 a 4 explicado |
| Historial | ¿Qué tocó y cómo lo deshago? | Cada cambio con el valor de antes y el de ahora, y "Deshacer" por fila |
| Programas | ¿Qué hace con mis otros programas? | Nuevo. Una acción por tipo de programa (frenar, solo ahorro, no tocar), excepciones por programa y juegos añadidos a mano (fase 5) |
| Configuración | ¿Cómo se comporta VegaFlow? | Nuevo en la GUI (hoy solo existe `config.json`). General (iniciar con Windows, bandeja), detección, los umbrales de memoria de `MemoryPurgeConfig`, opciones avanzadas marcadas "No recomendado" cuando se midió que empeoran y copias y restauración. Marcados como "Nuevo": aviso al empezar a optimizar y exportar la configuración |

**Reglas de contenido (decididas el 2026-11-09):**
- **No se prometen mejoras.** Nada de "+40 %" ni "bajó un 19 %" en la GUI: el resultado depende de cada equipo. Las cifras medidas se quedan en `docs/mediciones/`.
- **"Recomendado" en lugar de cifras.** Significa "VegaFlow lo activaría en un equipo como el tuyo". Por dentro sale de la puerta de evidencia: solo es recomendado lo que se midió con ganancia en esa clase de equipo, y lo que se midió con pérdida se marca "No recomendado".
- **Cada ajuste tiene un botón ⓘ** que explica exactamente qué cambia (qué clave del registro u opción de Windows, con qué valor) y que se puede deshacer. Se abre con clic o con el teclado, no solo al pasar el ratón.
- **Inicio muestra el perfil del equipo** (fase 3), no FPS. Cada dato dice qué hace VegaFlow con él, y lo que no se pudo leer aparece en ámbar con "se usan valores seguros".

**Auditoría UX/UI del diseño (2026-11-09).** Revisé el código del diseño y calculé los contrastes con la fórmula de WCAG. No lo abrí en un navegador, así que el foco del teclado y las capturas quedan sin comprobar.

| # | Gravedad | Hallazgo | Medida | Arreglo |
|---|---|---|---|---|
| 1 | Accesibilidad (1.4.11) | Bordes de botones y campos, e interruptor apagado, casi invisibles | `#3A4150` sobre `#16191E`: 1,72:1; sobre `#1B1E24`: 1,63:1 (mínimo 3:1) | Cambiado a `#6E7787`: 3,9:1 y 3,7:1 |
| 2 | Accesibilidad (1.4.3), **en la GUI actual** | `CLR_TEXT_DIM` se usa para texto en 2 sitios de `gui_tabs.cpp` | `#6B7280` sobre `#1B1E24`: 3,45:1 (mínimo 4,5:1) | No usarlo para texto; `#9CA3AF` da 6,58:1 |
| 3 | Accesibilidad (2.4.7) | El diseño no define cómo se ve el foco del teclado | Ninguna regla de foco en los 7 archivos | En GDI, un borde de 2 px `#93B4F8` (8,05:1 sobre las tarjetas) en el control con foco |
| 4 | Mayor | Una acción con tres nombres: "Devolver todo a como estaba", "Restaurar todo" y "Volver a los valores de fábrica" (que en realidad es otra cosa) | Ajustes, Historial y Configuración | Unificado: "Devolver todo a como estaba" para el sistema; "Restablecer esta configuración" para las opciones de VegaFlow |
| 5 | Mayor | "Solo ahorro" y "Solo ahorro de energía" para lo mismo | Programas | Unificado en "Solo ahorro" |
| 6 | Mayor | Faltan estados vacíos: sin biblioteca de Steam, Historial sin cambios, sin excepciones | No hay diseño para esos casos | Cada uno con una frase y una acción ("Añadir un juego", "Todavía no se ha cambiado nada") |
| 7 | Mayor | "Dejar de optimizar este juego" no dice qué pasa | Inicio | Renombrado a "Pausar VegaFlow en este juego": devuelve el juego y el fondo a lo normal hasta que se cierre el juego |
| 8 | Nota | Algunos controles miden 40 px de alto | Programas y Configuración | Pasa el mínimo de 24 × 24 px (2.5.8, alto y ancho) a 96 ppp; en GDI se escala con el DPI. WCAG es una norma web: aquí se aplica por analogía |

Lo que ya está bien: el texto secundario tiene entre 5,69:1 y 6,94:1, las etiquetas de color entre 7,60:1 y 9,47:1 y el botón principal 5,17:1. Todo pasa el 4,5:1 de WCAG AA.

**Pantallas y piezas por tipo de gráfica y madurez (secciones 1c a 1e):**
- **Ajustes según la gráfica:** arriba, "Tu gráfica" con su tipo; debajo, solo las familias que aplican. Las experimentales, ocultas tras un interruptor con aviso.
- **Configuración → Funciones avanzadas:** "Mostrar funciones experimentales" y "Modo desarrollador", los dos apagados de fábrica y con aviso.
- **Requisitos y advertencias:** una pantalla propia, enlazada desde Configuración y desde la bienvenida.

**Pantallas y piezas nuevas por los perfiles de uso (sección 1b):**
- **Bienvenida** (primera vez): qué es tu equipo, qué hará VegaFlow y qué nunca hará, y elegir el modo de uso.
- **Inicio en el nivel 1:** una frase de estado arriba; el resto, debajo y opcional.
- **"Qué puede hacer este programa"** (adoptado el 2026-11-09), en Configuración: qué lee, dónde escribe (sale de la lista blanca de la fase 1) y qué nunca toca. Solo afirma lo que el código cumple; por ejemplo, "no se conecta a internet" es cierto hoy (ninguna conexión de red en `src/`, comprobado el 2026-11-09) y una prueba debe vigilar que siga siéndolo.

**Mejoras adoptadas de la ronda de ideas (2026-11-09):**
- **Vista previa "qué haría en tu equipo":** muestra el resultado del `dry_run` que ya exige la regla del proyecto, con el valor actual y el nuevo, antes de aplicar nada. Es lo que hace comprobable el "Recomendado".
- **Informe del equipo para copiar:** el perfil del equipo, lo que se pudo leer y lo que no, y los cambios aplicados, en texto plano y con una lista cerrada de campos (sin nombre de usuario ni rutas personales). Sirve para pedir ayuda y para conseguir datos de equipos que no tenemos.
- **Pantalla pequeña y mando:** una columna con controles grandes en pantallas estrechas, todo accesible con Tab, y el mando (XInput) solo cuando la ventana de VegaFlow tiene el foco, para no robárselo al juego. Las resoluciones y escalas reales de los portátiles de mano están **sin verificar**.
- **"Algo va mal":** pausar, devolver todo y copiar el informe, accesible desde la bandeja y un atajo configurable.
- **El ⓘ se genera de la misma tabla** que decide los ajustes, nunca se escribe aparte.

**Controles:** los botones de hoy son rectángulos dibujados que se detectan con `PtInRect` (`gui_window.cpp:532-564`). No hay foco, Tab ni campos de texto, y un lector de pantalla no ve nada. Por eso los botones, interruptores y campos se hacen con **controles nativos de Win32** (`BUTTON`, `EDIT`, `COMBOBOX`), que traen el foco, el teclado y la accesibilidad de Windows sin trabajo extra; GDI queda para tarjetas, barras y etiquetas.

**Más mejoras propuestas:**
- **Primera vez:** una pantalla corta al abrir VegaFlow por primera vez. Muestra el perfil del equipo, lo que va a hacer y lo que no (nada con anti-cheat), y deja elegir si arranca con Windows.
- **Aviso al salir:** si al cerrar quedan cambios aplicados, ofrecer "Devolver todo a como estaba" o "Dejarlos".
- **Atajo de pánico:** una combinación de teclas que pausa VegaFlow al momento, por si un juego va mal.
- **Escala de Windows:** respetar 125 % y 150 %. La GUI ya pide reconocer el DPI (`gui_window.cpp:710-716`); falta comprobar que todo se redibuja bien.
- **Idioma:** los textos en un solo archivo, para poder traducirlos al inglés sin tocar el código de dibujo.

**Cómo se construye (sin librerías nuevas):**
- Sigue siendo Win32/GDI. El diseño usa solo lo que GDI dibuja bien: rectángulos redondeados, bordes de 1 px, barras y texto Segoe UI, con los mismos colores de `gui_components.hpp`.
- Componentes de dibujo reutilizables en `src/ui/`: tarjeta, etiqueta (chip), interruptor, barra con marca, fila de lista. Hoy cada pestaña dibuja a mano.
- **Heredado de R1 y R2:** la lógica de sesión de `src/app/gui_mode.cpp` (una función de 500 líneas con lambdas que comparten estado) pasa aquí a un objeto de `session/`. No se hizo en R2 porque esas lambdas devuelven `GuiDashboardData`, que vive en `ui/` (y `session/` no puede incluir `ui/`), porque la GUI solo se puede verificar abriéndola en el escritorio, y porque esta fase la reescribe.
- La GUI **solo lee** un "modelo de pantalla" que prepara `session/` (por ejemplo, la lista de ajustes con su familia, su estado y su evidencia). Así la GUI no decide nada y cumple la tabla de capas.
- La evidencia de cada ajuste sale de una sola tabla en el código, la misma que resume `docs/mediciones/estado-de-verificacion.md`.
- Textos en español claro; los nombres técnicos (`r.AOQuality`, `MPO`) solo como detalle secundario.

**Terminado cuando** (quitados los puntos ya hechos: nombre del laptop, «UXTU» y textos que prometen):
- cada ajuste tiene su ⓘ con el cambio exacto, y solo los que pasan la puerta de evidencia se marcan "Recomendado" (y ningún texto nuevo promete una mejora);
- todos los bordes de controles tienen al menos 3:1 y el foco del teclado se ve;
- cada lista tiene su estado vacío con una acción;
- todo lo que se cambia en Programas y Configuración se guarda en `config.json` y sobrevive a un reinicio;
- se navega con el teclado (Tab) por todos los botones y los textos tienen un contraste de al menos 4,5:1;
- el daemon en reposo sigue por debajo de 15 MB con la GUI cerrada.

## Estado de la construcción (2026-11-15)

Fuente de cada fila: el código en `src/ui/*_view.*`, `src/session/*_screen.*` y `src/app/gui_mode.cpp`. Suite de C++ en el laptop: `TEST RESULTS: 269 PASSED` (`scripts/remote/vfdev cross-test`). Cómo se reparte el código: [`modulos.md`](../../arquitectura/modulos.md), sección 3.3b.

**Hecho**

| Pieza del diseño | Estado |
|---|---|
| Inicio, Ajustes del sistema, Juegos, Historial, Configuración, Programas, Tu equipo | Construidas con controles nativos (Tab, foco visible) y su modelo en `session/` |
| Requisitos y advertencias | Construida; se abre desde Configuración (detalle en [`producto.md`](producto.md), sección 1e) |
| «Volver al archivo original» y excluir un juego | En Juegos: «Volver al archivo original» y «Me fue mal en este juego» (con «Volver a incluir»). El botón antiguo «Aplicar Preset APU» ya no existe: es «Aplicar perfil» |
| Estados vacíos con frase | Historial, Juegos (sin biblioteca de Steam) y Programas (sin excepciones) tienen su texto |
| Una acción, un nombre | «Devolver todo a como estaba» (sistema) y «Restablecer esta configuración» (opciones de VegaFlow) |
| Textos en un solo archivo | `src/ui/texts.hpp` |
| Contraste y foco | Prueba de contraste de la paleta (`tests/unit/contrast_unit_tests.cpp`); anillo de foco en los controles nativos |
| Lógica fuera de `gui_mode.cpp` | `GameActions`, `SettingsActions`, `ConfigActions` y `RestoreAllMessage` están en `session/` y se prueban con falsos; quedan en `gui_mode.cpp` el sondeo de telemetría, purgar, aislar, restaurar la afinidad y el sondeo del historial (ver `modulos.md`) |
| Interruptor «Mostrar funciones experimentales» | En Ajustes del sistema, con aviso y etiqueta «Experimental» por fila |

**No construido**

- «Pausar VegaFlow en este juego» (la pantalla Inicio no lo tiene).
- Bienvenida de la primera vez.
- Atajo de pánico y «Algo va mal» (pausar, devolver todo y copiar el informe).
- Mando (XInput).
- Vista previa «qué haría en tu equipo» (el `dry_run` en pantalla).
- Informe del equipo para copiar.
- Aviso al salir con cambios aplicados.
- Configuración → «Funciones avanzadas» con «Modo desarrollador», y los grupos «General» (iniciar con Windows, bandeja), «Qué puede hacer este programa» y exportar la configuración: la pantalla Configuración tiene hoy cuatro grupos (Modo de uso, Detección de juegos, Memoria compartida y Avanzado, este último solo con «Limitar el juego a sus núcleos»).
- **Escalado por DPI:** la GUI no escala sus tamaños, y **las capturas a 125 % y 150 % no se hicieron**.
- Lector de pantalla: sin probar.

**Terminado cuando**, punto por punto

- Cada ajuste con su ⓘ y «Recomendado» solo con evidencia de ganancia: hecho en el modelo (`Evidence::Gain` es lo único que dice «Recomendado»).
- Bordes con 3:1 y foco visible: hecho en el código y en la prueba de contraste; no se verificó con capturas.
- Estado vacío con acción en cada lista: hay frase en las tres listas; la «acción» de cada una (por ejemplo «Añadir un juego») no se comprobó pantalla por pantalla.
- Programas y Configuración se guardan en `config.json`: hecho, y la edición solo se da por buena si el archivo se escribió (`ConfigActions`).
- Navegación con Tab y contraste 4,5:1: hecho en código; con el teclado se activa con la barra espaciadora (no con Intro).
- Daemon en reposo bajo 15 MB: medido sin la GUI en la sesión 1 (ver [`estado-de-verificacion.md`](../../mediciones/estado-de-verificacion.md)); **con la GUI abierta en el escritorio no se midió**.

## Decisiones pendientes que salieron al construir el catálogo (2026-11-15)

El catálogo de ajustes (`src/session/settings_catalog.*`, Prueba 61) se comparó fila por fila con lo que escribe el código. Quedan estas decisiones para el usuario; la prueba 61.10 fija la lista actual para que cambiarla sea a propósito:

- **Cuatro ajustes sin medir se aplican solos**, contra la regla «lo no medido no va por defecto»: la memoria de vídeo informada (`main.cpp`, al arrancar con administrador), `Win32PrioritySeparation` y GameDVR (en cada juego detectado, `game_session.cpp`) y el perfil de energía (si la política fuerza el plan de alto rendimiento). Medidos sin efecto: `Win32PrioritySeparation` y GameDVR. La pantalla debe decirlo tal cual; apagarlos de fábrica es un cambio de comportamiento que pide su propia aprobación.
- **«Caché de shaders» y «teselación» no son ajustes sueltos:** el código los escribe juntos con seis valores más del driver de AMD. El catálogo los muestra como un solo «Perfil del driver de AMD».
- **La compresión de memoria y GameDVR** no estaban en el diseño del canvas; ya tienen fila.
- **Sin probar todavía:** que cada fila coincida con lo que escribe el optimizador, con un `RegistryPort` en memoria como la Prueba 57. Hoy se comprobó leyendo el código.
- **Aviso «se aplica al reiniciar» en Configuración: sin decidir (2026-11-15).** Configuración guarda en `config.json` al momento, pero no se ha decidido si la pantalla debe avisar de que algunos valores solo los lee el daemon al arrancar. Cuáles son esos valores no se comprobó al escribir esto.
- **«Devolver todo» escribe valores por defecto de Windows sin copia previa** en varios ajustes (detalle en [`estado-de-verificacion.md`](../../mediciones/estado-de-verificacion.md)). Sin decidir si se corrige.
- **Guardián UMA:** usa un contador de memoria de vídeo que probablemente nunca refleja el uso de un juego (mismo documento). Sin decidir si se corrige.

## Evidencia (commit `61eed56`)

**GUI ya hecha**
- Insignia del guardián: `src/ui/gui_tabs.cpp:32-44`. Alerta al 85 % y contador: `:434-492`.
