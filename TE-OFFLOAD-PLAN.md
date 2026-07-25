# TE Offload Plan (#2) — "silicon does it, CPU sleeps"

The Tournament-Edition headline: get the input path off the CPU and onto RP2040
silicon (PIO + timers + IRQ), so steady state = CPU asleep and report timing is
**deterministic (zero device-side jitter)**.

## Honest framing (so we optimize the right thing)
- **This buys jitter + power, NOT sub-1ms latency.** Input is already captured in µs by
  the loop; the 1 ms floor is the **USB-FS wire poll**, which no device-side trick beats.
  Beating 1 ms is Track B (chip-less raw Ethernet), a separate effort.
- **Bench-validated, not sim-validated.** PIO/timers/IRQ/WFI are silicon timing — the DST
  proves *logic*, not picoseconds. Each step here is built green then flashed + scoped on
  the board. Until then it is **"plausible," never "done."** (CLAUDE.md golden rule.)
- **Therefore: one flash-tested step at a time.** I can't flash or scope, so we don't
  stack unverified hardware code — each phase is small, buildable, and you validate it
  before the next.

## RP2040 resource map (confirmed)
- **PIO:** `neopico` = pio0 SM0; Maple = pio0/pio1 **DC-mode only**. In the XInput path
  **~7 SMs are free** — a button-reader SM is available.
- **Timers:** system timer (4 alarms) for the sync-window commit deadline / wake; PWM
  slices (~8) for turbo pulses. The core already exposes the hooks:
  `sync_window_next_commit()` and `turbo_next_toggle()` return "ticks until next event."
- **IRQ/WFI:** wake sources = PIO IRQ (button edge) + timer alarm + USB IRQ. `__wfe()`
  sleeps between. Requires **IRQ-driven USB** (the risky bit — can break enumeration).

## Phases (recommended order: safe/high-value first)
| # | Phase | What | Risk | Verify |
|---|-------|------|------|--------|
| **A1** | RAM-pin the hot path | `__not_in_flash_func` the pipeline + reader + report path (+ a portable `CORE_HOT` hook for the core stages) — no XIP cache-miss jitter | **low** (relocation only, can't change logic) | `nm`: funcs at `0x2000xxxx` (SRAM), not `0x10000xxx` (flash). No bench needed to confirm *correct* |
| **A3** | SOF-synced report | send the XInput report aligned to USB SOF (`tud_sof_cb` / driver `.sof`, today `NULL`) — kills the 0–1 ms poll jitter *within* the 1 ms | med | scope report edge vs SOF |
| **A4a** | PIO edge-detect reader | one PIO SM samples all GPIOs, pushes on change + IRQ; Core0 reads the FIFO instead of `gpio_get_all()` | med | flash: inputs still register; logic-probe the SM |
| **A4b** | Event-driven wake (WFI) | Core0 `__wfe()`, woken by PIO IRQ + timer alarm + USB IRQ; run the pipeline on wake. Needs IRQ-driven USB | **high** (USB) | flash: enumerates + no missed inputs |
| **A4c** | HW-timer sync/turbo | alarm for the sync commit deadline; PWM for turbo pulses (drive from the core's `*_next_*` hooks) | med | scope timing |

## Where to start
**A1 (RAM-pin)** — the ideal first step: real jitter reduction, **zero risk to logic**
(it only moves code to RAM), and **verifiable without a bench** (`nm` shows the SRAM
addresses). Then A3 (SOF-sync). A4b (WFI + IRQ-USB) is the riskiest and comes last.

**Before any of this: flash the CURRENT build and feel SOCD-once + the core sync land.**
That's real, done, proven-in-logic work — validate it on the stick before we add silicon.
