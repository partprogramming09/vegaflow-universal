# Modules: GUI and session layer

**In short:** the window is drawn with plain Windows parts, with no big frameworks, so it uses little memory. Each screen is split into three pieces: a view that draws it, a model that decides what it shows, and actions that run its buttons. For example, pressing "Apply profile" on the Games screen goes through the session layer, and a test can check the status message without opening a window.

**Reader:** people

## 3.3. GUI architecture and visual redesign (Clean Slate / Nordic Dark Mode)

Implemented in [`src/ui/gui_window.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.hpp) and [`src/ui/gui_window.cpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/gui_window.cpp):

- **Zero dependencies & ultra-low consumption**: the GUI is built entirely on the plain Win32/GDI API with double buffering (`BitBlt`), with no heavy frameworks (Qt, WPF, Electron). It uses less than **15 MB of private RAM** and **0.0% CPU** at rest. When a game is detected in the foreground, the interface suspends its telemetry graphics calls to give 100% of the cycles to the GPU.
- **Window identity (R3):** [`src/ui/app_identity.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/app_identity.hpp) keeps in one place the version, the class and the title of the window (`VegaFlowUniversalWindow`, "VegaFlow Universal") and the "processor | graphics" line (`FormatHardwareLine`). That line never takes the name of another machine: what could not be read shows as "Procesador no identificado" or "Gráfica no identificada" (the program's Spanish texts, meaning "processor not identified" and "graphics not identified"). A line longer than 65 bytes is cut with "..." without splitting a UTF-8 character. The GUI no longer promises improvements in its texts. The texts of the new screens are in one file, [`src/ui/texts.hpp`](https://github.com/partprogramming09/vegaflow-universal/blob/main/src/ui/texts.hpp).
- **Screens (phase G, version 0.2.0):** the side menu has eight entries, and a ninth screen hangs from Settings. The new screens are Home ("Inicio"), System settings ("Ajustes del sistema"), Games ("Juegos"), History ("Historial"), Settings ("Configuración"), Programs ("Programas"), Your machine ("Tu equipo") and Requirements and warnings ("Requisitos y advertencias"). The last one has no entry of its own: it opens from Settings. **The third, "UMA memory & CPU cores" ("Memoria UMA & Núcleos CPU"), is still in the old drawing (GDI) of `gui_tabs.cpp`.** Each new screen has its `src/ui/<name>_view.*` (it draws the screen and creates the controls) and its model in `src/session/<name>_screen.*` (it decides what is shown). For what each one does, for someone using the program, see the [usage guide](../usage-and-features/), section 2.8.
- **Native controls:** the buttons, switches and fields are Windows `BUTTON` and `EDIT` controls drawn by the program (owner-draw), so they have focus, a Tab order and an accessible name. The cards, bars and labels are still GDI. A button drawn this way is activated with the space bar, not Enter. The shared pieces (creating the control, placing it inside the scrolling area, the frame with the focus ring) are in `src/ui/native_controls.*`.
- **No own DPI scaling:** the window asks Windows for the "not DPI-aware, GDI-scaled" mode (the design uses fixed pixels), and Windows enlarges it with sharp text. The window fits the work area. It was seen at 125% and 150% (see [verification status](../../measurements/verification-status.md)).
- **Slate / neutral Zinc palette ("Clean Slate / Nordic Dark Mode")**:
  - Main background: `#111317` (`RGB(17, 19, 23)`).
  - Sidebar: `#16191E` (`RGB(22, 25, 30)`).
  - Content cards: `#1B1E24` (`RGB(27, 30, 36)`).
  - Subtle borders: `#2B303C` (`RGB(43, 48, 60)`, thin 1px stroke).
  - Panel headers: `#20242C` (`RGB(32, 36, 44)`).
  - Typographic hierarchy: titles `#F3F4F6`, body text `#D1D5DB`, muted text `#9CA3AF`, technical notes `#6B7280`.
  - Sober functional accents: steel blue `#3B82F6` (active), emerald green `#10B981` (optimal), amber `#F59E0B` (warning), soft red `#EF4444` (critical), purple/indigo `#8B5CF6`.
- **Geometry and symmetric spacing**:
  - Perimeter margins of 16px (`padX = 16`, `padY = 16`).
  - Space between cards with symmetric 12px gutters.
  - Internal 1px horizontal dividers (`CLR_CARD_BORDER`).
- **Technical educational subtitles**:
  - Each card and control has short descriptions that explain to the user the physical impact of the optimization (CCX isolation to avoid L3 cache contention; lifting the 512 MB UMA limit on AMD to avoid blurry textures in UE4/UE5; and an explicit note in `OverlayTestMode`/MPO saying that it is not documented by Microsoft and stays off by default).
- **Which screen is which** (menu index; the description of each is in the [usage guide](../usage-and-features/), section 2.8): 0 Home (`home_view`), 1 System settings (`settings_view`), 2 UMA memory & CPU cores (old GDI, `gui_tabs.cpp`), 3 Games (`games_view`), 4 History (`history_view`), 5 Settings (`config_view`), 6 Programs (`programs_view`), 7 Your machine (`equipment_view`) and 8 Requirements (`requirements_view`, hung from Settings). The "Central dashboard" ("Dashboard Central") of 0.1.0 (tab 0) no longer exists.
- **System settings** shows only the families that work on the detected machine (`AmdDriverTweaks` only on AMD GPUs without a driver lock; `PowerModeTweaks` only when the power policy applies). The list comes from the catalog `src/session/settings_catalog.*`, which decides which row exists, whether it is experimental and what evidence it has.

## 3.3b. The `session/` layer and the screens

**The essentials.** A screen is split into three pieces: `ui/<x>_view` draws and creates the controls; `session/<x>_screen` is a **pure model** (data ready to show, with no calls to Windows); and `session/*_actions` does what the buttons do. `ui/` only reads the model, so it decides nothing.

**Example.** In "Games", pressing "Apply profile" makes the view call `on_ApplyGameEnginePreset(app_id)`. That enters `GameActions::Apply`, which writes the game's files through the classes of `optimize/` and returns the sentence shown in the status bar. A test builds `GameActions` with fake files and checks that sentence without opening any window.

| `session/` piece | What it contains |
| --- | --- |
| `*_screen.*` (`home`, `settings`, `games`, `history`, `config`, `programs`, `equipment`, `requirements`) | Screen models: rows, texts, empty states and which button is active |
| `GameActions` (`game_actions.*`) | The Games buttons: apply or revert a game's profile; exclude it ("Me fue mal", "it went badly for me") or include it; install or remove DXVK; and reread the Steam library. The two readings that touch Windows (scanning Steam, rereading a folder) are passed in as functions |
| `SettingsActions` (`settings_actions.*`) | The switches on System settings, one per catalog row (a switch changes only if the current state is different, so a second click never undoes by mistake), and "put everything back to the Windows value" (`ResetAll`) |
| `ConfigActions` (`config_actions.*`) | The edits on Settings and Programs. Each one is applied to a copy, validated by the same rule the daemon uses to read `config.json`, and the in-memory configuration only changes if the file was written. A rejected edit leaves the file and the screen as they were |
| `RestoreAllMessage` (`restore.*`) | The sentence after "Revert all" ("Devolver todo"): the journal summary if there were entries; otherwise, how many saved registry values were restored; otherwise, that there was nothing to change. It is a pure function with its own test |

**Layer rules** (checked by [`scripts/check_layers.sh`](https://github.com/partprogramming09/vegaflow-universal/blob/main/scripts/check_layers.sh), which runs in CI): `ui/` may only include `session/` and `model/`; `session/` includes `model/`, `ports/` and `bridge/` and, while the migration lasts, `windows/`, `detect/` and `optimize/`; `app/` may include everything, to assemble the pieces.

**What stays in `src/app/gui_mode.cpp`, and why.** `gui_mode.cpp` builds the objects, connects them to the buttons and keeps the session state. These stay there on purpose:

- The **telemetry poll** (`on_PollTelemetry`): it returns a `GuiDashboardData`, a structure that lives in `ui/`, and `session/` cannot include `ui/`.
- **Purging memory, isolating background processes, restoring affinity and polling the history:** they call classes that touch Windows directly and for which there is no fake version. Moving them to `session/` would not make them testable without Windows.
