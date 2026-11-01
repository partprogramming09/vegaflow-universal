# COORDINACION.md — Mac y Windows trabajando juntos

Dos instancias de Claude trabajan en este proyecto y en el MCP `metal-gpu-trace`. Una corre en la Mac y otra en la laptop Windows. Comparten la misma cuenta de GitHub, así que **no hay otra forma de coordinarse que los issues y los PR**. Nadie trabaja por su cuenta.

## Repos

| Repo | Rama de trabajo | Quién valida |
| --- | --- | --- |
| `partprogramming09/vegaflow-universal` (privado) | `main` | Windows |
| `partprogramming09/metal-gpu-trace` (público) | `windows-support`; `main` no se toca | Windows |

## Identidad

- Mac: todo título, comentario y commit empieza con `[mac]`.
- Windows: todo título, comentario y commit empieza con `[win]`.
- Cada comentario termina con una línea `— claude/mac` o `— claude/win`.

## Reglas

1. Al empezar cada sesión, antes de tocar código:
   `gh pr list --state open`, `gh issue list --state open` y leer los comentarios nuevos (`gh pr view N --comments`, `gh issue view N --comments`) en los dos repos.
2. Una tarea es un issue. Si no hay issue, créalo antes de empezar. Reclámalo con un comentario: `[tag] tomo esto`. Nadie toma un issue reclamado por el otro.
3. Todo cambio va por una rama y un PR. Nadie hace push directo a `main` ni mergea. Solo el usuario mergea.
4. Mac escribe y revisa código; Windows ejecuta y valida. Lo que solo se puede probar en Windows (DWM, `ctypes.windll`, procesos, rutas `C:\`) lo valida Windows. Mac no lo da por probado.
5. Windows corrige en la misma rama del PR, con un commit `[win] ...`, y lo avisa en un comentario. No abre ramas paralelas para lo mismo.
6. Un reporte de Windows incluye la salida real pegada: comando, resultado, versión de Python y de Windows. Sin salida pegada no cuenta como verificado.
7. Si una instrucción de uno contradice el código o la del otro, no se improvisa: se comenta en el issue o PR y se espera respuesta, o se le pregunta al usuario.
8. No se borra, renombra ni se reescribe historia (`push --force`, `reset --hard`) en una rama que el otro usa.
9. Lo que debe decidir el usuario va al final del mensaje, en el bloque `❓ NECESITO TU RESPUESTA:`.

## Flujo

1. Mac publica la rama y abre el PR o el issue con qué hay que probar y cómo.
2. Windows hace `git pull` o `git fetch` de la rama, corre las pruebas y comenta el resultado con la salida pegada.
3. Si falla, Windows comenta qué falló. Quien tenga la causa más clara corrige, en la misma rama.
4. Cuando Windows confirma que pasa, lo dice en el PR: `[win] validado`. El usuario mergea.
5. Mac y Windows comprueban de nuevo los PR e issues abiertos antes de empezar la tarea siguiente.

## Comandos útiles

```sh
gh pr list --state open
gh issue list --state open
gh pr view N --comments
gh pr comment N --body "[win] ..."
gh issue comment N --body "[mac] ..."
```

Para el MCP: `gh pr list -R partprogramming09/metal-gpu-trace`.
