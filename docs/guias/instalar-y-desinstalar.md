# Instalar y desinstalar VegaFlow

**Lector:** quien instala VegaFlow en un equipo Windows y quien comprueba el paquete antes de entregarlo.

**Lo esencial:** el paquete es un `.zip` con el daemon y dos scripts de PowerShell. `install.ps1` comprueba que ningún archivo esté alterado, copia el daemon a una carpeta que solo los administradores pueden escribir y, si ya había una instalación, primero devuelve sus cambios. `uninstall.ps1` devuelve los cambios al sistema **antes** de borrar nada. Los dos admiten `-DryRun`, que muestra los pasos sin cambiar nada.

**Ejemplo.** Descargas `VegaFlow-0.1.0-win-x64.zip`, lo descomprimes, abres PowerShell como administrador y ejecutas `install.ps1`. VegaFlow queda en `C:\Program Files\VegaFlow`, aparece en «Agregar o quitar programas» y en el menú Inicio, y no arranca solo con Windows (eso se pide con `-Autostart`). Si luego lo quitas con `uninstall.ps1`, el plan de energía y los ajustes de Windows que VegaFlow hubiera cambiado vuelven a su valor original.

## Requisitos

- Windows 10 u 11 de 64 bits y PowerShell.
- Una consola de PowerShell **como administrador**: los dos scripts lo exigen y fallan si no.
- El paquete **no está firmado**: Windows puede mostrar un aviso al abrirlo. Por eso conviene verificar el hash (paso 1). La firma y otros formatos de instalador están en [`plan/maestro/fuera-del-plan.md`](../plan/maestro/fuera-del-plan.md).

Si PowerShell bloquea los scripts por su política de ejecución, lánzalos así: `powershell -ExecutionPolicy Bypass -File .\install.ps1`.

## Paso 1 de 5: verificar el zip

Junto al zip va su `.sha256`, con el formato de `shasum`: el hash, dos espacios y el nombre del archivo.

```powershell
$esperado = (Get-Content .\VegaFlow-0.1.0-win-x64.zip.sha256).Split(' ')[0]
(Get-FileHash .\VegaFlow-0.1.0-win-x64.zip -Algorithm SHA256).Hash -ieq $esperado
```

Deberías ver `True`. Si ves `False`, no lo instales: descarga el zip otra vez.

Este hash solo prueba que el zip es el que corresponde a ese `.sha256`. Sirve contra una descarga dañada o cambiada si el `.sha256` te llega por un camino distinto al del zip. Dónde se publicará cada uno está **pendiente de decidir**.

## Paso 2 de 5: descomprimir

El zip contiene una carpeta `VegaFlow-0.1.0-win-x64` con:

| Archivo | Para qué sirve |
| --- | --- |
| `vegaflow_daemon.exe` | El programa |
| `install.ps1`, `uninstall.ps1` | Instalar y desinstalar |
| `config/config.json` | La configuración de fábrica |
| `VERSION` | La versión (0.1.0) |
| `SHA256SUMS` | El hash de cada archivo; `install.ps1` lo comprueba |
| `LICENSE`, `CHANGELOG.md`, `README.md`, `README.en.md` | Licencia, historial de versiones y documentación |

## Paso 3 de 5: ver qué haría, sin instalar (`-DryRun`)

```powershell
.\install.ps1 -DryRun
```

Deberías ver líneas con el prefijo `[dry-run]`: primero `Paquete 0.1.0 verificado (SHA-256 de 9 archivos).` y después cada paso. No se crea ninguna carpeta ni se toca Windows. Aun así verifica los hashes: si el paquete está alterado, falla aquí.

## Paso 4 de 5: instalar

```powershell
.\install.ps1
```

