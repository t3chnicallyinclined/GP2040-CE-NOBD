# Why NOBD Exists: The Evidence

NOBD rests on one claim: **1000Hz USB polling reads your inputs at a finer resolution than human intent exists, splitting a single intended press into separate inputs.** This document is the evidence for that claim. For what NOBD is and why it is fair, see the **[Position Statement](POSITION.md)**.

> NOBD was built for and tested on Marvel vs Capcom 2 (via Marvel vs Capcom Fighting Collection on Steam). That is the proven case. The same mechanism applies to any game that requires simultaneous presses and has no input leniency, but MvC2 is the one actually measured here. I'm a cloud engineer, not a firmware dev. Everything below was pieced together from datasheets, API docs, community threads, testing, and a lot of trial and error. If you spot something wrong, correct me.

---

## The problem

You press two buttons "together" and the game sees only one, then the other a frame later. You get a jab instead of a dash. That is not execution. Your fingers are never truly simultaneous: even fast, practiced inputs land **2 to 8ms apart**. On Dreamcast (60Hz polling) that gap was invisible. On modern hardware polling at 1000Hz, it is fully exposed and splits your inputs across frames.

This is a documented community problem, not theory:

> *"I'm having trouble doing the 2-button dashes consistently... this might force me to get an actual fight stick with buttons I can assign the dash to."* — [Steam Community Discussion](https://steamcommunity.com/app/2634890/discussions/0/4755326933235585026/)

It is a [long-standing observation](https://archive.supercombo.gg/t/you-think-mvc2-is-hard-to-play-on-pad/133861): MvC2 does not recognize buttons pressed together unless they land in the same frame.

---

## How USB polling works

The host asks the controller for its current state at a fixed interval. The controller keeps its state current and hands it over when asked. The interval is set by the USB descriptor's `bInterval`:

| Platform | Poll interval | Rate |
|----------|--------------|------|
| PC / XInput | 1ms | 1000Hz |
| PS4 / PS3 (wired) | 1ms | 1000Hz |
| Switch (HID) | 1ms | 1000Hz |
| Dreamcast (Maple Bus) | ~16.67ms | 60Hz |

*Dreamcast polling from [dreamcast.wiki](https://dreamcast.wiki/Maple_bus).*

At 1000Hz, **any finger gap over 1ms produces at least one report showing the first button alone.**

---

## Testing: how often does MvC2 read input per frame?

The claim that MvC2 has no multi-read leniency is testable. We sent single button pulses of exact durations via a virtual controller (ViGEmBus) and counted how many the game registered. If a game reads once per frame (~16.67ms), catch rate ≈ pulse / 16.67ms.

| Pulse duration | Caught | Rate | Predicted (1 read/frame) | Predicted (3 reads/frame) |
|---------------|--------|------|--------------------------|---------------------------|
| 1ms | 1 / 20 | 5% | 6% | 18% |
| 8ms | 10 / 20 | 50% | 48% | 100% |
| 16ms | 20 / 20 | 100% | 96% | 100% |

The results match the single-read-per-frame model and are inconsistent with multi-read leniency. For comparison, [SF6 reads inputs three times per frame](https://www.eventhubs.com/news/2023/jun/17/sf6-input-trouble-breakdown/) and would catch a 1ms pulse ~18% of the time. MvC2 does not. It reads input about **once per frame, with no leniency and no buffering.**

---

## What 1000Hz does to a split press

```
USB polls:  |--1ms--|--1ms--|--1ms--|--1ms--|
Your fingers:
  LP pressed ──────●
  HP pressed ─────────────●
                   |← 3ms →|

Poll #1: LP=1, HP=0  → reports LP only  ← STRAY
Poll #2: LP=1, HP=0  → reports LP only  ← STRAY
Poll #3: LP=1, HP=0  → reports LP only  ← STRAY
Poll #4: LP=1, HP=1  → reports both
```

The controller faithfully reports the stray. Whether the game acts on it depends on how it reads input, but for MvC2, stray inputs are a consistent, observable problem: split LP+HP gives a jab instead of a dash, and there is no buffer or leniency to save you.

---

## The Dreamcast comparison

On Dreamcast the controller state is read once per VBlank (~16.67ms), synchronized to the frame. Both buttons are already held by the time the console looks, so the gap is invisible. This is in the source: the [KallistiOS SDK](http://gamedev.allusion.net/docs/kos-current/maple_8h_source.html) ties Maple bus DMA to the VBlank interrupt handler, called "on every VBL (~60fps)." Controller read, game logic, and render all fire on the same 60Hz heartbeat.

1000Hz USB removed that frame-synchronized read. Lowering the USB polling rate to 60Hz does not bring it back, because the problem was never frequency, it was **synchronization** to the game's frame, which a USB device cannot see.

---

## Intent and resolution

The 16ms frame was the game's window for reading intent: anything inside one frame, it treats as one intended moment. Human intent lands at 2 to 8ms. USB's 1ms read is finer than human intent exists, so it slices one intention into two inputs.

NOBD restores a window wide enough to hold one human intent (default 5ms), and aims it at your press so it never slices one in half. It is **stricter than the original 16ms hardware window, not looser.** Full reasoning and the fairness case are in the **[Position Statement](POSITION.md)**.

---

## How NOBD fixes it

When a new press is detected, the firmware holds it briefly (default 5ms) instead of reporting it immediately. Additional presses during that window join it. When the window expires, they commit together, on the same report.

This is not a new mechanism. It is what USB polling already does. At 1000Hz, the host reads the controller once per millisecond, so every press is already held, invisible to the game, until the next poll up to 1ms away, and two presses that land in the same 1ms interval are already reported together. Standard polling, including stock GP2040-CE, is a hold-and-group window. It is just 1ms wide and fixed to a clock, too small to contain a 2 to 8ms finger gap, so it splits the press. NOBD changes one thing: the window starts when you press instead of on a fixed clock tick, and it is wide enough to hold one human intent. Same mechanism, aimed at your finger.

- **Releases apply immediately.** Negative edge and fast inputs are unaffected.
- **Bounce filtering is built in.** The buffer is continuously validated against live GPIO, so a press that bounces off during the window is dropped before commit.
- **It replaces stock debounce.** The two are mutually exclusive.

---

## The latency tradeoff (honest)

NOBD is not "zero added latency." It trades up to 5ms of first-press latency for reliable simultaneous delivery. That 5ms is:

- Less than one-third of a single 60fps frame (16.67ms).
- The same timing budget stock debounce already spends filtering noise.

For single-button actions, the worst case is 5ms of added latency. For simultaneous presses, it eliminates the dropped input. You trade a little speed for consistency. That is the whole deal, and it is a tradeoff, not a free advantage.

---

## References

**USB and input systems:**
- [USB HID Specification](https://www.usb.org/hid) — bInterval and polling
- [USB Polling Rate for Controllers in Emulation](https://pulsegeek.com/articles/usb-polling-rate-for-controllers-in-emulation/) — why polling rate alone doesn't solve frame sync
- [SF6 Input Polling Analysis](https://www.eventhubs.com/news/2023/jun/17/sf6-input-trouble-breakdown/) — SF6 reads inputs 3x per frame

**Dreamcast and arcade hardware:**
- [Dreamcast Maple Bus](https://dreamcast.wiki/Maple_bus) — 60Hz VBlank-synced polling
- [KallistiOS maple.h](http://gamedev.allusion.net/docs/kos-current/maple_8h_source.html) — VBlank-driven Maple DMA source
- [Sega NAOMI Hardware](https://en.wikipedia.org/wiki/Sega_NAOMI) — shared architecture with Dreamcast

**Switch bounce:**
- [A Guide to Debouncing](https://www.ganssle.com/debouncing.htm) — Jack Ganssle's switch bounce study

**Community reports:**
- [MVC Fighting Collection — Steam Discussions](https://steamcommunity.com/app/2634890/discussions/0/4755326933235585026/)
- [Shoryuken Archive — MVC2 on Pad](https://archive.supercombo.gg/t/you-think-mvc2-is-hard-to-play-on-pad/133861)

**NOBD project:**
- [GP2040-CE NOBD Repository](https://github.com/t3chnicallyinclined/GP2040-CE-NOBD)
- [Finger Gap Tester](https://github.com/t3chnicallyinclined/finger-gap-tester)
- [Position Statement](POSITION.md)
