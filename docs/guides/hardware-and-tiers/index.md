# APU hardware and tiers

**In short:** VegaFlow changes its settings depending on the processor and memory a computer has. The test machine has an AMD Ryzen 5 4600H processor and a built-in Radeon RX Vega 6 graphics chip, which shares its 16 GB of memory with the processor. Machines are sorted into four tiers: a machine with 8 GB of memory, for example, gets stricter limits so that it does not run out of memory.

**Reader:** people

**Reviewed:** 2026-11-18 (branch `feat/expansion-e1`). No tier keeps a STAPM frequency cap: `cpu_stapm_cap_mhz = 0` (no ceiling) in Tier 1, 2, 3 and 4.
Sources: `src/detect/hardware_profiler.cpp:268` (sets 0 before the `switch` per tier, with no exceptions); `tests/integration/hardware_tests.cpp:141, 168, 185` (expect 0 in Tier 1, 2 and 3); `tests/vectors/apu_tier.tsv` fixes the classification per tier, not the frequency caps.

## What each page covers

| Page | What it covers |
| --- | --- |
| [APU hardware limits](./apu-limits.md) | Memory shared with the processor, the false 512 MB VRAM report, DXVK GPL, `AsyncCompute` and the thermal budget. |
| [Tiers and guards](./tiers-and-guards.md) | The four tiers and the runtime DXGI pressure guard. |
| [Design record: classification and policies](./design-spec-classification.md) | The problem with the fixed values, the classification rules and the parameter table per tier. |
| [Design record: integration and tests](./design-spec-integration.md) | The changes to each existing module, and the acceptance criteria and test plan. |
