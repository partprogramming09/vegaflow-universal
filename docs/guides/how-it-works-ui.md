# How ui/ works

**In short:** `src/ui/` is the Windows window you see: the side menu, the screens, the buttons and the tray icon (the small icon by the clock).
It draws what it is given and decides nothing. Each screen has a model, built in `src/session/`, and a view in `src/ui/` that paints that model and sends button presses back to the app.
While it is open, every 1.5 seconds the window asks for fresh numbers, refreshes the screen in front and repaints it. Hidden in the tray with no game running, it stops polling: it wakes only for window-focus, display-adapter and power events, plus a 30-second safety net.
The one thing not to forget: the numbers and the rules are not in this folder. If a figure on screen looks wrong, check the model in `session/` first.

**Reader:** people and agents. **Last review:** 2026-11-20. **Explains:** `src/ui/`. **Answers:** How is a screen drawn, and where does each number come from? What happens on a button press, on a second start, or when the window is hidden?

## Why it exists

VegaFlow is a background program that changes Windows settings. Its window has to show what it found and let the person choose a few things. It must not hold any rule of its own: a rule buried in a drawing function cannot be tested without a real window and a real PC.

So the code is split in two. The layering is described in [code layers](./modules/layers.md) and in [ADR 0001](../adr/0001-nucleo-que-decide-y-puertos.md).

- `session/<name>_screen.cpp` builds a **model**: plain data, such as "53 % in use" and the sentences of the screen. It has no window code.
- `ui/<name>_view.cpp` **paints** that model and creates its buttons.

The window is a plain Win32 window. Win32 is Windows' own API for windows and buttons; GDI (Graphics Device Interface) is its basic drawing layer. No UI framework is used: the project rules in `CLAUDE.md` forbid Qt, wxWidgets and Boost, and keep the idle daemon under 15 MB.

## A concrete example

The Home screen on the test laptop (AMD Ryzen 5 4600H, 15788 MB of RAM), opened with `--gui --observe` on 2026-11-20, shows "53 % en uso" (53 % in use) and "7454 MB disponibles de 15788 MB" (7454 MB available out of 15788 MB). See the [screenshot](./img/gui-home.png).

Where the 53 % comes from, with the screenshot numbers:

- Used memory = 15788 − 7454 = 8334 MB (`BuildHomeScreen` in `src/session/home_screen.cpp`).
- Percent = (8334 × 100 + 15788 / 2) / 15788, using whole numbers = 841294 / 15788 = 53 (`BuildHomeScreen` in `src/session/home_screen.cpp`).
- The 7454 MB is free memory plus standby memory (the cache Windows gives back on request): `in.ram_available_mb = d.free_ram_mb + d.standby_ram_mb` (`Model` in `src/ui/home_view.cpp`).
- The words "Modo seguro" (safe mode) appear because the window was opened with `--observe`. The flag is set in `ParseCommandLine` in `src/app/cli.cpp`, turned into safe mode in `main` in `src/app/main.cpp`, and `RunGuiMode` in `src/app/gui_mode.cpp` sets `automatic = !safe_mode`. The view copies it (`Model` in `src/ui/home_view.cpp`), and `BuildHomeScreen` in `src/session/home_screen.cpp` writes "En espera" (waiting) and "Modo seguro: VegaFlow no detecta juegos" (safe mode: VegaFlow does not detect games).

Second example, tab 3 ([screenshot](./img/gui-uma-cores.png), taken before F4.2: it still shows the old "DPC / ISR" label): the 12 threads are labelled by role. Since F4.2 the role comes only from the real CPU set id, and a thread the probe left alone says "WINDOWS" (the same word as the "Tu equipo" screen), the game's threads say "JUEGO" and the isolated ones "FONDO". Below the map: the count of threads per role, the thread the keyboard is on, and "Procesos en segundo plano detectados" (background processes found). Tab 3 now has a `session/` model (`BuildMemoryScreen`) and native controls.

## How it works, step by step

