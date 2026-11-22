# System information and the window

**In short:** VegaFlow can read the hardware of your PC and print it as a report or as JSON, a text format other programs can read.
It also has a native Windows window with one screen per topic: home, games, settings, history and programs.
For example, the Games screen lists each Steam game in its own row, with a button «Aplicar perfil» (Apply profile) that applies VegaFlow's settings to that game.

**Reader:** people

### 2.7. SystemInfo Subsystem (Hardware Telemetry & JSON Export)

The [`SystemInfo`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/system_info.hpp) subsystem separates deep hardware and operating-system detection into a standalone, high-precision module:

- **Zen 2 CPU detection:** reads the *brand string* with native `CPUID` instructions (*AMD Ryzen 5 4600H with Radeon Graphics*). It works out the topology of 6 physical and 12 logical cores, the 7 nm Renoir architecture, an 8 MB L3 cache and a boost target of 4.0 GHz, with no STAPM cap (`cpu_stapm_cap_mhz = 0`).
- **Graphics and UMA memory topology (Vega 6):** compares the BIOS's fictitious report (512 MB) with the system's real shared UMA memory (~7.7 GB). It also polls dynamic VRAM budgets in real time through `IDXGIAdapter3::QueryVideoMemoryInfo`.
- **RAM topology from firmware (SMBIOS Type 17):** reads the SMBIOS `RSMB` tables (`GetSystemFirmwareTable`), finds 2 populated DIMM modules and checks the 128-bit bus in **Dual-Channel** (~42 GB/s of bandwidth). It shows a breakdown of real free memory, the Standby List and modified pages.
- **NT kernel:** reports the exact Windows 11 build (Build 26300 with UBR), whether the MMCSS gaming profile is applied, and the memory compression state.
- **Standard JSON export:** supports the `--system-info` command, which prints the full hardware and system profile as standard JSON for other tools to read.

### 2.8. Visual Redesign of the GUI (Clean Slate / Nordic Dark Mode)

