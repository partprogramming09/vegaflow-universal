# Para quién es y qué se muestra

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

## 1. Por qué este plan

VegaFlow corre en Windows **como administrador**: escribe en el registro, cambia archivos de juegos y frena otros procesos. Si un programa con esos permisos se equivoca, el daño es real: un juego con la configuración rota, un archivo escrito donde no debía o un proceso frenado sin motivo.

Al investigar las piezas que queríamos pasar a Rust salieron dos tipos de problema:

1. **Reglas mal planteadas.** Son la mayoría: la regla dice algo distinto de lo que se quería. Da igual el lenguaje; hay que corregir la regla.
2. **Código delicado escrito a mano en C++,** como leer y mezclar textos de configuración. Rust ayuda aquí porque obliga a tratar cada caso (archivo vacío, marcador que falta, otra codificación) y porque se prueba con muchos ejemplos de forma barata.

Por eso cada fase hace las dos cosas a la vez: **la decisión pasa a Rust con la regla ya corregida y las acciones de Windows se quedan en C++,** con sus propias correcciones. Es el mismo reparto de la fase 1: Rust decide y C++ actúa.

## 1b. Para quién es y qué se le muestra

**Por qué.** Quien abre VegaFlow casi nunca quiere aprender cómo funciona Windows: quiere que su juego o su trabajo vaya mejor y fiarse de que no le rompe nada. Cada pantalla y cada texto se juzga con esa vara: **si un dato no le ayuda a decidir algo ni a confiar, no se muestra en el primer nivel.**

**Cuatro tipos de persona:**

| Persona | Qué quiere | Qué necesita ver | Qué hace VegaFlow |
|---|---|---|---|
| Solo quiere jugar | Abrir el juego y que vaya bien | Una frase: "Todo listo" u "Optimizando Elden Ring". Nada más si no pregunta | Actúa solo mientras juega; fuera del juego no cambia nada |
| Jugador curioso | Saber qué se toca y por qué | Qué se cambió, con el ⓘ de cada cosa y la vista previa | Lo mismo, con todo el detalle a un clic |
| Trabajo pesado (render, edición de vídeo, compilar) | Que su programa termine antes y el equipo no se arrastre | Que su programa está reconocido y con prioridad | Lo trata como a un juego mientras trabaja (modo "Jugar y trabajos pesados") |
| Quiere una mejora general siempre | Olvidarse del tema | Qué ajustes quedan aplicados de forma fija | Aplica de forma fija **solo** los ajustes "Recomendado" para su equipo; no frena el fondo fuera de un juego o trabajo pesado |

**Plataformas:** sobremesa con ratón, portátil (con batería), consola portátil con mando y pantalla de 7 a 8 pulgadas, y Windows sobre ARM (solo informa; ver fase 3). La misma información sirve para todas; cambia el tamaño de los controles y cómo se navega (fase G).

**Tres modos de uso** (se elige en la bienvenida y se cambia en Configuración):
- **Solo jugar** (por defecto): actúa durante el juego y devuelve todo al cerrarlo.
- **Jugar y trabajos pesados:** además reconoce programas de trabajo pesado (fase 5) y les da el mismo trato. Es una palanca nueva **sin medir**: hasta medirla con una carga real (un render o una compilación, con A/B), queda como opción y sin "Recomendado".
- **Siempre activo:** deja fijos los ajustes "Recomendado". Hoy ninguno de los ajustes del sistema tiene ganancia medida, así que en la práctica este modo solo añade lo que se mida en el futuro; la GUI lo dice así.

**Tres niveles de información:**

```
+---------------------------------------------+
| Nivel 1: estado en una frase                |
| "Todo listo" / "Optimizando Elden Ring"     |
| Lo ve todo el mundo                         |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Nivel 2: qué se hizo, en palabras normales  |
| "14 programas en modo ahorro"               |
| Un clic                                     |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Nivel 3: detalle técnico exacto             |
| Clave del registro, valor, cómo deshacerlo  |
| Solo en el botón ⓘ                          |
+---------------------------------------------+
```

