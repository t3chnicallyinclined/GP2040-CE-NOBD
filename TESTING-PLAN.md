# GP2040-TE — Test-Driven Architecture Plan (TigerBeetle-style)

**Branch:** `gp2040-te`. We wire the buffer+reflector architecture the same way
nobd-zero-v2 does: **reference vs shipping + differential fuzzing + DST**, per
`nobd-zero-v2/docs/ENGINEERING-STANDARD.md`. Nothing subtle ships unproven.

## The method (why)
Every latency-critical piece exists twice — a **Python reference** (the spec) and
the **C that ships** — and a **seeded differential fuzzer** proves they produce
byte-identical output on random input. A failure prints a replayable seed. So we
change SOCD / the pipeline / the reflectors / the LAN frame with *proof*, not hope.

## The huge head start — what to work FROM (all in `../nobd-zero-v2`)
The tested input pipeline **already exists** and is portable (host + chip):

| Asset | What it is |
|---|---|
| `firmware/core/` | the **portable input pipeline in C** — `buffer`, `input_pipeline`, `socd`, `sync_window`, `remap`, `turbo`, `macros`, `reverse`, `focus`, `analog`, `hotkeys`, `profiles`. Host+chip. **This becomes GP2040-TE's input core.** |
| `reference/input/*.py` | the **Python golden spec** for each stage (`socd.py`, `sync_window.py`, `pipeline.py`, …) |
| `reference/input/dst.py` + `core_ffi.py` | the **differential fuzzer** — runs C-vs-Python on random input, replayable seed |
| `firmware/retro/maple/dst.py` | the **VOPR pattern** (seeded fault injection) — the model for the LAN reflector's PIO 10BASE-T |
| `docs/ENGINEERING-STANDARD.md` | TigerStyle rules (static alloc, bounded loops, assert-twice) |

**Key consequence:** we don't re-implement SOCD/sync/remap in GP2040-CE. We **adopt
the proven core** and make GP2040-TE a thin adapter + reflectors around it. nobd-zero-v2
stays the reference home; its `dst.py` keeps proving the exact C we ship.

## The plan — staged, each step build-green + differentially proven

### Phase 0 — Adopt the tested core (START HERE)
- Vendor `nobd-zero-v2/firmware/core/` into GP2040-TE as `src/input/core/` (source of
  truth stays nobd-zero-v2; the DST fuzzer there keeps proving it).
- Add it to CMake; confirm it compiles for the RP2040.
- Re-run `nobd-zero-v2/reference/input/dst.py` → the core is proven byte-identical to spec.
- **Deliverable:** GP2040-TE ships a DST-proven input pipeline (buffer + all stages).

### Phase 1 — RP2040 adapter → the buffer
- `src/input/` becomes a thin adapter: `gpio_get_all()` → feed `input_pipeline` (the core)
  → the core writes **`buffer`** (the single source of truth). Retire GP2040-CE's tangled
  gamepad SOCD/remap in favor of the core.

### Phase 2 — Reflectors read the buffer (SOCD-once — the bug fix)
- USB reflector builds its report from `buffer`.
- **DC reflector builds CMD9/Maple from `buffer`, not `debouncedGpio`** (`gp2040.cpp:483`) —
  SOCD-once, unified, and the DC-skips-SOCD bug is gone.
- Integration test: same input → USB and DC agree on the logical state (the SOCD-once invariant).

### Phase 3 — Tournament optimizations on the wired path
- A1 RAM-pin the pipeline (`__no_inline_not_in_flash_func`), A2 fire-on-first-edge debounce,
  A3 USB report SOF-synced (CPU out of the per-poll path).

### Phase 4 — The LAN reflector (Track B)
- **Frame core:** raw-Ethernet frame + CRC32 as a portable C unit + Python reference + fuzzer
  (same pattern; nobd-zero-v2 `firmware/net/` has the FGCC packet to model from).
- **PIO 10BASE-T TX:** a host simulator + **VOPR** (seeded wire faults), modeled on the Maple VOPR.
- **PC receiver:** Npcap raw socket by EtherType → ViGEmBus/HIDMaestro virtual XInput.

## Where we start
**Phase 0.** It's the foundation every reflector reads from, it's *already proven*, and it
immediately replaces GP2040-CE's tangled SOCD with the tested core. Then Phase 1 (adapter),
Phase 2 (SOCD-once), and we're on rails — differentially proven at every step.