The Win32/GDI graphical interface ([`gui_window.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp)) has been fully redone following ergonomic and modern instrument-panel design standards:

- **Neutral Slate/Zinc palette:** the garish gradients were replaced with elegant charcoal tones (`#111317` main background, `#16191E` sidebar, `#1B1E24` cards, thin 1px `#2B303C` borders) and sober, moderate accent colours (steel blue `#3B82F6`, emerald green `#10B981`, amber `#F59E0B`, critical red `#EF4444`).
- **Geometry and spacing:** the spacing is set per screen. On the «Inicio» (Home) screen the outer margins are 20px and the gaps between cards are 16px; on «Tu equipo» (Your PC) the gaps are 8px. Horizontal 1px dividers are part of the design.
- **Educational technical subtitles:** each telemetry card and interactive control has short explanatory subtitles that teach the user about the real physical effect on the chip (easing L3 contention in the CCX, eliminating blurry textures in UMA, DPC interrupt latency in the NT kernel).
- **Screens (version 0.2.0):** the side menu has eight entries, and a ninth screen hangs off «Configuración» (Settings). All of them except number 3 are new screens from phase G. They use native Windows buttons and fields (you move through them with Tab), the program's text is plain Spanish, and nothing is promised as an improvement. Each screen reads a "model" (the data already prepared) from the `session/` layer (see [modules](../modules/)) and decides nothing itself.

| Menu entry | What it shows |
|---|---|
| 1. «Inicio» (Home) | Is a game open? Your PC in three lines, the shared graphics memory and the background programs. Three buttons: «Ver todo el detalle» (See full details, opens «Tu equipo» (Your PC)), «Liberar memoria ahora» (Free memory now) and «Elegir qué se frena» (Choose what gets slowed down, opens Programs). At the end, «¿Algo va mal?» (Something wrong?): «Devolver todo a como estaba» (Restore everything as it was) and «Copiar informe» (Copy report, plain text with the version, the machine, the memory and the last 30 changes; the Windows user name is replaced with `<usuario>` in every path) |
| 2. «Ajustes del sistema» (System settings) | Each Windows setting VegaFlow can change, with its real state (enabled or factory default), whether it is measured or «Sin medir» (Not measured), an ⓘ with the exact change, and a separate warning for the experimental features. Only the families that apply to your graphics card are shown |
| 3. «Memoria UMA & Núcleos CPU» (UMA memory & CPU cores) | Still the old drawing (GDI): it has not been redone |
| 4. «Juegos» (Games) | One row per Steam game. «Ver cambios» (See changes) shows the file, the copy and the lines the profile writes. Buttons: «Aplicar perfil», «Volver al archivo original» (Restore the original file), «Me fue mal en este juego» (It went badly in this game; excludes the game), «Volver a incluir» (Include again), «Quitar DXVK» (Remove DXVK; shown only for a game where an earlier version installed it), and «Buscar juegos de nuevo» (Search for games again) |
| 5. «Historial» (History) | Each VegaFlow change with the value before and the value after, «Deshacer» (Undo) on each row, and «Devolver todo a como estaba» |
| 6. «Configuración» | The values in `config.json`: the usage mode, how often the foreground window is checked, the shared-memory thresholds, and «Limitar el juego a sus núcleos» (Limit the game to its cores, marked «No recomendado» (Not recommended)). Each change is saved immediately. «Restablecer esta configuración» (Reset this configuration) and «Devolver todo» (Restore everything) are here. A «Ver» (See) button opens the requirements screen |
| 7. «Programas» (Programs) | What VegaFlow does with each kind of program while you play («Frenar» (Slow down), «Solo ahorro» (Savings only) or «No tocar» (Leave alone)), per-program exceptions, and games added by hand |
| 8. «Tu equipo» | Read only: the thread map (game, background, Windows), the charts and the memory-protection level |
| «Requisitos y advertencias» (Requirements and warnings) (no entry of its own) | Read only. What VegaFlow needs, whether your PC meets it, and what is good to know. You reach it from «Configuración», and a button returns there |

**Keyboard:** Tab and Shift+Tab move between the controls, the space bar presses the focused button (Enter does not), Ctrl+1 to Ctrl+8 jump to the menu entry with that number, and on the Home, Your PC and Requirements screens, Page Up, Page Down, Home and End scroll the screen.

**Window extras (E2.7).** Their logic is in [`session/gui_flow.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/gui_flow.cpp) and is tested without a window (Test 115); the window only forwards its messages to it. The dialogs are the window's own ([`ui/choice_dialog.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/choice_dialog.cpp)), never a `MessageBox`.

| Extra | What it does |
|---|---|
| Exit notice | Closing the window (X, or «Cerrar VegaFlow Universal» in the tray menu) with changes still in force asks: «Devolver todo a como estaba» (runs the same restore as the Home button, then closes), «Dejarlos» (closes; the journal is untouched) or «Cancelar». With no change in force it closes without asking. A second close while the dialog is open is ignored. Windows shutting down never asks. |
| Panic shortcut | `Ctrl+Alt+Shift+R`, registered with `RegisterHotKey` and `MOD_NOREPEAT`; it does what «Devolver todo a como estaba» does. One restore runs at a time, and a press queued behind it is dropped. If another program owns the shortcut, «Configuración» says so under its subtitle and nothing else changes. |
| Welcome | A dialog the first time the window opens, until the file `welcome.seen` (next to `config.json`) exists. If the file cannot be written, it shows again next time. |
| Game pad | `xinput1_4.dll` is loaded at run time (a PC without it runs the same window). A 50 ms timer, only while the window is in front and no dialog is open, reads pad 0: D-pad moves the focus, A presses the focused button, bumpers change the menu entry. |
| Restart notice | A «Configuración» number that the daemon reads only at start shows «Se aplica al reiniciar VegaFlow» after its name: `poll_interval_ms` and the four `memory.*` numbers. The source is `applies_live` in `rust/vegaflow-core/src/policy/config_rules.rs`. |
| (i) preview | The (i) of each row in «Ajustes del sistema» adds «what would happen on your PC»: the state now, the exact write and whether it can be undone. Nothing is changed to build it. |
| Pause offer | On «Inicio», while a known game is open and VegaFlow acts by itself, «Pausar VegaFlow en este juego» excludes that game (same as «Me fue mal en este juego»). |
| Diagnosis | «Diagnóstico» on «Inicio» opens what `--doctor` prints, read with the rights of the running account (without administrator it shows what could be read). «Copiar informe» copies it and leaves the dialog open. |

The old «Dashboard Central» (Central dashboard) screen, with cards for EcoQoS, I/O priority and DirectFlip, no longer exists. Neither does the «Aplicar Preset APU» (Apply APU preset) button. It is now «Aplicar perfil», on the Games screen.
