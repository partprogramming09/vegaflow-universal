# Install and uninstall VegaFlow

**In short:** VegaFlow comes as a `.zip` file with the program and two PowerShell scripts, `install.ps1` and `uninstall.ps1`. The installer first checks every file against a list of SHA-256 hashes (a fingerprint that changes if a file changes), then copies the program into a folder that only administrators can write. Uninstalling gives back the Windows settings that VegaFlow changed *before* it deletes anything. For example, `install.ps1` run as administrator puts VegaFlow in `C:\Program Files\VegaFlow` and does not start it with Windows; if you later run `uninstall.ps1`, the power plan goes back to its original value.

**Reader:** people

## Requirements

- Windows 10 version 1709 (build 16299) or later, or Windows 11, 64-bit, and PowerShell. That is the minimum the "Requirements and warnings" screen checks (`src/session/requirements_screen.cpp:7`, the first build with the EcoQoS call); start-up on 1709 itself is not measured.
- A PowerShell console **run as administrator**. Both scripts require it and fail without it.
- The package is **not signed**, so Windows may show a warning when you open it. That is why it is worth checking the hash (step 1). Signing and other installer formats are outside the current scope.

If PowerShell blocks the scripts because of its execution policy (the setting that decides which scripts may run), start them like this: `powershell -ExecutionPolicy Bypass -File .\install.ps1`.

## Step 1 of 5: verify the zip

Next to the zip is its `.sha256` file, in the format `shasum` writes: the hash, two spaces, and the file name.

```powershell
$esperado = (Get-Content .\VegaFlow-0.1.0-win-x64.zip.sha256).Split(' ')[0]
(Get-FileHash .\VegaFlow-0.1.0-win-x64.zip -Algorithm SHA256).Hash -ieq $esperado
```

You should see `True`. If you see `False`, don't install it: download the zip again.

This hash only proves that the zip matches its `.sha256` file. Both files are published as attachments on the same GitHub release page (checked with `gh release view v0.1.0`; they are uploaded by hand, since there is no release workflow under `.github/workflows/`). So the check catches a damaged download, but it does not protect you if someone changes both files on that page. The zip is not signed; that guarantee would need a *code-signing certificate* (a certificate that proves who published the program).

**Only the executable.** The release also includes a loose `vegaflow_daemon.exe` with its own `.sha256`, the same file that is inside the zip (checked with `gh release view v0.1.0`; it is uploaded by hand, since `scripts/build/package.sh` does not create a separate copy). If you run it from a folder where standard accounts can write (Downloads, the desktop), VegaFlow shows a warning, uses its factory values and saves no configuration. It is a way to try it, not to install it. To keep its configuration, install it with `install.ps1`. See [`security.md`](./security.md), point 5.

## Step 2 of 5: unzip

The zip contains a folder `VegaFlow-0.1.0-win-x64` with:

| File | What it is for |
| --- | --- |
| `vegaflow_daemon.exe` | The program |
| `install.ps1`, `uninstall.ps1` | Install and uninstall |
| `config/config.json` | The factory configuration |
| `VERSION` | The version (0.1.0) |
| `SHA256SUMS` | The hash of each file; `install.ps1` checks them |
| `LICENSE`, `CHANGELOG.md`, `README.md` | License, version history and documentation |

## Step 3 of 5: see what it would do, without installing (`-DryRun`)

```powershell
.\install.ps1 -DryRun
```

You should see lines starting with the prefix `[dry-run]`. The first one is `Paquete 0.1.0 verificado (SHA-256 de 8 archivos).` (the messages the scripts print are in Spanish), followed by each step. No folder is created and Windows is not touched. Even so, the script checks the hashes, so if the package has been altered, it fails here.

## Step 4 of 5: install

```powershell
.\install.ps1
```

| Parameter | What it does |
| --- | --- |
| `-InstallDir <path>` | Destination folder. Default: `C:\Program Files\VegaFlow`. It must be inside Program Files, unless you use `-AllowOutsideProgramFiles` |
| `-Autostart` | Creates the task `VegaFlow`, which starts the daemon when you sign in (highest level, daemon only). **Off by default.** Cannot be combined with `-NoShell` |
| `-NoShell` | Does not register Add/Remove Programs, the Start menu shortcut or the task |
| `-AllowOutsideProgramFiles` | Allows a folder outside Program Files. Only use it for a parent folder that no standard user can write: whoever can rename the folder can put their own program where the shortcut and the task point |
| `-DryRun` | Shows the steps without changing anything |

