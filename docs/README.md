# Documentación de VegaFlow Universal

Cada carpeta responde una pregunta. Las reglas y los comandos del proyecto están en [`CLAUDE.md`](../CLAUDE.md); la presentación del producto, en el [`README.md`](../README.md) de la raíz.

| Carpeta | Pregunta que responde | Qué hay |
| --- | --- | --- |
| [`arquitectura/`](arquitectura/) | ¿Cómo funciona? | Visión e invariantes, núcleo híbrido C++20 y Rust, hardware APU y Tiers, seguridad y SandboxGuard, patrón universal AMD e Intel, módulos y estructura del repo, base consolidada, las dos especificaciones de diseño |
| [`mediciones/`](mediciones/) | ¿Qué se midió? | Informes fechados, con método y límites: línea base, A/B del daemon, juegos reales, Octopath II factorial, y el estado de verificación (qué está medido y qué no). Las capturas están en [`assets/`](assets/octopath2/) |
| [`plan/`](plan/) | ¿Qué falta? | Plan universal AMD, Intel, dGPU y eGPU (verificado contra el código), los pilares de GUI, perfiles y telemetría, y la propuesta de reestructura (¿más Rust?, estructura de archivos, qué se commitea) |
| [`guias/`](guias/) | ¿Cómo se hace? | Compilar y probar, usar el daemon, qué hace cada función |
| [`archivo/`](archivo/) | ¿Qué hubo antes? | Documentos superados o ya hechos; solo consulta |

## Cuál leer primero

- Cambiar una optimización: [`arquitectura/hardware-apu-y-tiers.md`](arquitectura/hardware-apu-y-tiers.md) y las mediciones de la palanca.
- Trabajar con el núcleo Rust: [`arquitectura/nucleo-hibrido.md`](arquitectura/nucleo-hibrido.md).
- Soporte para otro hardware: [`plan/universal-amd-intel-egpu.md`](plan/universal-amd-intel-egpu.md).
- Compilar: [`guias/compilar-y-probar.md`](guias/compilar-y-probar.md).

## Reglas

1. Una medición lleva fecha y no se reescribe; si cambia el método, va un archivo nuevo.
2. Un plan hecho pasa a `archivo/`.
3. Cada cifra vive en un solo sitio y los demás la enlazan.

## Archivo

| Documento | Por qué está archivado |
| --- | --- |
| `INVENTARIO_TECNICO_Y_HOJA_DE_RUTA.md` | Inventario de 28 módulos de la versión 1.2, anterior al núcleo Rust; sus enlaces apuntan a archivos que ya no existen |
| `PLAN_REDISENO_Y_ACTUALIZACION.md` | Borrador del 2026-11-04; incluye el ejecutor `vfjob`, ya retirado |
| `HANDOFF_VFJOB_A_WINBRIDGE.md` | Entrega a la sesión de WinBridge, hecha el 2026-11-05 |
| `2026-11-01-apu-dx11-dx12-universal-guards.md` | Plan de implementación de los guardianes por Tier, ya implementado |
| `2026-11-06-rust-hybrid-uma-and-mcp.md` | Plan de implementación del núcleo híbrido Rust, ya implementado |
| `PARTE_IV_plan_original_sin_verificar.md` | Texto original del plan universal, sin verificar; vale el de `plan/` |
| `MEJORAS_MCP_Y_APU.md` | Mejoras pendientes de las herramientas remotas y la APU; `vfjob` ya está retirado |
| `PLAN_EJECUCION_AUTONOMA.md` | Plan de ejecución autónoma de la sesión de medición, ya cerrado |
| `README_casos_de_estudio_sin_medir.md` | Cifras del README antiguo que las mediciones no respaldan (Zenless) |

Los docs privados (entorno, decisiones, estado, pruebas) no están en git: viven en `.claude/`.