![How the ui layer works, step by step](./img/how-it-works-ui.png)

| Step | Function | What to look at | Why |
|---|---|---|---|
| 1. Create the window | [`RunGuiLoop`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp) | Size 1120 × 740, cut down to the work area when the screen is smaller; then each view's `Refresh` and `Show` run | The window starts with the Home screen in front and the other screens built but hidden |
| 2. Start the tray and the timer | [`DashboardWndProc`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp) (`WM_CREATE`, `WM_SHOWWINDOW`), [`PlanGuiCadence`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/gui_cadence.cpp) | `WM_CREATE` adds the tray icon, connects the event source (`on_AttachWake`) and asks for the first telemetry; the timers start when the window is shown, at the interval `PlanGuiCadence` gives for what the window is doing | Visible: 1500 ms tick and the 50 ms game-pad tick. Hidden with no game: a 30 s safety net, no pad tick. Hidden with a game, or with the hooks unavailable: 1500 ms |
| 3. Each tick: poll and refresh | [`DashboardWndProc`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp) (`WM_TIMER`), [`RunGuiMode`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/gui_mode.cpp) | `on_PollTelemetry` returns a `GuiDashboardData`; nothing is repainted while the window is hidden or minimized. A hidden window also polls on a wake event (`WM_APP_WAKE`, a burst merged into one poll after 250 ms) and the moment it is shown again, so restoring it never shows old numbers | The app layer does the Windows work; the window only copies the result in |
| 4. Build the model | [`Model`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/home_view.cpp), [`BuildHomeScreen`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/home_screen.cpp) | `Model(st)` copies the telemetry into `HomeInputs`; `BuildHomeScreen` returns the texts and numbers | One function holds the rules; the view only reads its result |
| 5. Paint the screen | [`HomeViewPaint`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/home_view.cpp) | Cards are drawn with GDI; "% en uso" is printed from `memory_percent`, or "Sin dato" (no data) when it is unknown | The bar shows the same number as the text, so colour is never the only cue |
| 6. A press: button or click | [`DashboardWndProc`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp), [`HomeViewCommand`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/home_view.cpp) | Native buttons send `WM_COMMAND` to their view; GDI boxes are found with a point-in-rectangle test; the action is a callback into the app | The window does not do the action; it asks the app and shows the sentence that comes back |
| 7. Change tab | [`RefreshActiveScreen`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp), [`HandleActionClick`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp) | `current_tab` changes, the view of the old tab is hidden, the new one is shown and refreshed | Only one screen has native controls in front, so Tab never lands on a hidden button |
| 8. Second start | [`main`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/main.cpp), [`DashboardWndProc`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp) | The second program finds the window by its class and posts the registered restore message; the window shows and focuses itself | A normal click cannot cross the privilege filter (see below), so only this one message is let through |

The step numbers match the diagram. Steps 1 and 2 run once. Steps 3 to 5 repeat every tick. Steps 6 to 8 run when the person acts.

## The pieces inside

