# Documentación de VegaFlow Universal

**Lector:** personas y agentes

Empieza aquí. Cada documento responde una sola pregunta; busca la tuya.

## ¿Qué quieres hacer?

| Quiero... | Leo |
| --- | --- |
| Entender qué es VegaFlow y por qué existe | [`arquitectura/vision-y-principios.md`](arquitectura/vision-y-principios.md) |
| Compilar y correr las pruebas | [`guias/compilar-y-probar.md`](guias/compilar-y-probar.md) |
| Instalar o desinstalar VegaFlow, o comprobar el zip | [`guias/instalar-y-desinstalar.md`](guias/instalar-y-desinstalar.md) |
| Usar el daemon o saber qué hace cada función | [`guias/uso-y-funciones.md`](guias/uso-y-funciones.md) |
| Cambiar una optimización de GPU, memoria o energía | [`arquitectura/hardware-y-tiers.md`](arquitectura/hardware-y-tiers.md), y antes de tocarla, qué se midió en [`mediciones/estado-de-verificacion.md`](mediciones/estado-de-verificacion.md) |
| Tocar el núcleo Rust o la frontera C++ ↔ Rust | [`arquitectura/nucleo-rust-cpp.md`](arquitectura/nucleo-rust-cpp.md) |
| Tocar el anti-cheat o el aislamiento de archivos | [`arquitectura/seguridad.md`](arquitectura/seguridad.md) |
| Saber qué hace cada módulo o dónde está un archivo | [`arquitectura/modulos.md`](arquitectura/modulos.md) |
| Dar soporte a otro hardware (Intel, AMD multi-CCD, dGPU, eGPU) | [`arquitectura/patron-universal.md`](arquitectura/patron-universal.md), sección 3.5: el porqué y las reglas de la fase 3 (tipo de gráfica) y la fase 7 (reparto de núcleos) |
| Saber qué falta por hacer, en qué orden y con qué arquitectura | [`plan/maestro/README.md`](plan/maestro/README.md) |
| Cambiar o rediseñar la GUI | [`plan/maestro/14-fase-g.md`](plan/maestro/14-fase-g.md) |
| Saber qué se investigó ya (APIs de Windows, gráficas, librerías, accesibilidad) antes de buscar en la web | [`investigacion/`](investigacion/README.md) |
| Ver cifras y cómo se midieron | [`mediciones/`](mediciones/) |
| Usar los scripts de medición o los trabajos remotos | [`../scripts/README.md`](../scripts/README.md) |

## Los documentos, por carpeta

Cada documento dice en su segunda línea para quién es (`**Lector:**`).

**`arquitectura/`: cómo funciona**

| Documento | Qué contiene |
| --- | --- |
| `vision-y-principios.md` | Resumen, invariantes del daemon, principios de ingeniería y lo ya construido |
| `nucleo-rust-cpp.md` | Puente FFI C++ ↔ Rust, ciclo de vida del daemon y la especificación del guardián UMA |
| `hardware-y-tiers.md` | UMA, falso límite de 512 MB, GPL, STAPM, los 4 Tiers y su especificación DX11/DX12 |
| `seguridad.md` | Política anti-cheat y aislamiento Zero-Trust de `SandboxGuard` |
| `modulos.md` | Capas del código, SystemInfo, GUI, suite 3D, silicio y el árbol del repo |
| `patron-universal.md` | Por qué pasa en cualquier procesador, qué cambia entre Ryzen e Intel Core y qué decide el código en cada equipo (tipo de gráfica y reparto de núcleos) |

**`mediciones/`: qué se midió.** Informes fechados que no se reescriben: `2026-11-base.md` (línea base), `2026-11-ab-daemon.md` (daemon apagado contra encendido), `2026-11-juegos-reales.md`, `2026-11-octopath2-factorial.md`, `2026-11-09-diagnostico-laptop.md` (estado del laptop y verificación de la investigación), `2026-11-14-paquete-0.1.0.md` (instalar, desinstalar y memoria en reposo del paquete), `2026-11-15-regresion-octopath.md` (puerta de regresión de Octopath con y sin el preset UMA), y `estado-de-verificacion.md`, que resume qué está medido y qué no. Las capturas están en `assets/`.

**`plan/`: qué falta.** `maestro/`: el único plan, con un `README.md` de entrada, un archivo por fase y tres comunes (`producto.md`, `arquitectura.md` y `fuera-del-plan.md`). Arquitectura nueva y módulos, librerías, qué pasa a Rust, arreglos de seguridad, soporte para otro hardware y funciones futuras, en fases.

**`adr/`: decisiones.** `0001-nucleo-que-decide-y-puertos.md` (Rust decide, C++ actúa a través de puertos; aceptado el 2026-11-14).

**`investigacion/`: lo ya investigado.** Un documento por tema, con fuente, fecha y confianza en cada dato, para no repetir búsquedas: `cpu-energia-y-procesos.md`, `graficas-y-perfil-del-equipo.md`, `librerias-y-herramientas.md`, `gui-y-accesibilidad.md`, `recursos-de-windows.md`, `apis-graficas-dx-y-vulkan.md`, `fps-y-estabilidad.md` y `dxvk-descarga.md`. Su `README.md` es el índice: reglas, caducidad de los datos y lo que falta por investigar; se mira antes de buscar en la web.

**`diagrams/`: los diagramas de los documentos.** Cada uno es un `.html` (el original, el que se edita) y su `.png` exportado; el documento muestra la imagen y enlaza al HTML para verlo a pantalla completa. Cómo se cambian: `diagrams/README.md`.

**`guias/`: cómo se hace.** `compilar-y-probar.md` (compilar, probar y generar el paquete), `instalar-y-desinstalar.md` (instalar el zip, desinstalar y probar el paquete) y `uso-y-funciones.md`.

## Reglas

1. Una medición lleva fecha y no se reescribe; si cambia el método, va un archivo nuevo.
2. Un plan hecho se borra: git guarda el historial (los documentos viejos, como la PARTE IV original o los planes de implementación, están en el commit `d59739f`, carpeta `docs/archivo/`).
3. Cada cifra vive en un solo sitio y los demás la enlazan.
4. Un documento nuevo solo se crea si ninguno de los que ya hay responde su pregunta.

Fuera de git: `docs/estado.md`, la libreta de relevo de cada máquina (lo hecho, lo abierto y lo que sigue), y la carpeta `.claude/`.