| Parámetro | Qué hace |
| --- | --- |
| `-InstallDir <ruta>` | Carpeta de destino. Por defecto, `C:\Program Files\VegaFlow` |
| `-Autostart` | Crea la tarea `VegaFlow`, que arranca el daemon al iniciar sesión (nivel más alto, solo el daemon). **Desactivada de fábrica** |
| `-NoShell` | No registra «Agregar o quitar programas», ni el acceso del menú Inicio, ni la tarea |
| `-DryRun` | Muestra los pasos sin cambiar nada |

Qué hace, en orden:

| Paso | Qué ocurre | Si falla |
| --- | --- | --- |
| 1. Paquete | Comprueba el SHA-256 de cada archivo contra `SHA256SUMS` | No copia nada |
| 2. Carpeta | Pide una ruta absoluta. Rechaza la raíz de una unidad, `SystemRoot`, `ProgramFiles` y `ProgramData` a secas, y cualquier carpeta que exista, no esté vacía y no sea una instalación de VegaFlow (una carpeta que solo tiene `config` y `logs`, por ejemplo tras `-KeepConfig`, sí vale) | No toca la carpeta |
| 3. Instalación previa | Si ya había una, para sus procesos y ejecuta `vegaflow_daemon.exe --restore-registry` para devolver sus cambios | Si ese comando falla, no reemplaza nada |
| 4. Archivos | Copia el daemon, `uninstall.ps1`, `VERSION` y `config\config.json`; fija los permisos y escribe `install.json`, la lista de lo instalado; ejecuta `--version` y comprueba que coincide con el paquete | Error con el motivo |
| 5. Windows | Registra «Agregar o quitar programas» y el acceso del menú Inicio (abre `--gui`); con `-Autostart`, la tarea | — |

Sobre los permisos: se fijan por identificador de seguridad (SID) para que funcionen en un Windows en cualquier idioma. SYSTEM y Administradores tienen control total, Users solo lectura, y el propietario es Administradores. Un `config.json` que ya existía **se conserva solo si su dueño es SYSTEM o Administradores**: uno plantado por otra cuenta no se hereda, porque el daemon aplica la misma regla al leerlo (ver [`arquitectura/seguridad.md`](../arquitectura/seguridad.md), punto 5).

La entrada de «Agregar o quitar programas» tiene nombre fijo: si instalas en dos carpetas, apunta a la última.

## Paso 5 de 5: comprobar

```powershell
& "C:\Program Files\VegaFlow\vegaflow_daemon.exe" --version
```

Deberías ver `VegaFlow Universal 0.1.0`.

## Desinstalar

Desde «Agregar o quitar programas», o con el script que se copió a la carpeta de instalación:

```powershell
& "C:\Program Files\VegaFlow\uninstall.ps1"
```

| Parámetro | Qué hace |
| --- | --- |
| `-InstallDir <ruta>` | Carpeta a desinstalar. Por defecto, la del propio script |
| `-KeepConfig` | Conserva la carpeta `config\` (`config.json` y lo que el daemon guarda allí) |
| `-DryRun` | Muestra los pasos sin cambiar nada |

Qué devuelve y qué borra, en orden:

1. **Solo actúa en una carpeta de VegaFlow:** tiene que contener `vegaflow_daemon.exe` e `install.json`. Si no, no borra nada.
2. **Para el daemon** de esa carpeta.
3. **Devuelve los cambios al sistema** con `--restore-registry` (lo que está en el diario de cambios y en las copias de `SOFTWARE\VegaFlow\RegBackup`). **Si falla, no borra nada** y dice por qué, para no perder la copia de los valores originales; revisa la salida y vuelve a ejecutarlo.
4. **Quita solo lo que apunta a esa carpeta:** la entrada de «Agregar o quitar programas», el acceso del menú Inicio y la tarea `VegaFlow`.
5. **Borra** los archivos que lista `install.json`, `logs\` y `config\` (salvo con `-KeepConfig`). `install.json` y `uninstall.ps1` van al final, para poder repetir si algo se interrumpe. Si la carpeta queda vacía, la borra; si queda algo, lo lista.

Para reinstalar encima de una carpeta que dejó `-KeepConfig`, ejecuta `install.ps1 -InstallDir` con esa misma ruta: conserva el `config.json` si sigue siendo de un administrador.

## Errores frecuentes

| Mensaje (resumido) | Causa y qué hacer |
| --- | --- |
| `... no coincide con SHA256SUMS: el paquete está dañado o fue modificado. No se instaló nada.` | Un archivo cambió. Descarga el zip otra vez y verifícalo (paso 1) |
| `Falta SHA256SUMS` o `Falta <archivo>` | El paquete está incompleto: descomprime el zip entero |
| `InstallDir no puede ser ...` | Usa una subcarpeta propia, no la raíz ni `Program Files` a secas |
| `... existe, no está vacía y no es una instalación de VegaFlow. No se toca.` | Elige otra carpeta |
| `--restore-registry ... terminó con código N` (al instalar o desinstalar) | No pudo devolver los cambios anteriores y se detuvo antes de borrar o reemplazar nada. Lee la salida, corrige y vuelve a ejecutar |
| `... no parece una instalación de VegaFlow` | La carpeta no tiene `vegaflow_daemon.exe` o `install.json`: revisa `-InstallDir` |
| Error de `#requires -RunAsAdministrator` | Abre PowerShell como administrador |

