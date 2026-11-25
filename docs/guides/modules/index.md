# Modules and repository structure

**In short:** VegaFlow's code is split into layers, and each folder may only use the layers listed for it. That keeps every call to Windows in one place, so the rest of the code can be tested without touching the real system. For example, the GUI draws the Games screen, but the rule that applies a game's profile lives in the `session/` layer, where a test can check it without opening a window.

**Reader:** people

This section explains how the code is organized and where each piece lives. Each subpage covers one topic:

- [Code layers](./layers.md): which folders may use which, and what moved in phase R3 (2026-11-14).
- [SystemInfo and hardware telemetry](./system-info.md): section 3.2, how VegaFlow reads the machine.
- The window, the screen models and the game optimizations are explained module by module in [How ui/ works](../how-it-works-ui.md), [How session/ works](../how-it-works-session.md) and [How optimize/ works](../how-it-works-optimize.md); what each tweak does for the user is in [Windows and driver tweaks](../usage-and-features/tweaks.md).
- [Repository tree](./repo-tree.md): section 4, folder by folder.

## How the layers fit together

Each folder of `src/` may only include the folders listed for it (its own folder is always allowed). The table is the same as the `allowed()` function in `scripts/ci/check_layers.sh`; if one changes, the other changes too.

### Layer table

| Folder | May include |
| --- | --- |
| `app/` (entry points and adapters) | `session/`, `ui/`, `model/`, `ports/`, `windows/`, `bridge/`, `detect/`, `optimize/` |
| `ui/` (GUI) | `session/`, `model/` |
| `session/` (use cases) | `model/`, `ports/`, `bridge/` |
| `ports/` (interfaces) | `model/` |
| `windows/` (adapters) | `ports/`, `model/`, `bridge/` |
| `detect/` (migration) | `model/`, `windows/`, `bridge/` |
| `optimize/` (migration) | `model/`, `ports/`, `windows/`, `bridge/` |
| `bridge/` (Rust bridge) | `model/`, `ports/` |
| `model/` | nothing |

`session/` is strict since F4.3: a file that includes `detect/`, `optimize/` or `windows/` fails the check, and there is no exception list. Each family of Windows calls that `session/` needs is an interface in `ports/` (see [Code layers](./layers.md#what-f4-3-moved-2026-11-25)).

For what the repository checks against this table, see [Code layers](./layers.md#what-the-repository-checks).