| File | Responsibility |
|---|---|
| [gui_window.hpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.hpp) | The public contract: `GuiDashboardData` (the telemetry in), `GuiCallbacks` (the actions out) and `GuiWindow::RunGuiLoop`. The only way the window talks to the app |
| [gui_window.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp) | The window class, the message loop, the timers (their cadence comes from `session/gui_cadence`), the tray and its menu (restore, purge, copy report, exit), the tab dispatch (`PaintDashboard`) and the keyboard shortcuts (Ctrl+1 to Ctrl+8, Enter on a focused button, the arrows of the side menu) |
| [gui_components.hpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_components.hpp) | Ids of the controls, the timer and tray constants, the colour palette, and `GuiWindowState`, which keeps each screen's model and its native controls |
| [gui_components.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_components.cpp) | The GDI drawing primitives: cards, text, badges, bars, buttons, tab buttons, switches |
| [native_controls.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/native_controls.cpp) | Native (owner-drawn) buttons: `MakeOwnerButton` creates a `BUTTON` with `WS_TABSTOP`, `PlaceClipped` hides the scrolled part without removing the control, `ScrollDeltaToReveal` scrolls a focused control into view, and `DrawFocusRing` draws the focus ring |
| [memory_view.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/memory_view.cpp) | Tab 3, "3. Memoria UMA & Núcleos CPU" (F4.2): the memory card is GDI; the three buttons are native; the thread map is ONE native control (one Tab stop) subclassed to take the arrow keys, Home and End, and to select a cell with the mouse. It draws every cell itself, with the role written in words and the focus ring on the selected cell |
| [sidebar_view.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/sidebar_view.cpp) | The side menu (F4.2): eight native buttons plus "Minimizar a Bandeja", created first so Tab starts there. Up, Down, Home and End move between entries, Enter and Space open the focused one |
| `<name>_view.cpp` and `.hpp` (home, settings, memory, games, history, config, programs, equipment, requirements) | One view per screen. Each has `Refresh` (creates the native buttons once), `Show`, `Paint`, `DrawItem` (draws a button), `Command` (reacts to it) and, for long screens, `Scroll`. Home is the example: [home_view.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/home_view.cpp) |
| [texts.hpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/texts.hpp) | The Spanish texts of the newer screens, in one file "so they can be translated without touching the drawing code" (quoted from its header comment) |
| [app_identity.hpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/app_identity.hpp) | The version (`kVersion`), the window class (`kWindowClass`), the title, the tray command id (`kTrayRestoreCommand`), the restore message (`kRestoreWindowMessage`) and `FormatHardwareLine`, which builds "CPU | GPU" and cuts a line longer than 65 bytes |
| [session/home_screen.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/session/home_screen.cpp) and the other `session/*_screen.cpp` | The models. They take plain inputs and return plain data. The list of screens is just below |

