# Fase G: GUI nueva

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** La GUI de hoy funciona, pero tiene cuatro problemas:
- **Muestra el equipo de pruebas en otros equipos:** si el nombre de la CPU y la GPU pasa de 65 caracteres, escribe "AMD Ryzen 5 4600H | Radeon Vega 6 (UMA)" fijo (`src/ui/gui_tabs.cpp:21-28`). En un equipo universal eso es un dato falso.
- **Usa el nombre de otro programa:** "UXTU" (Universal x86 Tuning Utility, un proyecto ajeno) aparece 8 veces, entre ellas en el título de la ventana (`gui_window.cpp:474`) y en la clase de ventana (`:726`).
- **Jerga y mezcla de idiomas:** "Combo Gaming 2B", "Quantum de kernel", "TIMER KERNEL", "AVG FPS". La persona no sabe qué hace cada botón ni si sirve.
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
- La GUI **solo lee** un "modelo de pantalla" que prepara `session/` (por ejemplo, la lista de ajustes con su familia, su estado y su evidencia). Así la GUI no decide nada y cumple la tabla de capas.
- La evidencia de cada ajuste sale de una sola tabla en el código, la misma que resume `docs/mediciones/estado-de-verificacion.md`.
- Textos en español claro; los nombres técnicos (`r.AOQuality`, `MPO`) solo como detalle secundario.

**Terminado cuando:**
- en un equipo simulado se ve su nombre real, nunca el del laptop de pruebas, en los tres casos de `gui_tabs.cpp:21-28`: nombre de más de 65 caracteres, marca de CPU vacía y resumen de GPU vacío;
- no queda ninguna mención a "UXTU"; la clase de ventana se renombra a la vez en `gui_window.cpp:726` y en `main.cpp:59` (`FindWindowW`);
- ningún texto promete una mejora; cada ajuste tiene su ⓘ con el cambio exacto, y solo los que pasan la puerta de evidencia se marcan "Recomendado";
- todos los bordes de controles tienen al menos 3:1 y el foco del teclado se ve;
- cada lista tiene su estado vacío con una acción;
- todo lo que se cambia en Programas y Configuración se guarda en `config.json` y sobrevive a un reinicio;
- se navega con el teclado (Tab) por todos los botones y los textos tienen un contraste de al menos 4,5:1;
- el daemon en reposo sigue por debajo de 15 MB con la GUI cerrada.

## Evidencia (commit `61eed56`)

**GUI ya hecha**
- Insignia del guardián: `src/ui/gui_tabs.cpp:32-44`. Alerta al 85 % y contador: `:434-492`.