**Qué no se muestra nunca en los niveles 1 y 2:** cifras de mejora (dependen de cada equipo), claves del registro, identificadores internos (Tier, CCX, EcoQoS sin explicar), siglas sin explicar y datos que no cambian nada de lo que la persona puede hacer.

## 1c. Qué aplica según la gráfica

**Por qué.** Si la GUI muestra lo mismo a todo el mundo, la persona no sabe qué le sirve a su equipo. La pantalla de Ajustes dice primero **qué gráfica usa el juego** y enseña solo lo que aplica a esa gráfica. Lo demás no aparece, y una línea explica por qué ("Tiene memoria propia: no hacen falta los ajustes de memoria compartida").

| Función | Integrada (APU, iGPU) | Dedicada (dGPU) | Externa (eGPU) | Nivel |
|---|---|---|---|---|
| Frenar programas de fondo | Sí | Sí | Sí | Estable; recomendado (ganancia medida) |
| Perfil de juego para gráfica integrada (`Engine.ini`) | Sí | No | No | Estable; recomendado (medido en un juego) |
| Perfil de juego sin recortes (solo caché de shaders) | No | Sí | Sí | Nuevo (fase 3): experimental hasta probarlo |
| Vigilar la memoria compartida | Sí | No | No | Estable tras la fase 4 (hoy no tiene pausa entre purgas) |
| Informar 4 GB de memoria de vídeo | Solo AMD | No | No | Experimental |
| Ajustes del driver de AMD (caché de shaders, teselación) | Si es AMD | Si es AMD | Si es AMD | Experimental |
| Repartir la subida de texturas (Unreal) | No | No | Sí | Experimental (sin fuente fiable) |
| Ahorro del bus PCIe apagado | No | No | Sí | Experimental (sin efecto medido) |
| Ajustes de Windows (reparto de CPU, núcleo en RAM, MPO) | Sí | Sí | Sí | Experimental |
| Plan de energía de alto rendimiento | Sin batería | Sin batería | Sin batería | Estable, sin ganancia medida (por eso no es recomendado) |

**Portátil con dos gráficas:** se usa la columna de la gráfica que usa el juego (fase 3). Si no se sabe, se toma la más prudente: la columna de la dedicada, que no recorta nada.

## 1d. Tres niveles de madurez: estable, experimental y desarrollo

| Nivel | Qué es | Cómo se ve | Por defecto |
|---|---|---|---|
| **Estable** | Probado por la suite, reversible y sin efectos raros conocidos | Siempre visible. Lleva "Recomendado" solo si además tiene ganancia medida | Lo recomendado, activado; lo demás, apagado |
| **Experimental** | Sin probar en ese tipo de equipo, sin medir o sin documentación de Microsoft | Oculto hasta activar "Mostrar funciones experimentales" en Configuración o en Ajustes, con un aviso en ámbar. Cada fila lleva la etiqueta "Experimental" | Apagado siempre |
| **Desarrollo** | Herramientas para probar VegaFlow: simulación sin cambios (`dry_run`), registro detallado, `--monitor`, `--system-info` y los scripts de medición | Solo con "Modo desarrollador" en Configuración, con aviso; la línea de comandos sigue funcionando como hoy | Apagado |

**Regla:** pasar de experimental a estable exige la prueba de la suite y, para llevar "Recomendado", la medición A/B en ese tipo de equipo. Pasar de estable a experimental (por ejemplo, si Windows cambia un comportamiento) solo exige actualizar la tabla de evidencia.

## 1e. Requisitos y advertencias

Comprobados en Microsoft Learn el 2026-11-09:

| Requisito | Detalle | Por qué |
|---|---|---|
| Windows 10, versión 1709 o posterior, de 64 bits | El modo ahorro de los programas de fondo (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED`) existe desde la 1709; los CPU Sets, desde Windows 10. Con Windows 11 es mejor: el modo ahorro es EcoQoS completo (antes se llamaba LowQoS) | Son las dos piezas que más se usan |
| Permisos de administrador | Cambia prioridades de otros procesos y ajustes del sistema | — |
| Procesador x64 | En Windows sobre ARM, VegaFlow corre emulado y solo muestra información | No se ha probado ni medido en ARM |
| Gráfica | Cualquiera; lo que aplica depende del tipo (sección 1c) | — |
| Steam | Opcional: sirve para preparar los juegos solo | — |

**Advertencias que la GUI muestra** (pantalla "Requisitos y advertencias" y bienvenida):
- El resultado depende de cada equipo; no se promete una mejora.
- Con batería no se fuerza el alto rendimiento; si se activa a mano, la batería dura menos.
- Otro optimizador (por ejemplo, el del fabricante del portátil) puede cambiar los mismos ajustes.
- Las funciones experimentales pueden no hacer nada o causar efectos raros; se deshacen desde Historial.
- Si VegaFlow se cierra de golpe, al abrirlo otra vez ofrece deshacer lo que quedó aplicado (fase 2b).

**Pendiente de comprobar:** que el ejecutable arranque en la versión 1709 (hoy se usan funciones de Windows 10 posteriores, como `SetProcessDpiAwarenessContext`, que se carga de forma dinámica). Se prueba al empezar la fase G; si no arranca, el mínimo sube a la versión que se compruebe.

## 1f. Modo desarrollador: que cualquiera pueda hacer sus propias pruebas

**Por qué.** VegaFlow es código abierto y se mantiene con un solo equipo de pruebas. La única forma de saber qué funciona en un ROG Ally, un Intel híbrido o una eGPU es que quien los tiene pueda probarlo con el mismo método que el proyecto y compartir el resultado. El modo desarrollador da esas herramientas sin quitar ninguna protección.

**Lo que no cambia con el modo desarrollador:** no toca juegos con anti-cheat, no escribe fuera de la lista blanca (fase 1) y todo cambio pasa por el diario (fase 2b). Solo deja ver más y probar más.

| Herramienta | Qué hace | Se apoya en | Aviso |
|---|---|---|---|
| Simular sin cambiar nada | Lista todo lo que haría ahora, con el valor actual y el nuevo | El `dry_run` que ya exige la regla del proyecto | No |
| Por qué decidió esto | Para cada programa y ajuste, la regla aplicada y sus datos | El núcleo Rust devuelve la regla junto con la decisión | No |
| Perfil del equipo completo | Todos los datos leídos y los que no se pudieron leer, para copiar | Fase 3, con la lista cerrada de campos (sin datos personales) | No |
| Registro detallado y diario | Lo que pasa en cada vuelta y el diario de cambios | Fase 2b; el registro se borra solo a los 7 días | No |
| Probar como otro tipo de gráfica | Ver qué decidiría con una gráfica integrada, dedicada o externa | Fase 3; **solo en simulación**, nunca se aplica | Sí |
| Activar una función experimental sola | Encender un único cambio, para que una prueba mida solo eso | Sección 1d | Sí |
| Prueba A/B guiada | Alterna sin y con el ajuste en orden ABBA, al menos 3 veces cada uno, con el juego en la misma escena | PresentMon instalado aparte y el método de `scripts/bench.ps1` y `ab_loop_task.ps1` | Sí |
| Exportar para el proyecto | Perfil del equipo + ajuste probado + series de la prueba en un archivo | Formato fijo en `docs/mediciones/` | No |

**La prueba A/B no convierte a VegaFlow en un medidor:** solo existe en el modo desarrollador, usa PresentMon (una herramienta aparte) y enseña las dos series tal cual, sin decir "mejoró". La conclusión la saca la revisión.

**Cómo un resultado de la comunidad cambia algo:**

```
+---------------------------------------------+
| Alguien exporta su prueba A/B y la propone  |
| en el repositorio                           |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Revisión: método ABBA, 3 o más corridas por |
| variante, misma máquina, perfil completo    |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Se guarda en docs/mediciones/ con su fecha  |
| y su clase de equipo                        |
+---------------------------------------------+
                      |
                      v
+---------------------------------------------+
| Con resultados coherentes en al menos dos   |
| equipos distintos de esa clase, el ajuste   |
| pasa a "Recomendado" para esa clase         |
+---------------------------------------------+
```

El umbral de "dos equipos distintos" es una propuesta; se decide cuando llegue el primer resultado. Va en la fase G, después de la vista previa.
