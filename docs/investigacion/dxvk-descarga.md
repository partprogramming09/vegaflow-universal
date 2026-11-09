# De dónde sacar las DLL de DXVK-GPLAsync

**Lector:** personas. Fecha: 2026-11-09. Pregunta: ¿puede un `scripts/fetch_dxvk.ps1` bajar las DLL con versión y sha256 fijos?

| Dato | Fuente (consultada 2026-11-09) | Confianza |
|---|---|---|
| DXVK oficial v2.5.3 existe (2025-01-13): `https://github.com/doitsujin/dxvk/releases/download/v2.5.3/dxvk-2.5.3.tar.gz`. No publica checksum | API de releases de GitHub del proyecto | Alta |
| Licencia de DXVK oficial: zlib/libpng | `LICENSE` en el tag v2.5.3 | Alta |
| GPLAsync tiene la release **v2.5.3-1** (2025-01-14). Su enlace apunta a la rama `main`, que puede cambiar; el `.zip` es un artefacto de CI que caduca | API de releases de GitLab de `Ph42oN/dxvk-gplasync` | Alta |
| Se puede fijar al commit que subió el archivo: `https://gitlab.com/Ph42oN/dxvk-gplasync/-/raw/b05550505672f65d9348513a691aadb8a07b9e2e/releases/dxvk-gplasync-v2.5.3-1.tar.gz` (mismo blob que en `main` hoy) | API `repository/tree` de GitLab | Alta |
| GPLAsync no publica checksums ni declara licencia (sin LICENSE, `license: null`) | API de proyecto y árbol de GitLab | Alta |
| El daemon depende del modo async: escribe `dxvk.enableAsync = True` (`src/optimize/dxvk_manager.cpp`) | Código del repo | Alta |

**Sin verificar:** la estructura interna de los tarballs (`x64/`, `x32/`) y ningún sha256: hay que descargarlos una vez y calcularlo nosotros.

**Opciones:**
1. Script que baja GPLAsync desde la URL fijada al commit, con un sha256 calculado por nosotros. El hash solo garantiza que el archivo no cambia desde la primera descarga; la falta de licencia impide copiar las DLL al repo o a un paquete (el script no redistribuye: baja desde el origen en la máquina del usuario).
2. DXVK oficial: licencia clara, pero quita el modo async que el daemon configura; sería otro diseño y otra medición.
3. Seguir a mano (`assets/dxvk/README.md`).

Decisión: pendiente del usuario.
