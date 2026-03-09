# Why 1000Hz USB Polling Breaks Your Dashes (And How NOBD Fixes It)

If you've ever dropped dashes in MvC2, gotten stray jabs instead of throws, or felt like your buttons "just don't work" on PC when they were fine on Dreamcast — this is why.

---

## The Problem You Can Feel But Can't See

You press LP+HP at the same time. You *know* you pressed them together. But the game sees LP on one frame and HP on the next. You get a jab instead of a dash.

This isn't your execution. It's physics — specifically, how USB polling interacts with human fingers.

Your fingers are never truly simultaneous. Even the fastest, most practiced inputs have a **2-8ms gap** between the two contacts closing. On Dreamcast, nobody noticed because the console only checked once every 16.67ms. On modern hardware polling at 1000Hz (once per millisecond), that tiny finger gap is fully visible — and it splits your inputs across frames.

---

## How USB Polling Actually Works

A common misconception is that the controller pushes input to the console. It doesn't. The **host** (PC, PS4, Switch) asks the controller for its current state at a fixed interval. The controller just keeps its state updated and hands it over when asked.

This interval is set by the USB descriptor's `bInterval` field:

| Platform | Poll Interval | Rate |
|----------|--------------|------|
| PC / XInput | 1ms | 1000Hz |
| PS4 (wired) | 1ms | 1000Hz |
| PS3 | 1ms | 1000Hz |
| Switch (HID) | 1ms | 1000Hz |
| Switch Pro | 8ms | 125Hz |
| Dreamcast (Maple Bus) | ~16.67ms | 60Hz |