What it does, in order:

| Step | What happens | If it fails |
| --- | --- | --- |
| 1. Package | Checks the SHA-256 of each file against `SHA256SUMS` | Copies nothing |
| 2. Folder | Asks for an absolute path with a drive letter. It must be inside Program Files unless `-AllowOutsideProgramFiles` is given. It rejects a drive root, `SystemRoot`, `ProgramFiles` and `ProgramData` themselves, and any folder that exists, is not empty and is not a VegaFlow installation. A folder that only holds `config` and `logs` (for example after `-KeepConfig`) is fine. A link named `config` or `logs` is not | Doesn't touch the folder |
| 3. Previous install | If there is one, it stops its processes and runs `vegaflow_daemon.exe --restore-registry` to give back its changes | If that command fails, nothing is replaced |
| 4. Files | Sets the permissions, writes `install.json` (the list of what was installed), creates `config\` and `logs\`, copies the daemon, `uninstall.ps1`, `VERSION` and `config\config.json` (an existing `config.json` is kept if an administrator owns it), checks the hashes of the copies, and runs `--version` to confirm it matches the package | Stops with the reason |
| 5. Windows | Registers Add/Remove Programs and the Start menu shortcut (it opens `--gui`); with `-Autostart`, also the task | — |

Permissions are set by **SID** (a security identifier, a number that Windows uses for each account and group), so they work on Windows in any language. SYSTEM and Administrators get full control, Users get read and execute only, and the owner is Administrators. An existing `config.json` is **kept only if its owner is SYSTEM or Administrators**. A file planted by another account is not inherited, because the daemon applies the same rule when it reads it (see [`security.md`](./security.md), point 5).

The Add/Remove Programs entry has a fixed name. If you install into two folders, it points to the last one.

## Step 5 of 5: check

```powershell
& "C:\Program Files\VegaFlow\vegaflow_daemon.exe" --version
```

You should see `VegaFlow Universal 0.1.0`.

## Uninstall

Use Add/Remove Programs, or run the script that was copied into the install folder:

```powershell
& "C:\Program Files\VegaFlow\uninstall.ps1"
```

| Parameter | What it does |
| --- | --- |
| `-InstallDir <path>` | Folder to uninstall. Default: the script's own folder. Must be an absolute path with a drive letter |
| `-KeepConfig` | Keeps the `config\` folder (`config.json` and whatever the daemon saves there) |
| `-Force` | Deletes even if giving the changes back failed. The registry backups stay in `HKLM\SOFTWARE\VegaFlow\RegBackup` |
| `-DryRun` | Shows the steps without changing anything |

What it gives back and what it deletes, in order:

1. **It acts only on a VegaFlow folder.** The folder must contain `install.json`, the mark that the installer leaves. If it doesn't, nothing is deleted. If `config\` or `logs\` is a link instead of a folder, it stops.
2. **It stops the daemon** in that folder.
3. **It gives the changes back to the system** with `--restore-registry`: the entries in the change journal (the log of changes VegaFlow made) and the copies under `SOFTWARE\VegaFlow\RegBackup`. **If that fails, it deletes nothing** and says why, so the copies of the original values are not lost. Read the output and run it again. With `-Force` it deletes anyway. If `vegaflow_daemon.exe` is missing, it warns and goes on without giving anything back.
4. **It removes only what points to that folder:** the Add/Remove Programs entry, the Start menu shortcut and the `VegaFlow` task.
5. **It deletes** `logs\` and `config\` (unless `-KeepConfig`), then `VERSION` and `vegaflow_daemon.exe`. `install.json` and `uninstall.ps1` go last, so the script can run again if anything was interrupted. The list of files is fixed inside the script; it is not read from `install.json`. If the folder ends up empty, the script removes it. If anything is left, the script lists it.

To reinstall over a folder that `-KeepConfig` left behind, run `install.ps1` from the package with `-InstallDir` set to that same path. It keeps `config.json` if an administrator still owns it.

## Common errors

| Message (summarized) | Cause and what to do |
| --- | --- |
| `... no coincide con SHA256SUMS: el paquete está dañado. No se instaló nada.` | A file changed. Download the zip again and verify it (step 1) |
| `Falta SHA256SUMS` or `Falta <file>` | The package is incomplete: unzip the whole zip |
| `InstallDir no puede ser ...` | Use your own subfolder, not the root or `Program Files` itself |
| `InstallDir debe estar dentro de Program Files ...` | Use a folder inside Program Files. Only if no standard user can write to the parent folder, add `-AllowOutsideProgramFiles` |
| `... existe, no está vacía y no es una instalación de VegaFlow. No se toca.` | Choose another folder |
| `Ya existe una tarea 'VegaFlow' que no es de VegaFlow. No se reemplaza.` | With `-Autostart`, a different task already uses that name. Rename or remove it, then install again |
| `-Autostart no se puede combinar con -NoShell.` | Remove one of the two parameters |
| `--restore-registry ... terminó con código N` (on install or uninstall) | Earlier changes could not be given back, so it stopped before deleting or replacing anything. Read the output, fix the cause and run again |
| `... no parece una instalación de VegaFlow (falta install.json)` | The folder has no `install.json`: check `-InstallDir` |
| `#requires -RunAsAdministrator` error | Open PowerShell as administrator |

