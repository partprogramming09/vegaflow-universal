# Fase 0: preparar el laptop

**Lector:** personas. Parte del [plan maestro](README.md); método común y orden de las fases en el README.

**Por qué.** El repo de GitHub se rehízo con el historial limpio. La copia del laptop tiene el historial viejo, así que no se puede actualizar con `pull` y no hay que hacer `push` desde ella. Sin el laptop no se puede probar ninguna fase.

**Qué se hace:**
1. Clonar `vegaflow-universal` de nuevo en el laptop.
2. Crear `scripts/remote/.env` en el Mac a partir de `.env.example`. Desde el arreglo de seguridad, los scripts remotos ya no traen el usuario ni la IP escritos en el código.
3. Compilar y correr la suite completa.
4. Pendientes de limpieza que no son fases: decidir de dónde salen los DLL de DXVK-GPLAsync ahora que no están en el repo (antes estaba previsto un `scripts/fetch_dxvk.ps1` con versión y sha256 fijos; la condición era encontrar una descarga estable de esa variante parcheada), y borrar `scripts/remote/archive/` (el `vfjob` retirado) si nadie lo ha necesitado.

**Terminado cuando:** la suite C++ y todas las pruebas de Rust pasan en el laptop con el código actual. Ese resultado es la línea base.
