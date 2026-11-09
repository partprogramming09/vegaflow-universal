# Fase R3: cierre

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** Con las fases anteriores, cada pieza ya tiene su regla en Rust y su puerto. Falta comprobar que no queda nada de lo viejo y dejar la documentación con el estado nuevo.

**Terminado cuando:**
- ningún caso de uso llama a Windows directamente;
- las pruebas que tocan el registro real están solo en `tests/integration/`;
- el daemon en reposo sigue por debajo de 15 MB;
- `docs/arquitectura/` muestra el diagrama nuevo, `patron-universal.md` describe las reglas de las fases 3 y 7, y el ADR 0001 pasa a "aceptado".
