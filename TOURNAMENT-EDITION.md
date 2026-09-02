# GP2040-TE — Tournament Edition

**Branch:** `gp2040-te` · fork of GP2040-CE-NOBD · *brought to you by NOBD.*

**Goal:** the lowest-latency, **zero-jitter** competitive build. We optimize for
**P99.99 latency and determinism (jitter)** — not average — and every claim must
survive an oscilloscope. (Philosophy per `nobd-zero-v2/docs/roadmap/latency-tricks-catalog.md`.)

## The hard truth (so we optimize the right thing)

The RP2040's USB is **Full-Speed only → a hard 1 kHz / 1 ms poll floor.** No overclock,
descriptor trick, or extra endpoint beats it (that needs USB High-Speed silicon the
RP2040 doesn't have). So the plan is two tracks:

- **Track A** makes the 1 kHz USB path *perfectly deterministic* (zero device-side jitter).
- **Track B** leaves USB entirely — a chip-less raw-Ethernet link to the PC — to beat the 1 ms.

---

## Track A — make the USB path zero-jitter (buildable + testable now)

| # | Change | Win | Target (from the audit) |
|---|---|---|---|
| A1 | `__not_in_flash_func` the XInput hot path | removes XIP cache-miss jitter | `syncGpioGetAll` / `debounceGpioGetAll` / `Gamepad::read` / `Gamepad::process` / `XInputDriver::process` |
| A2 | **Fire-on-first-edge debounce** | detection latency *is* input latency | replaces `debounceGpioGetAll()` in `gp2040.cpp` |
| A3 | **SOF-synced report** via `tud_sof_cb` | kills the 0–1 ms poll jitter *within* the 1 ms | `XInputDriver` (`.sof` is currently `NULL`) |
| A4 | **PIO edge-detect buttons** — read buttons in silicon (~30 ns, CPU sleeps) | the headline: no fightstick does this | port the `maple.pio` + IRQ pattern to button pins |
| A5 | Locked **tournament profile** (zero optional addons) | removes all variable-cost work | config only (`available()` already gates addons) |
| A6 | Never `Storage::save()` mid-match | removes the multi-100 ms flash freeze | guard the save path |

## Track B — beat USB: chip-less PIO 10BASE-T raw Ethernet → PC (flagship)

- **RP2040 bit-bangs 10BASE-T via PIO — no Ethernet chip.** Hardware: `2×47 Ω + 1×470 Ω`
  resistors + an RJ45 magjack. Ref: [kingyoPiyo/Pico-10BASE-T](https://github.com/kingyoPiyo/Pico-10BASE-T).
- **Firmware:** build the raw frame in software —
  `preamble | SFD | dst MAC | src MAC | EtherType 0x88B5 | payload(button state) | CRC32` —
  then PIO Manchester-encodes + transmits, plus NLP link pulses so the NIC links up.
- **PC app:** Npcap/`AF_PACKET` raw socket filtered by EtherType → ViGEmBus / HIDMaestro
  virtual XInput (or `XInputGetState` hook).
- **Latency:** µs-scale, deterministic — beats USB's 1 ms by ~100× on the device+wire side.
- **Limits:** 10 Mbit (≫ what input needs), TX-only (all a push device needs), NLP required,
  magnetics recommended, PC app is new work.

---

## Already on this branch (carried WIP from the reboot investigation)

- **Keep (real fixes):** unbounded-I2C timeout (`lib/PicoPeripherals/peripheral_i2c.cpp`),
  EventManager no-alloc guard (`eventmanager.*`, `gp2040.cpp`).
- **Decide (diagnostics):** fault-capture (`faultcapture.*`, `main.cpp`) + hang breadcrumbs
  (`gp2040.cpp`, `gp2040aux.cpp`, `ButtonLayoutScreen.cpp`) — keep as a permanent net, or trim.

## Recommended build order

1. **Commit the carried WIP as the TE baseline** (clean starting point).
2. **A1–A3** — quick, testable, no new hardware → a zero-jitter 1 kHz stick. Ships the branch's value immediately.
3. **A4** — PIO edge-detect buttons (the novel silicon-read headline).
4. **Track B** — PIO 10BASE-T; needs a magjack + 3 resistors on a bench to fully validate.
