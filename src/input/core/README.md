# src/input/core — the input pipeline core (self-contained)

gp2040-te's own copy of the DST-proven portable input pipeline: the cross-core
`buffer` + the ordered stages `remap → sync_window → socd → turbo → macros`, plus
`analog`, `focus`, `hotkeys`, `reverse`, `profiles`. Latency-critical logic in
portable C — compiles for the host **and** the RP2040 (Cortex-M0+).

**This repo is self-contained.** The Python reference specs and the DST/VOPR fuzzer
that *prove* this C live in [`test/reference/input/`](../../../test/reference/input/)
(seeded from the sibling nobd-zero-v2 project, now maintained here independently). To
prove the core after ANY change to a `.c`/`.py` pair:

```
cd test/reference/input && python dst.py     # 17 tiers incl. C<->spec parity — must PASS
```

Each stage's C has a matching `.py` spec; keep the two in lockstep — `dst.py` tiers
I9–I17 fuzz C-vs-spec differentially (needs `zig cc` on PATH for the parity tiers).
Behaviour-equivalence vs GP2040's incumbents is proven separately under
[`test/`](../../../test/) (`socd_equiv.c`, `sync_equiv.c`).

**RP2040 note:** `buffer.c`'s seqlock uses C11 `<stdatomic.h>` — correct within a
core; a true cross-core publish would need SIO/barriers (a board bring-up item), and
it's moot on the RP2040 where the input pipeline and the reflectors share Core0.
