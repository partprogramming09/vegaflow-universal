# VegaFlow Universal documentation

**In short:** VegaFlow is a Windows program that keeps background apps out of a game's way on laptops where the processor and the graphics share memory. These pages explain how it works, how to install it and what was really measured. For example, if you want to know why a browser slows a game down on an AMD APU, start with [Hardware and tiers](./hardware-and-tiers/).

**Reader:** people and agents

Start here. Each page answers one question: find yours.

## What do you want to do?

| I want to... | Read |
| --- | --- |
| Understand what VegaFlow is and why it exists | [Vision and principles](./vision-and-principles.md) |
| Build and run the tests | [Build and test](./build-and-test.md) |
| Install or uninstall VegaFlow, or check the zip | [Install and uninstall](./install-and-uninstall.md) |
| Use the daemon or learn what each feature does | [Features](./usage-and-features/) |
| Change a GPU, memory or power optimization | [Hardware and tiers](./hardware-and-tiers/), and before touching it, what was measured in [Verification status](../measurements/verification-status.md) |
| Touch the Rust core or the C++ ↔ Rust boundary | [Rust and C++ core](./rust-cpp-core.md) |
| Touch anti-cheat handling or file isolation | [Security](./security.md) |
| Know what each module does or where a file lives | [Modules](./modules/) |
| Support other hardware (Intel, AMD multi-CCD, dGPU, eGPU) | [Universal pattern](./universal-pattern.md), section 3.5 |
| Follow one module from start to end (`app/`, `session/`, `optimize/`, `windows/`, `ui/`) | [app](./how-it-works-app.md), [session](./how-it-works-session.md), [optimize](./how-it-works-optimize.md), [windows](./how-it-works-windows.md), [ui](./how-it-works-ui.md) |
| Change or redesign the GUI | [How ui/ works](./how-it-works-ui.md) (how a screen is split between `ui/` and `session/`, and the list of screens) |
| See figures and how they were measured | [Measurements](../measurements/verification-status.md) |
| Use the measurement scripts or the remote jobs | [`scripts/README.md`](https://github.com/partprogramming09/vegaflow-universal/blob/main/scripts/README.md) |

## Pages by folder

Each page says on its second line who it is for (`**Reader:**`).

**`guides/`: how it works and how to do things.** Vision and principles, universal pattern, hardware and tiers, Rust and C++ core, security, modules, features, build and test, install and uninstall. Each diagram is an `.html` (the source you edit) with its exported `.png` next to it in `guides/img/`.

**`measurements/`: what was measured.** Dated reports that are never rewritten (written in Spanish), plus `verification-status.md`, the living list of what is measured and what is not.

**`adr/`: decisions.** [ADR 0001](../adr/0001-nucleo-que-decide-y-puertos.md): Rust decides, C++ acts through ports (accepted 2026-11-14).

## Rules

1. A measurement carries its date and is never rewritten; if the method changes, it goes in a new file.
2. Each figure lives in one place and the others link to it.
3. A new page is created only when no existing page answers its question.

Outside git: `docs/estado.md` (the handoff notes of each machine), `docs/plans/`, `docs/research/` and `.claude/`.
