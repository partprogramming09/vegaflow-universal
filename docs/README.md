# Documentación de VegaFlow Universal

Empieza aquí. Cada documento responde una sola pregunta; busca la tuya.

## ¿Qué quieres hacer?

| Quiero... | Leo |
| --- | --- |
| Entender qué es VegaFlow y por qué existe | [`arquitectura/vision-y-principios.md`](arquitectura/vision-y-principios.md) |
| Compilar y correr las pruebas | [`guias/compilar-y-probar.md`](guias/compilar-y-probar.md) |
| Usar el daemon o saber qué hace cada función | [`guias/uso-y-funciones.md`](guias/uso-y-funciones.md) |
| Cambiar una optimización de GPU, memoria o energía | [`arquitectura/hardware-y-tiers.md`](arquitectura/hardware-y-tiers.md), y antes de tocarla, qué se midió en [`mediciones/estado-de-verificacion.md`](mediciones/estado-de-verificacion.md) |
| Tocar el núcleo Rust o la frontera C++ ↔ Rust | [`arquitectura/nucleo-rust-cpp.md`](arquitectura/nucleo-rust-cpp.md) |
| Tocar el anti-cheat o el aislamiento de archivos | [`arquitectura/seguridad.md`](arquitectura/seguridad.md) |
| Saber qué hace cada módulo o dónde está un archivo | [`arquitectura/modulos.md`](arquitectura/modulos.md) |
| Dar soporte a otro hardware (Intel, AMD multi-CCD, dGPU, eGPU) | [`plan/maestro/07-fase-3.md`](plan/maestro/07-fase-3.md) y [`plan/maestro/11-fase-7.md`](plan/maestro/11-fase-7.md), con el porqué en [`arquitectura/patron-universal.md`](arquitectura/patron-universal.md) |
| Saber qué falta por hacer, en qué orden y con qué arquitectura | [`plan/maestro/README.md`](plan/maestro/README.md) |
| Cambiar o rediseñar la GUI | [`plan/maestro/14-fase-g.md`](plan/maestro/14-fase-g.md) |
| Saber qué se investigó ya (APIs de Windows, gráficas, librerías, accesibilidad) antes de buscar en la web | [`investigacion/`](investigacion/README.md) |
| Ver cifras y cómo se midieron | [`mediciones/`](mediciones/) |
| Usar los scripts de medición o los trabajos remotos | [`../scripts/README.md`](../scripts/README.md) |

## Los 43 documentos, por carpeta

**`arquitectura/`: cómo funciona**

| Documento | Qué contiene |
| --- | --- |
| `vision-y-principios.md` | Resumen, invariantes del daemon, principios de ingeniería y lo ya construido |
| `nucleo-rust-cpp.md` | Puente FFI C++ ↔ Rust, ciclo de vida del daemon y la especificación del guardián UMA |
| `hardware-y-tiers.md` | UMA, falso límite de 512 MB, GPL, STAPM, los 4 Tiers y su especificación DX11/DX12 |
| `seguridad.md` | Política anti-cheat y aislamiento Zero-Trust de `SandboxGuard` |
| `modulos.md` | SystemInfo, GUI, suite 3D, silicio y el árbol del repo |
| `patron-universal.md` | Por qué pasa en cualquier procesador y qué cambia entre Ryzen e Intel Core |

**`mediciones/`: qué se midió.** Informes fechados que no se reescriben: `2026-11-base.md` (línea base), `2026-11-ab-daemon.md` (daemon apagado contra encendido), `2026-11-juegos-reales.md`, `2026-11-octopath2-factorial.md`, `2026-11-09-diagnostico-laptop.md` (estado del laptop y verificación de la investigación), y `estado-de-verificacion.md`, que resume qué está medido y qué no. Las capturas están en `assets/`.

**`plan/`: qué falta.** `maestro/`: el único plan, con un `README.md` de entrada, un archivo por fase y tres comunes (`producto.md`, `arquitectura.md` y `fuera-del-plan.md`). Arquitectura nueva y módulos, librerías, qué pasa a Rust, arreglos de seguridad, soporte para otro hardware y funciones futuras, en fases.

**`adr/`: decisiones.** `0001-nucleo-que-decide-y-puertos.md` (Rust decide, C++ actúa a través de puertos).

**`investigacion/`: lo ya investigado.** Fuentes oficiales con fecha y confianza, para no repetir búsquedas: `cpu-energia-y-procesos.md`, `graficas-y-perfil-del-equipo.md`, `librerias-y-herramientas.md`, `gui-y-accesibilidad.md`, `recursos-de-windows.md`, `apis-graficas-dx-y-vulkan.md`, `fps-y-estabilidad.md` y un `README.md` con lo que falta por investigar.

**`guias/`: cómo se hace.** `compilar-y-probar.md` y `uso-y-funciones.md`.

## Reglas

1. Una medición lleva fecha y no se reescribe; si cambia el método, va un archivo nuevo.
2. Un plan hecho se borra: git guarda el historial (los documentos viejos, como la PARTE IV original o los planes de implementación, están en el commit `d59739f`, carpeta `docs/archivo/`).
3. Cada cifra vive en un solo sitio y los demás la enlazan.
4. Un documento nuevo solo se crea si ninguno de los 39 responde su pregunta.

Los docs privados (entorno, decisiones, estado, pruebas) no están en git: viven en `.claude/`.
