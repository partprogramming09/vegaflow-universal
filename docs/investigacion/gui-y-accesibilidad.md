# GUI y accesibilidad

**Lector:** personas y agentes. Comprobado el 2026-11-09. Reglas en el [README](README.md).

## Umbrales de WCAG 2.2 (nivel AA)

WCAG es una norma para la web; en una aplicación de escritorio se aplica por analogía. Fuente: [WCAG 2.2](https://www.w3.org/TR/WCAG22/).

| Criterio | Qué pide | Matiz |
|---|---|---|
| 1.4.3 Contraste del texto | 4,5:1 (3:1 a partir de 24 px, o 18,66 px en negrita) | — |
| 1.4.11 Contraste de lo que no es texto | 3:1 para bordes y estados de los controles | Solo si el borde es lo único que deja ver el control; los controles desactivados quedan exentos ([Understanding 1.4.11](https://www.w3.org/WAI/WCAG22/Understanding/non-text-contrast.html)) |
| 1.4.13 Contenido al pasar el ratón | Se puede cerrar, se puede poner el puntero encima y no desaparece solo | Por eso el ⓘ se abre con clic o teclado |
| 2.4.7 Foco visible | El control con foco se ve | En GDI hay que dibujarlo; los controles nativos lo traen |
| 2.5.8 Tamaño del objetivo | 24 × 24 px como mínimo, alto y ancho | En píxeles CSS, que equivalen a 96 ppp: en GDI se escala con el DPI ([Understanding 2.5.8](https://www.w3.org/WAI/WCAG22/Understanding/target-size-minimum.html)) |

## Contrastes medidos de la paleta

Calculados con la fórmula de WCAG el 2026-11-09. No hace falta volver a calcularlos mientras no cambien los colores.

| Par | Contraste | ¿Pasa? |
|---|---|---|
| Texto secundario `#9CA3AF` sobre tarjeta `#1B1E24` | 6,58:1 | Sí |
| `#9CA3AF` sobre menú `#16191E` | 6,94:1 | Sí |
| Texto atenuado `#8B93A1` sobre `#16191E` | 5,69:1 | Sí |
| Enlace `#93B4F8` sobre `#1B1E24` | 8,05:1 | Sí |
| Blanco sobre botón `#2563EB` | 5,17:1 | Sí |
| Etiquetas de color (ámbar, verde, rojo y azul sobre su fondo) | 7,60:1 a 9,47:1 | Sí |
| `CLR_TEXT_DIM` `#6B7280` sobre `CLR_CARD_BG` `#1B1E24` (GUI actual, usado en texto) | 3,45:1 | **No** (1.4.3) |
| Borde `#3A4150` sobre `#16191E` / `#1B1E24` | 1,72:1 / 1,63:1 | **No** (1.4.11) |
| Borde `#6E7787` sobre `#16191E` / `#1B1E24` / `#20242C` | 3,90:1 / 3,70:1 / 3,45:1 | Sí |

## Lo que explican los botones ⓘ

Qué documenta Microsoft de cada ajuste está en [`cpu-energia-y-procesos.md`](cpu-energia-y-procesos.md) (temporizador, MMCSS, `Win32PrioritySeparation`, `DisablePagingExecutive`) y en [`graficas-y-perfil-del-equipo.md`](graficas-y-perfil-del-equipo.md) (`OverlayTestMode`). La regla es que un ⓘ solo afirma lo que dice la fuente, y si no hay fuente lo dice.