## Probar el paquete de extremo a extremo

`tests/package/package_tests.ps1` instala, aplica un cambio real, desinstala y compara el sistema con el de antes. Solo corre en un Windows real, como administrador, y cambia por unos segundos `OverlayTestMode` (con `--mpo-disable`), así que no se ejecuta con un juego abierto.

```powershell
# Carpeta temporal: instala en C:\ProgramData\vf-pkgtest-<8 caracteres> y la borra al final
.\tests\package\package_tests.ps1 -PackageDir .\dist\VegaFlow-0.1.0-win-x64

# Instalación real: prueba también «Agregar o quitar programas» y el acceso de Inicio
.\tests\package\package_tests.ps1 -PackageDir .\dist\VegaFlow-0.1.0-win-x64 -InstallDir 'C:\Program Files\VegaFlow' -Shell
```

`-PackageDir` es la carpeta ya descomprimida, no el zip. Con `-Shell` la carpeta de destino no debe existir y no puede haber otra entrada VegaFlow en «Agregar o quitar programas»: desinstala antes si la hay.

Qué comprueba, en diez grupos: `-DryRun` no cambia nada; un paquete con el `.exe` alterado se rechaza antes de copiar; la instalación deja los archivos, `--version` correcto, propietario Administradores y Users sin escritura; reinstalar conserva el `config.json` del administrador; un cambio real (`--mpo-disable`) pone `OverlayTestMode` en 5 y guarda su copia; desinstalar con `-DryRun` no toca nada; desinstalar con `-KeepConfig` devuelve el valor de antes y guarda la configuración; reinstalar sobre esa carpeta y desinstalar del todo borra la carpeta; carpetas ajenas y `C:\Program Files` a secas se rechazan sin tocar nada; y al final el plan de energía, `OverlayTestMode`, la entrada, el acceso y la tarea quedan como al empezar. Termina con código 0 solo si todo pasó.

Resultado en el laptop de pruebas (2026-11-14): `38 PASSED, 0 FAILED` con la carpeta temporal y `41 PASSED, 0 FAILED` con `C:\Program Files\VegaFlow` y `-Shell`.

## Generar el paquete

Se hace desde un Mac o Linux con `scripts/package.sh`; está en [`compilar-y-probar.md`](compilar-y-probar.md).

## Documentos relacionados

- Qué hace el daemon una vez instalado: [`uso-y-funciones.md`](uso-y-funciones.md).
- Cómo devuelve `--restore-registry` los cambios y qué es el diario: [`../arquitectura/seguridad.md`](../arquitectura/seguridad.md).
- Historial de versiones: [`../../CHANGELOG.md`](../../CHANGELOG.md).