## Test the package end to end

`tests/package/package_tests.ps1` installs, applies a real change, uninstalls, and compares the system with its earlier state. It only runs on a real Windows machine, as administrator. For a few seconds it changes `OverlayTestMode` (with `--mpo-disable`), a Windows graphics setting (MPO, multiplane overlay), so don't run it while a game is open.

```powershell
# Temporary folder: installs into C:\ProgramData\vf-pkgtest-<8 characters> and deletes it at the end
.\tests\package\package_tests.ps1 -PackageDir .\dist\VegaFlow-0.1.0-win-x64

# Real installation: also tests the Add/Remove Programs entry and the Start menu shortcut
.\tests\package\package_tests.ps1 -PackageDir .\dist\VegaFlow-0.1.0-win-x64 -InstallDir 'C:\Program Files\VegaFlow' -Shell
```

`-PackageDir` is the folder that is already unzipped, not the zip. The target folder must not exist, with or without `-Shell`. With `-Shell`, there must also be no other VegaFlow entry in Add/Remove Programs: uninstall first if there is one.

What it checks, in groups numbered 1 to 10 (with 8b and 8c) in the script:

- `-DryRun` changes nothing (1).
- A package with an altered `.exe` is rejected before anything is copied (2).
- The install puts the files in place, `--version` is correct, the owner is Administrators, and Users cannot write (3).
- Reinstalling keeps the administrator's `config.json` (4).
- A real change (`--mpo-disable`) sets `OverlayTestMode` to 5 and keeps a copy of the original value (5).
- Uninstalling with `-DryRun` changes nothing (6).
- Uninstalling with `-KeepConfig` restores the earlier value and keeps the configuration (7).
- Reinstalling over that folder and uninstalling fully deletes the folder. A name with `..` in `install.json` cannot delete files outside it. A corrupt journal stops the uninstall and deletes nothing (8).
- Rejected paths and combinations: outside Program Files without `-AllowOutsideProgramFiles`, `-Autostart` with `-NoShell`, and a relative path (8b).
- A link named `logs` stops the uninstall, and the link's target is not touched (8c).
- Folders that hold someone else's files are not touched, and `C:\Program Files` itself is rejected (9).
- At the end, the power plan, `OverlayTestMode`, the Add/Remove entry, the shortcut and the task are as they were at the start (10).

The script exits with code 0 only if every check passed.

The script prints `PACKAGE TESTS: N PASSED, M FAILED`. Last run on the test laptop, 2026-11-20, temporary folder, without `-Shell`: `PACKAGE TESTS: 62 PASSED, 0 FAILED`. The run with `C:\Program Files\VegaFlow` and `-Shell` was last recorded on 2026-11-14 (`41 PASSED, 0 FAILED`, commit `1ef00a8`), before the script gained its newer checks.

## Build the package

It is built on a Mac or Linux with `scripts/build/package.sh`. See [`build-and-test.md`](./build-and-test.md).

## Related documents

- What the daemon does once installed: [`usage-and-features/daemon-and-cli.md`](./usage-and-features/daemon-and-cli.md).
- How `--restore-registry` gives changes back and what the journal is: [`security.md`](./security.md).
- Version history: [`CHANGELOG.md`](https://github.com/partprogramming09/vegaflow-universal/blob/main/CHANGELOG.md).
