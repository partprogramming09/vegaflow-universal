# DXVK (no incluido / not included)

**Español.** VegaFlow instala DXVK en los juegos que lo permiten, pero **no distribuye sus DLL**: son de terceros y la variante «GPLAsync» no declara ninguna licencia. Colócalas tú aquí con esta forma:

```
assets/dxvk/dxvk-2.5.3/
  x64/  d3d11.dll  dxgi.dll   (y, si quieres, d3d10core.dll d3d9.dll d3d8.dll)
  x32/  d3d11.dll  dxgi.dll
```

- DXVK original (licencia zlib): <https://github.com/doitsujin/dxvk/releases>
- Variante con compilación asíncrona «GPLAsync»: <https://gitlab.com/Ph42oN/dxvk-gplasync> (en esta fecha su página no indica licencia: revísala antes de redistribuirla).

Si faltan, el programa responde `Error: No se encontraron los binarios de DXVK` y no toca el juego. Esta carpeta está en `.gitignore`, así que tus DLL nunca se suben por error.

**English.** VegaFlow installs DXVK into games that allow it but **does not ship its DLLs**: they are third-party files and the "GPLAsync" variant states no license. Put them here yourself, laid out as above.

- Upstream DXVK (zlib license): <https://github.com/doitsujin/dxvk/releases>
- Async-compilation variant "GPLAsync": <https://gitlab.com/Ph42oN/dxvk-gplasync> (its page states no license at the time of writing: check it before redistributing).

If they are missing, the program answers `Error: No se encontraron los binarios de DXVK` and leaves the game untouched. This folder is git-ignored, so your DLLs are never uploaded by mistake.
