# COORDINACION.md — Mac y la laptop Windows con WinBridge

Una sola instancia de Claude trabaja en este proyecto y en el MCP `metal-gpu-trace`, y corre en la Mac. La laptop
Windows (`HERNAN`, Windows 11) es un banco de pruebas: Claude la maneja desde la Mac con WinBridge, un MCP por
Tailscale. Ya no hay una segunda instancia en Windows ni coordinación por issues y PR entre dos Claude.

## Repos

| Repo | Rama de trabajo | Dónde se valida |
| --- | --- | --- |
| `partprogramming09/vegaflow-universal` (privado) | una rama por cambio, PR a `main` | Laptop Windows |
| `partprogramming09/metal-gpu-trace` (público) | una rama por cambio, PR a `main` (el soporte de Windows ya está en `main`) | CI en Linux, Windows y macOS; grabaciones reales en la Mac y en la laptop |

## Qué se puede hacer en la laptop

WinBridge (agente en Go, `native-go`) expone 9 herramientas: `Snapshot`, `Click`, `Type`, `Move`, `Scroll`,
`Shortcut`, `FocusWindow`, `Wait` y `GetSystemInfo`. Controlan la pantalla y el teclado; no ejecutan comandos ni copian
archivos. La ejecución de tareas con nombre fijo (`RunTask`) y el informe de solo lectura (`Diagnose`) están en
desarrollo en el repo `winbridge`: hasta que estén desplegadas, correr un comando en la laptop significa escribirlo
en una terminal y leer el resultado en una captura.

Límites que hay que tener presentes:

- Con la pantalla bloqueada, `Click`, `Type`, `Move`, `Scroll`, `Shortcut` y `FocusWindow` devuelven error; solo
  `Snapshot` y `GetSystemInfo` responden. Antes de probar, comprobar que la sesión está desbloqueada.
- El agente Python antiguo escribía `°` en vez de `|`. El agente Go lo corrige, pero un comando con tuberías se
  comprueba en pantalla antes de ejecutarlo.
- Una prueba que dependa de PresentMon, DWM (`ctypes.windll`), procesos o rutas `C:\` solo cuenta como validada si
  corrió en la laptop. El runner `windows-latest` de GitHub no tiene GPU real ni sesión de escritorio.

## Reglas

1. Todo cambio va por una rama y un PR. Nadie hace push directo a `main` ni mergea sin que el usuario lo diga.
2. Claude escribe y revisa el código en la Mac. Lo que solo se puede probar en Windows se corre en la laptop por
   WinBridge, y no se da por probado hasta entonces.
3. Un reporte de una prueba en la laptop incluye la salida real pegada: comando, resultado, versión de Python y de
   Windows. Sin salida pegada no cuenta como verificado.
4. No se ejecuta ni se descarga nada en la laptop que el usuario no haya autorizado, y las claves no pasan por
   argumentos ni por logs.
5. No se reescribe historia (`push --force`, `reset --hard`) en una rama compartida.
6. Lo que debe decidir el usuario va al final del mensaje, en el bloque `❓ NECESITO TU RESPUESTA:`.

## Flujo

1. Claude publica la rama y abre el PR con qué hay que probar y cómo.
2. Claude corre las pruebas que puede en la Mac y en el CI.
3. Para lo que solo corre en Windows, Claude lo prueba en la laptop por WinBridge y pega la salida en el PR.
4. Si falla, se corrige en la misma rama, con un commit nuevo.
5. El usuario revisa y mergea.

## Comandos útiles

```sh
gh pr list --state open
gh pr view N --comments
gh pr checks N
```

Para el MCP: `gh pr list -R partprogramming09/metal-gpu-trace`.
