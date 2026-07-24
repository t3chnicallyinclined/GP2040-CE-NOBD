# src/input/core — VENDORED (do NOT edit here)

**Source of truth:** `../../../../nobd-zero-v2/firmware/core/`

These are the DST-proven portable input-pipeline cores — the single tested input
pipeline shared by V2 (CH32H417) and this V1 (RP2040) firmware:

- `buffer` (cross-core seqlock), `input_pipeline` (raw → remap → sync → SOCD → turbo → macros)
- stages: `remap`, `socd`, `sync_window`, `turbo`, `macros`, `reverse`, `focus`, `analog`, `hotkeys`, `profiles`
- `frame.h` (the published `input_frame_t` contract), `buttons.h`

**Proven** byte-identical to `nobd-zero-v2/reference/input/*.py` by that repo's
`reference/input/dst.py` — 17 VOPR tiers incl. full C-parity (I9–I17) via `zig cc`.
Compiles clean for host, Cortex-M0+ (RP2040), and the CH32H417.

**Re-sync** after any core change upstream, then re-run the fuzzer to prove parity
before committing:
```
cp ../../../../nobd-zero-v2/firmware/core/*.c ../../../../nobd-zero-v2/firmware/core/*.h .
rm -f test_core.*
( cd ../../../../nobd-zero-v2/reference/input && python dst.py )   # must PASS all 17 tiers
```

**RP2040 port note:** the `buffer` seqlock uses C11 `<stdatomic.h>`. On the RP2040 those
lower to libcalls that mask interrupts — correct within a core, **not** across the two
cores. True cross-core publish on RP2040 needs SIO spinlock / hardware barriers; that's a
Phase 1/2 wiring detail (buffer.h flags it as a board bring-up item), not a core change.