*Source: USB descriptor `bInterval` values from [GP2040-CE driver source code](https://github.com/t3chnicallyinclined/GP2040-CE-NOBD). Dreamcast polling from [dreamcast.wiki](https://dreamcast.wiki/Maple_bus).*

The firmware's main loop runs at **hundreds of thousands of iterations per second** on the RP2040 (125MHz ARM Cortex-M0+). It's constantly reading GPIO pins and updating button state. But the host only picks up that state when it polls. Between polls, all those intermediate state changes are invisible.

---

## What Stock Debounce Does (And Doesn't Do)

GP2040-CE's stock debounce is a per-pin timer that filters electrical noise from mechanical switches:

```c
// Stock GP2040-CE debounce (simplified from gp2040.cpp)
for (pin = 0; pin < NUM_GPIOS; pin++) {
    if (state_changed(pin) && (now - lastChangeTime[pin]) > debounceDelay) {
        accept_change(pin);
        lastChangeTime[pin] = now;
    }
}
```

**What it does:**
- Each pin has its own timestamp
- After accepting a state change, it ignores further changes on that pin for 5ms
- This filters switch bounce (the electrical contact flickering on/off as it settles)

**What it doesn't do:**
- It has no concept of "other buttons exist"
- Each pin is evaluated independently — there's no grouping
- The first press on any pin is accepted **instantly** (0ms latency)

**Key insight: debounce solves noise. It doesn't solve grouping.** You could set debounce to 0ms or 100ms — it wouldn't change whether your two-button press arrives on the same frame or not. That's a fundamentally different problem.

---

## The Math: Why Strays Are Inevitable at 1000Hz

When USB polls at 1ms intervals and your fingers have a 3ms gap:

```
USB polls:  |--1ms--|--1ms--|--1ms--|--1ms--|--1ms--|--1ms--|
                 ↑                    ↑
            Poll #1               Poll #4

Your fingers:
  LP pressed ──────●
  HP pressed ─────────────●
                   |← 3ms →|

Poll #1: sees LP=1, HP=0  → reports LP only  (STRAY)
Poll #2: sees LP=1, HP=0  → reports LP only  (STRAY)
Poll #3: sees LP=1, HP=0  → reports LP only  (STRAY)
Poll #4: sees LP=1, HP=1  → reports both     (too late)
```

With 1ms polling, **any finger gap greater than 1ms guarantees at least one USB report with only the first button**. At a typical 3ms gap, there are 2-3 reports showing a stray press before the second button appears.

The game reads input once per frame (16.67ms at 60fps). If *any* of those stray USB reports is the one the game reads — you get a jab instead of a dash.

Compare this to Dreamcast at 60Hz:

```
DC polls:  |───────── 16.67ms ──────────|───────── 16.67ms ──────────|
                                        ↑
                                   DC reads input

Your fingers:
  LP pressed ──────●
  HP pressed ─────────────●
                   |← 3ms →|

By the time the Dreamcast polls, BOTH buttons are held → sees LP+HP → DASH
```

**Stray probability by platform (3ms finger gap):**
- 1000Hz USB (PC/PS4): **~100%** — multiple polls land in the gap, guaranteed stray
- 60Hz Dreamcast: **~18%** — only fails if the 3ms gap straddles the frame boundary (3 / 16.67)

This is why dashes feel easy on Dreamcast and unreliable on PC. The hardware is too precise for human fingers.

---

## Switch Type Matters: Leaf Switches and Old Hardware

The problem gets worse with certain switch types.

Classic US arcade cabinets and MAS sticks used **Happ Competition buttons with leaf switches** — a flexible metal leaf that bends to make contact. Modern controllers and Japanese-style sticks (Sanwa, Seimitsu) use **microswitches** — a snap-action mechanism with a distinct click.

These have very different bounce characteristics:

| Switch Type | Typical Bounce Time | Notes |
|-------------|-------------------|-------|
| Quality microswitch (Omron) | ~1-2ms | Clean, consistent snap action |
| Standard microswitch | ~1.5-6ms | Varies by quality |
| Leaf switch | 5-20ms+ | Flexible contact, more oscillation |
| Worn leaf switch | 10ms+ | Degraded contact surface |

*Source: [Jack Ganssle's debounce study](https://www.ganssle.com/debouncing.htm) — the definitive reference on switch bounce measurements. Ganssle specifically notes: "In the bad old days we used a lot of leaf switches which typically bounced forever."*

**Two problems compound:**

1. **More bounce can exceed the debounce window.** If a worn leaf switch bounces for 10ms and the debounce lockout is 5ms, the lockout expires while the switch is still bouncing. The firmware sees a bounce-off as a real release — ghost input.

2. **Mechanical variance widens the effective finger gap.** Different switch types, ages, and wear levels have different actuation depths and spring tensions. Even if your fingers land at the exact same instant, a soft, worn LP button might close its contact 3-5ms before a stiff, newer HP button. This mechanical offset adds directly to your neurological finger gap.

A player on a MAS stick with mixed-wear leaf switches might have an effective finger gap of **8-15ms** — combining 3-5ms of neurological variance with 5-10ms of mechanical variance. At 1000Hz USB polling, that's 8-15 stray reports before both buttons appear.

---

## The Dreamcast Comparison

There's a reason MvC2 veterans say dashes "just work" on Dreamcast. It's not nostalgia — it's three things working together:

**1. 60Hz Maple Bus polling = natural grouping window**
The Dreamcast only reads controller input once per frame via the [Maple Bus](https://dreamcast.wiki/Maple_bus) (~16.67ms). Any two buttons pressed within that window appear on the same frame. With a typical 3ms finger gap, you'd need to be unlucky enough to straddle the exact frame boundary for a stray — about 18% of the time.

**2. CRT display = zero processing pipeline**
No frame buffer, no display processing, no vsync queue. The total latency from button press to pixels on screen is as short as it gets.

**3. NAOMI arcade hardware = the actual game**
The Dreamcast is essentially a home [Sega NAOMI](https://en.wikipedia.org/wiki/Sega_NAOMI) board — same SH4 CPU, same PowerVR2 GPU. MvC2 on Dreamcast isn't a port with rewritten input handling. It's the arcade game running on the arcade hardware.

**NOBD recreates the first piece of that stack** — the natural input grouping — on modern 1000Hz hardware. You get Dreamcast-style press grouping with PC-speed responsiveness for everything else.

---

## How NOBD Fixes It

When a new button press is detected, the firmware holds it in a buffer instead of reporting it immediately. A timer starts (default: 5ms). Any additional presses during that window are added to the buffer. When the window expires, all buffered presses are committed at once — guaranteed to appear on the same USB report.

```
Without NOBD (stock debounce):                With NOBD (5ms sync window):

t=0ms:  LP pressed → reported immediately     t=0ms:  LP pressed → buffered, window opens
t=1ms:  USB poll → [LP]     ← STRAY           t=1ms:  USB poll → [nothing yet]
t=2ms:  USB poll → [LP]     ← STRAY           t=2ms:  USB poll → [nothing yet]
t=3ms:  HP pressed → reported immediately     t=3ms:  HP pressed → added to buffer
t=4ms:  USB poll → [LP, HP]                   t=4ms:  USB poll → [nothing yet]
                                               t=5ms:  window expires → [LP, HP] committed
                                               t=6ms:  USB poll → [LP, HP]  ← CLEAN
```

Key behaviors:
- **Releases are always instant** — no delay on letting go of buttons. Charge moves, negative edge, and rapid inputs are unaffected.
- **Built-in bounce filtering** — the buffer is continuously validated against raw GPIO state. If a switch bounces off during the window, it's automatically cleaned before commit.
- **Replaces stock debounce** — they're mutually exclusive. When NOBD is active, stock debounce is bypassed.

For full configuration details, see the [main README](../README.md).

---

## The Latency Tradeoff (Honest)

Stock debounce and NOBD both use a 5ms timing window, but they use it differently:

| | Stock Debounce (5ms) | NOBD Sync Window (5ms) |
|---|---|---|
| First press latency | **0ms** — accepted instantly | **Up to 5ms** — held for window |
| Bounce filtering | 5ms lockout after each change | Continuous validation during window |
| Multi-button grouping | None — each pin independent | Guaranteed — all presses in window committed together |
| What 5ms buys you | Noise filtering | Noise filtering + grouping |

**NOBD is not "zero added latency."** It trades up to 5ms of first-press latency for guaranteed simultaneous delivery. That 5ms is:
- Less than **one-third of a game frame** (16.67ms at 60fps)
- The **same timing budget** stock debounce uses for bounce filtering
- **Imperceptible** in practice — fighting game reaction times are 150-250ms

The difference: stock debounce spends its 5ms budget filtering noise on individual pins. NOBD spends the same 5ms budget filtering noise *and* grouping presses across pins. Same cost, more capability.

For single-button actions (jabs, blocking, movement), the worst case is 5ms of added latency. For simultaneous presses (dashes, throws, supers), the tradeoff eliminates dropped inputs entirely.

---

## References

**USB and Input Systems:**
- [GP2040-CE Documentation](https://gp2040-ce.info/) — 1000Hz USB polling, sub-1ms latency
- [GP2040-CE FAQ](https://gp2040-ce.info/faq/faq-general/) — General FAQ
- [USB HID Specification](https://www.usb.org/hid) — bInterval and polling mechanics
- [Controller Input Lag Database](https://inputlag.science/controller/results) — Comprehensive latency measurements

**Dreamcast and Arcade Hardware:**
- [Dreamcast Maple Bus](https://dreamcast.wiki/Maple_bus) — 60Hz VBlank-synced polling
- [Sega NAOMI Hardware](https://en.wikipedia.org/wiki/Sega_NAOMI) — Shared architecture with Dreamcast
- [MVC2 Arcade vs Dreamcast](https://archive.supercombo.gg/t/mvc2-differences-between-arcade-version-dreamcast-version/142388) — Version comparison

**Switch Bounce and Debouncing:**
- [A Guide to Debouncing](https://www.ganssle.com/debouncing.htm) — Jack Ganssle's definitive switch bounce study
- [Switch Bounce and Dirty Secrets](https://www.analog.com/en/resources/technical-articles/switch-bounce-and-other-dirty-little-secrets.html) — Analog Devices technical article

**Fighting Game Input Analysis:**
- [SF6 Input Polling Analysis](https://www.eventhubs.com/news/2023/jun/17/sf6-input-trouble-breakdown/) — SF6 reads inputs 3x per frame
- [XInputGetState API](https://learn.microsoft.com/en-us/windows/win32/api/xinput/nf-xinput-xinputgetstate) — Snapshot-only input reading

**NOBD Project:**
- [GP2040-CE NOBD Repository](https://github.com/t3chnicallyinclined/GP2040-CE-NOBD) — Source code and releases
- [Finger Gap Tester](https://github.com/t3chnicallyinclined/finger-gap-tester) — Measure your natural finger gap (Python CLI / Rust GUI)
