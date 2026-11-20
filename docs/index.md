---
layout: home
hero:
  name: VegaFlow Universal
  text: More frame time for games on AMD APUs
  tagline: A Windows daemon in C++20 with a Rust core. It never injects anything into games that use anti-cheat.
  actions:
    - theme: brand
      text: What VegaFlow is
      link: /guides/
    - theme: alt
      text: What is measured
      link: /measurements/verification-status
features:
  - title: Background isolation
    details: Confines background processes (affinity, EcoQoS, I/O priority) so the CPU and the disk stay with the game.
  - title: Per-tier profiles
    details: ApuCapabilityGuard classifies the machine into 4 levels and never applies what was calibrated for one level to another.
  - title: Anti-cheat safe
    details: If an anti-cheat driver is present, SandboxGuard blocks every write and the game's files are not touched.
---
