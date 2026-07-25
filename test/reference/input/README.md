# input/ — input processing (the source side of the buffer)

The processors that **write** the shared input buffer — the "sources" half of the
buffer+reflector architecture ([../../ARCHITECTURE.md](../../ARCHITECTURE.md)). They run
*before* anything reflects the buffer: raw switch state → **process** → buffer →
every reflector.

| File | What |
|------|------|
| `remap.py` | physical GPIO pin → logical button (the **first** stage; identity by default) |
| `socd.py` | SOCD cleaner: `neutral` / `up_priority` / `last_win` / `first_win` / `bypass` |
| `sync_window.py` | NOBD sync window: same-frame co-registration of near-simultaneous presses |
| `analog.py` | Hall/analog actuation: adjustable actuation point + **rapid trigger** (the latency floor) |
| `turbo.py` | Auto-fire: exact on/off duty pulse on held buttons (post-SOCD) |
| `macros.py` | Recorded input sequences played on a trigger's rising edge (post-SOCD) |
| `hotkeys.py` | button **combos → config actions** (profile switch, SOCD cycle, …); edge-fire + output masking |
| `pipeline.py` | **configurable** ordered pipeline + presets (`low_latency` / `hall` / `reliable`) — **complete** |
| `test.py` | remap + SOCD + sync window + bounce + analog + turbo + macros + hotkeys + pipeline presets |
| `dst.py` | VOPR I1..I17: crash / latency / phantom / co-reg / bounce / rapid-trigger / turbo / macro / **C parity (remap, socd, sync_window, turbo, macros, analog, pipeline, hotkeys) + seqlock buffer** |
| `core_ffi.py` | cffi bridge: builds + binds the `firmware/core/` sources so the VOPR fuzzes the **shipping C** |

## Configurable — latency first (GP2040-webconfig-inspired)

Every stage is **optional and tunable** (`InputConfig` in `pipeline.py` — the
schema a web UI would later expose). Defaults are **low-latency-first**: the sync
window is **off**, only SOCD is on (tournaments require it).

- **There is no software debounce stage.** Bounce rejection is hardware (near-zero);
  and the sync window **absorbs intra-window press-bounce** — proven by `dst.py`'s
  **I5** tier — so software debounce would be redundant *and* add latency. It must
  never be mandatory.
- **For the absolute floor:** Hall-effect buttons (no bounce at all) + window off →
  raw sub-µs. Preset `InputConfig.hall()`.
- **For mechanical reliability:** `InputConfig.reliable()` turns the sync window on
  (it absorbs bounce) — you accept its window of latency in exchange.

## Pipeline order (it matters)

```
raw switches → hardware debounce → NOBD sync window → remap → SOCD → turbo/macros → buffer
```

- **Debounce and the sync window are SEPARATE stages on V2.** Bounce rejection is
  *hardware* (timer input-capture filter / comparator hysteresis) at the pin —
  near-zero latency. The **NOBD sync window** then groups near-simultaneous presses
  onto one frame (dashes/throw-techs) — the same-frame feature (V1's
  `syncGpioGetAll()`). It **deliberately adds up to its window** of latency; that's
  the co-registration trade. (On V1 these were one-or-the-other in *software*; on V2
  the hardware debounce is free, so they coexist.)
- **Remap before SOCD** — SOCD resolves *logical* directions (two physical pins can
  map to one logical UP). `socd.py` already operates on logical names.
- **SOCD once, before the buffer** — so every reflector sees the same cleaned state.
- **Turbo/macros after SOCD** — they modulate the already-resolved button (GP2040-CE
  loads turbo "close to the end").
- All of it runs on the **V3F core** (the PIOC has 2 pins — it cannot scan buttons,
  window presses, or run SOCD).

## SOCD

**S**imultaneous **O**pposite **C**ardinal **D**irections (Left+Right, Up+Down) — a
leverless-controller issue that tournaments require cleaning by default. Modes:

- **neutral** — both opposing directions cancel
- **up_priority** — U+D → Up; L+R → neutral
- **last_win** — most recently pressed direction wins (stateful)
- **first_win** — first pressed direction holds (stateful)
- **bypass** — raw dual-press passes

This is the **reference logic**. The "first stick with *hardware* SOCD" claim is
running exactly this in the PIOC edge window / on the V3F core, off the hot path,
for zero added latency — see `docs/roadmap/CHIP-EXPLOITATION.md`.

```bash
python input/test.py
```
