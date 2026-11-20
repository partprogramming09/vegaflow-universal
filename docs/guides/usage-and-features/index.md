# VegaFlow features and how to use the daemon

**In short:** VegaFlow is a Windows program that tries to make games run more smoothly on AMD APUs, chips where the processor and the graphics share the same memory.
It changes some Windows, driver and game settings, and it leaves games with anti-cheat alone.
This section explains what each part does and how to run the program from the command line or from its window.
For example, it can move browsers and other background programs to the last CPU cores, so the game keeps the fastest ones.

**Reader:** people

## 2. VegaFlow's main modules

![VegaFlow's main modules: non-invasive detection passes to the Anti-Cheat Sentinel; without anti-cheat, engine-level optimizations are applied, and with anti-cheat, non-invasive optimizations; both branches end in the central performance subsystem of the kernel and the operating system](../img/modulos-principales.png)

The features are split across four pages. The section numbers (2.1 to 2.11 and 5) are kept, so a reference to a section still matches.

## What each page covers

| Page | Sections | What it covers |
|---|---|---|
| [Windows and driver tweaks](./tweaks.md) | 2.1, 2.2, 2.3, 2.5, 2.9, 2.10 | The Windows, GPU driver and game-engine settings that VegaFlow can change, and how each one is applied |
| [Anti-cheat, file sandbox and hardware tiers](./safety-and-tiers.md) | 2.4, 2.6, 2.11 | How it detects anti-cheat, the rules for files, and the four hardware tiers |
| [System information and the window](./system-info-and-gui.md) | 2.7, 2.8 | The hardware report and its JSON export, and the screens of the window |
| [Daemon and command line](./daemon-and-cli.md) | 5 | The console commands, with examples |