**Which screen is which** (menu index; what each one is for, from the person's side, is in the [usage guide](./usage-and-features/system-info-and-gui.md)): 0 Home (`home_view`), 1 System settings (`settings_view`), 2 UMA memory & CPU cores (`memory_view`, model in `session/memory_screen`), 3 Games (`games_view`), 4 History (`history_view`), 5 Settings (`config_view`), 6 Programs (`programs_view`), 7 Your machine (`equipment_view`) and 8 Requirements (`requirements_view`, opened from Settings, with no menu entry of its own). These screens arrive with phase G, planned for version 0.2.0 (not released yet: the binary still reports 0.1.0, see `--version` below). The colours of all of them are constants in [gui_components.hpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_components.hpp) (`CLR_BG_MAIN` and the lines after it).

How they talk: the window holds one `GuiWindowState` (in [gui_components.hpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_components.hpp)) with the telemetry, the models and the native control handles. A view reads its model from that state, paints it, and calls the app only through `GuiCallbacks`. The app's side is wired in [gui_mode.cpp](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/app/gui_mode.cpp) (`callbacks.on_PollTelemetry` for the telemetry, `callbacks.on_PurgeNow` for the purge, both in `RunGuiMode`). Those few actions stay in `app/` on purpose: the telemetry poll returns a `ui/` structure, which `session/` may not include, and purge, isolate, restore affinity and the history poll call Windows classes that have no fake, so moving them would not make them testable.

## Why it was built this way

- **Win32 and GDI, not a framework.** The project rules allow no heavy frameworks, and the daemon must stay under 15 MB. The price: every control and every drawing is written by hand.
- **Owner-drawn native buttons, not only GDI boxes.** A native `BUTTON` has a focus, a place in the Tab order and an accessible name; a GDI box has none. So buttons, switches and fields are native (`MakeOwnerButton` in `src/ui/native_controls.cpp`), and cards and bars stay GDI. The alternative, all GDI, was what tab 3 and the side menu did until F4.2, and Tab did not reach them.
- **The views decide nothing.** The model is built in `session/` so that a test can check "in safe mode the waiting card promises nothing" without opening a window. ADR 0001 (accepted 2026-11-14) sets the same split for the whole program: the part that decides is apart from the part that acts on Windows.
- **Restore message instead of an ordinary command.** The window lets through only `RegisterWindowMessage("VegaFlowUniversal.RestoreWindow")` (`ChangeWindowMessageFilterEx`, in `DashboardWndProc`'s `WM_CREATE`). An ordinary click from a lower-rights program is refused by Windows' privilege filter (UIPI), and `kRestoreWindowMessage` in `app_identity.hpp` says so. The alternative, a plain `WM_COMMAND`, would let a less privileged program press the buttons of the administrator window.
- **Fixed pixels, Windows does the scaling.** The window declares itself DPI-unaware with GDI scaling (`RunGuiLoop`). The alternative, per-monitor awareness, "left it tiny at 125 % and 150 %", according to the comment there.
- **The side menu is native since F4.2, and Ctrl+1 to Ctrl+8 stays.** Tab, Shift+Tab and the arrows reach the entries; the shortcut remains as a fast jump from anywhere. The price: native children are not repainted when the parent is invalidated, so `SidebarViewSync` invalidates each button on a tab change, and the hover highlight of the old GDI menu is gone (the pressed and focus states remain).
- **Enter is handled by the loop, not by `IsDialogMessage`.** The main window is not a dialog, so `IsDialogMessage` would answer Enter with `IDOK`. `RunGuiLoop` sends `BM_CLICK` to the focused sidebar or Memoria button instead, and only to those (not to the fields of Configuración).
- **The thread map is one control, not one per thread.** With 64 or more threads, one Tab stop per cell would make Tab unusable. The selection logic (`MoveCoreSelection`) and the grid geometry (`ComputeGrid`) are pure and tested in `session/memory_screen` (Test 144), and roles are taken only from the real CPU set id (the old drawing also matched an id to a thread's position, and assumed ids start at 256).

## What happens when it fails

- **No RAM total.** If the telemetry cannot read the memory, the model says "No se pudo leer la memoria del equipo." (could not read the machine's memory) and the card shows "Sin dato" (no data) instead of a percent (`BuildHomeScreen` in `src/session/home_screen.cpp`, `HomeViewPaint` in `src/ui/home_view.cpp`). The card still draws.
- **Window hidden or minimized.** The timer still polls, but skips the repaint and the screen refresh (`DashboardWndProc`, `WM_TIMER`). Nothing is drawn for a window nobody sees.
- **A notice survives the poll.** Each poll replaces the data, so the window keeps the previous status sentence when the new one is empty (`DashboardWndProc`, `WM_TIMER`). A result shown in the status bar is not wiped by the next tick.
- **Second start, no window found.** `main` posts the message only if `FindWindowW` finds the window; otherwise it prints "already running in the background (no window to show)" and exits with 0.
- **Second start, low rights.** The filter lets only the restore message through; the other messages from a lower-rights program are refused by Windows (`ChangeWindowMessageFilterEx`, in `DashboardWndProc`).
- **A failed action.** Each action returns a sentence, and the window puts it in the status bar (`GuiCallbacks` in `gui_window.hpp`, for example the tray purge, the `ID_TRAY_PURGE` case of `DashboardWndProc`). The person sees why nothing changed.

Known differences, checked in the code and not fixed by this page:

- `gui-home.png` was taken before the 2026-11-20 fix, so its sidebar still says "Auto-Suspensión: ACTIVA" in safe mode. The code now shows "Modo seguro: sin cambios" there in safe mode (`PaintDashboard`), as seen on the laptop the same day.
- The status bar shows "VegaFlow Universal | Win32 GDI", a technical term, in every screen.
- The two screenshots show different free-memory numbers: Home 7454 MB (free plus standby), tab 3 "Libre: 7021 MB" with "Standby: 250 MB" (the old `RenderTabMemory`, now `MemoryViewPaint`). Taken at different moments, or read differently: not confirmed.
- Most texts are not in `texts.hpp`. It holds 96 lines of text; `gui_window.cpp` has 30 lines with text (tab names, tray menu, badges), and the old `gui_tabs.cpp` had 21 (moved to `texts.hpp` in F4.2).

## Evidence that it works this way

| Step | Command or action | Real output |
|---|---|---|
| 1 and 5 (Home, safe-mode text) | Opened `--gui --observe` on the laptop, screenshot | [gui-home.png](./img/gui-home.png): "En espera", "Modo seguro: VegaFlow no detecta juegos", "53 % en uso", "7454 MB disponibles de 15788 MB" |
| 4 (safe mode in the model) | Suite, `[PASS]` line | `[PASS] Test 70.8: in safe mode the waiting card says VegaFlow changes nothing by itself and does not promise detection` |
| 4 (RAM share) | Suite, `[PASS]` line | `[PASS] Test 70.5: the RAM share in use is computed from total and free, an unknown total is unknown, and the last purge states both numbers` |
| 5 (hardware line) | Suite, `[PASS]` line | `[PASS] Test 58: the hardware line shows this machine, never the test laptop (normal: PASS, name over 65 chars cut, not replaced: PASS, ...)` |
| 8 (identity) | `vegaflow_daemon.exe --version` | `VegaFlow Universal 0.1.0` |
| 8 (identity) | Suite, `[PASS]` line | `[PASS] Test 59: window class, title and version are VegaFlow's own (names: PASS, version 0.1.0 is MAJOR.MINOR.PATCH: PASS)` |
| 8 (second start, mutex) | Suite, `[PASS]` line | `[PASS] Test 14: Single-Instance Guard, ... (Mutex 1: OK, Mutex 2 ERROR_ALREADY_EXISTS: PASS, ...)` |
| 6 (purge behind the Home button) | Suite, `[PASS]` line; the click itself was not run | `[PASS] Test 8: ... (Relaunch Dry-Run: OK, IsProcessAlive: OK, MPO Set Dry-Run: OK, ForcePurgeNow Dry-Run: OK)` |
| 5 (readable controls) | Suite, `[PASS]` lines | `[PASS] Test 62.4: control borders reach 3:1 on the surfaces controls sit on`; `[PASS] Test 62.5: the focus ring reaches 3:1 on every surface` |
| 3 and 5 (tab 3) | Opened tab 3 on the laptop, screenshot. How the tab was opened is not recorded | [gui-uma-cores.png](./img/gui-uma-cores.png): "Procesos en segundo plano detectados: 22 \| Procesos aislados en núcleos secundarios: 0" |
| 6 (Tab key reaches the buttons) | Keyboard run | not reproduced |
| 8 (restore message brings the window forward) | `scripts/remote/vfdev run gui-second` on the laptop, with one GUI open | `segunda salio=True (codigo 0); GUIs abiertas=1; ventana al frente es la primera=True` |
| 2 and 3 (cadence) | Suite, `[PASS]` lines | `Test 143.2: hidden without a game -> 30 s safety net, no pad tick (30000 ms)` and `Test 143.5: game closed while hidden -> cadence falls to the safety net`; the window itself was driven on the laptop (see `docs/measurements/verification-status.md`) |
| Whole suite | Full C++ suite | `TEST RESULTS: 435 PASSED, 0 FAILED` |

## Check it yourself

On Windows, from the program folder:

```
vegaflow_daemon.exe --gui --observe
```

Compare the Home card "Memoria del equipo" with the formula above: the percent must be (used × 100 + total / 2) / total, using the numbers the card prints.

## Related

- [How session/ works](./how-it-works-session.md): where the models and the rules come from.
- [How app/ works](./how-it-works-app.md): the entry point that starts the window and wires the callbacks.
- [Code layers](./modules/layers.md): what `ui/` may include.
- [Features: system info and GUI](./usage-and-features/system-info-and-gui.md): what each screen is for, from the person's side.
- [ADR 0001](../adr/0001-nucleo-que-decide-y-puertos.md): why decisions and actions are separate.
