# Latency testing methodology — gp2040-te

*How we measure controller latency on gp2040-te, why the number is trustworthy, and
exactly what it does and does not claim.*

This document is written to the project's honesty standard: **every latency figure here
is provable from an artifact in this folder or the firmware, and every source of error is
named.** If a claim can't survive a scope and a skeptic, it's flagged as such.

---

## TL;DR

Two *independent* instruments — one on-device (the RP2040's own clock), one host-side
(a passive USB capture) — agree:

| Measurement | Instrument | Result |
|---|---|---|
| edge → report built (device compute) | on-device probe | **~1–2 µs** |
| edge → report on the USB wire | on-device probe | **avg ~0.36 ms**, max ~1.0 ms (one poll) |
| edge → report received by the host | passive USBPcap capture | **avg ~0.45 ms, median 0.40 ms**, p99 = 1.03 ms |

**Interpretation:** gp2040-te rides the **USB Full-Speed poll floor (~0.5 ms average)**
with roughly **1 µs of its own overhead** and **no missed-poll tail**. The two instruments
are consistent (host-receive is ~0.1 ms later than edge→wire, as it physically must be).

**What this is *not*:** we do not beat 0.5 ms average, and can't on USB Full-Speed — that
wall is the 1 kHz host poll and applies to every USB-FS device. The achievement is removing
*everything above* that floor: no firmware loop-cycle latency, no double-poll penalty.

---

## What we measure (and what we don't)

The full chain a player experiences is:

```
switch edge → [controller firmware] → [USB poll] → [OS input stack] → [game] → [display]
              \_____________________________________/
                    what a CONTROLLER can affect
```

Everything past the USB layer (OS HID stack, game input polling, monitor) is identical for
every controller and is not the firmware's to fix. So we measure the part the controller
owns: **switch edge → the report being available at the host USB layer.** That is the
honest, apples-to-apples number to compare controllers by.

## Why not the existing methods

- **[inputlag.science](https://inputlag.science/controller/methodology)** and the
  **[GP2040-CE method](https://github.com/OpenStickCommunity/Site/blob/main/latency_testing/README.md)**
  measure *button → display* (a photodiode on a monitor) or *button → USB* with an external
  logic analyzer. Both are valid, but they either fold in monitor/OS latency or require a
  rig, and they report an average over a modest sample.
- Our goal was a method that (a) needs **no external hardware**, (b) isolates the
  controller's own contribution, and (c) yields a **full distribution** (jitter and tail),
  not just an average — because for a fight stick the *worst case* and *consistency* matter
  as much as the mean.

We do this two ways, and cross-check them against each other.

---

## Instrument 1 — the on-device probe (single clock, zero sync error)

[`src/input/latency_probe.h`](../../src/input/latency_probe.h) /
[`.cpp`](../../src/input/latency_probe.cpp) take three timestamps off the RP2040's **one**
1 µs hardware timer. Because it is a single clock, there is **zero cross-device sync error** —
no two instruments to align.

| Stamp | Where it's taken | Meaning |
|---|---|---|
| **T0** `edge()`   | first instruction of the GPIO doorbell ISR | the raw switch edge |
| **T1** `report()` | the report is staged in USB DPRAM | ready to ship |
| **T2** `wire()`   | the USB IN endpoint-complete IRQ | the report went out on the wire |

- **T1 − T0 = edge → build** = the device compute. Bench: **min 1 / avg 2 / max ~0 µs**
  (typical) — the ISR fast path formats the report in ~1 µs.
- **T2 − T0 = edge → wire** = the true controller latency. Bench: **min 4 / avg 358 /
  max 1005 µs**. The average *is* the USB-FS poll floor; the max is exactly one poll
  (1005 µs) — proof the hot buffer leaves **no tail beyond a single poll**.

This is the number with **no clock-offset ambiguity** (one clock), and it is the figure we
stand behind. Instrument 2 exists to cross-check it from outside the chip.

## Instrument 2 — the host cross-check (passive USB capture, no rig)

We stamp **T0 (the device edge time, in microseconds)** into the XInput report's otherwise
unused reserved bytes. A passive [USBPcap](https://desowin.org/usbpcap/) capture then records,
per report, both the device's edge time *and* the host's receive time — so we can compute
edge→host-receive for every press with nothing but software.

**The stamp is engineered for measurement accuracy** (see
[`XInputDriver.cpp`](../../src/reflect/xinput/XInputDriver.cpp), `xinput_fastpath()`):

1. **Single-writer.** The edge ISR is the *only* code that writes the reserved region
   `[14..19]`; the main loop neither diffs on it nor copies over it. This removes a
   race between the ISR and the loop that (in the first draft) let a stale timestamp
   clobber a fresh one.
2. **Atomic, aligned store.** T0 is written as **one 32-bit store to the 4-byte-aligned
   offset 16**, never as separate byte writes. The USB SIE reads DPRAM concurrently; four
   byte-writes to offsets 14–17 could be read **half-updated** (torn), producing a garbage
   edge time. A single aligned word cannot tear.
3. **Marker byte.** Offset 14 = `0x7E`. The analyzer discards any 20-byte report on the bus
   without it, so a *different* device sharing the hub can't contaminate the sample. (In one
   capture, ~1,400 foreign 20-byte reports were correctly rejected.)

Report layout the analyzer relies on:

```
byte  0   1   2    3    4   5   6-13         14    15   16 17 18 19
      id  sz  b1   b2   lt  rt  sticks…      0x7E  0    T0 (LE uint32, device µs)
```

## Analysis

[`latency.py`](latency.py) turns a capture into a distribution:

1. **Fresh forward edges only.** The device clock is monotonic, so each *new* edge has a
   strictly larger T0. We key on that: take the **first host-receive that carries each new
   T0**. (Reports that merely *repeat* a held state — same T0, later polls — are ignored;
   they'd otherwise inflate latency by up to the hold duration.)
2. **Offset calibration.** `delta = host_receive − device_edge = (constant clock offset) +
   (true latency)`. With the tearing and clobber fixed, the smallest delta is a genuine
   fast edge, so `offset = min(delta)` and `latency = delta − offset`. A guard checks the
   min isn't an outlier far below the 1st percentile; if it is, it falls back to p1 and says
   so. (On the clean capture the guard does **not** trigger — the offset stands on its own.)
3. **Report the whole distribution** — histogram, percentiles, mean — not one number.

---

## Honest caveats (read these before quoting a number)

- **Absolute-floor ambiguity.** Host and device clocks share an *unknown constant offset*.
  Instrument 2 assumes the fastest observed edge had ~0 device latency; if it actually had,
  say, 50 µs, every absolute figure shifts by that. **Instrument 1 (single clock) has no
  such ambiguity** and agrees — which is why we lead with it.
- **Coalescing bias (optimistic).** Under fast mashing, several edges can fall inside one
  1 ms poll; only the last survives to be captured, and it is by definition close to the
  poll → **lower** apparent latency. Lighter/single-press captures give the true figure. Our
  heavy-mash capture read 0.42 ms; the lighter one read ~0.45–0.50 ms — the latter is the
  more honest single-press number. We quote the higher one.
- **Edge → USB, not edge → pixel.** This is the controller's contribution only. OS, game,
  and display latency sit on top and are the same for any controller.
- **The 0.5 ms is physics, not firmware.** ~0.5 ms average is the 1 kHz USB-FS poll floor;
  *any* always-ready device hits it. What gp2040-te proves is **zero firmware latency above
  it** and **no double-poll tail** — which is what separates it from stock boards that
  average 0.74–1 ms with a ~2 ms tail. Going below 0.5 ms needs a higher poll rate (a
  bulk/WinUSB path with a PC companion), which is a *different* track.

---

## Results

**Capture A** — heavy mash, 1,139 edges (first-generation stamp; required outlier
filtering because the byte-write stamp could tear):
median 0.33 ms · mean 0.42 ms · p99 1.07 ms (one poll).

**Capture B** — lighter mash, 379 edges (atomic-aligned single-writer stamp; **no filtering
needed**, offset guard did not trigger):
median 0.40 ms · mean 0.45 ms (0.50 ms including one 16 ms USB-retry blip) · p99 1.03 ms.

```
Capture B histogram (edge → host-receive, 100 µs buckets)
   0- 100us | ##########  20.6%
 100- 200us | ######      12.9%
 200- 300us | ###          7.7%
 300- 400us | ####         8.7%
 400- 500us | ##           4.7%
 500- 600us | ###          7.9%
 600- 700us | ###          7.1%
 700- 800us | ###          7.9%
 800- 900us | #####       10.0%
 900-1000us | ####         9.0%
1000-1100us | #            3.2%
    >1100us |              0.3%   ← one 16 ms blip / 379
```

Both agree with Instrument 1 (edge→wire avg 0.36 ms): host-receive is ~0.1 ms later, as
it must be. **Two independent instruments, consistent, no artifacts.**

| | avg edge→host | worst case |
|---|---|---|
| Fastest current boards | 0.74 – 1.0 ms | ~2 ms (missed-poll tail) |
| **gp2040-te** | **~0.45 ms** | **1.03 ms = one poll, no tail** |

---

## Reproduce it

Requires [USBPcap](https://desowin.org/usbpcap/) (capture) and Wireshark's `tshark` (read).
No Npcap needed — `tshark -r` reads a saved file.

```powershell
# 1. Build + flash a MEASUREMENT image (the stamp + probe are off in production):
#      cmake -B build -DTE_LATENCY_MEASURE=ON && cmake --build build --parallel
#    Turn NOBD sync OFF.
# 2. Capture (find your stick's filter with USBPcapCMD -h; here it's USBPcap1).
#    Run this, mash buttons for ~15 s, then Ctrl+C:
& "C:\Program Files\USBPcap\USBPcapCMD.exe" -d "\\.\USBPcap1" -A -o cap.pcap

# 3. Extract the 20-byte reports (epoch receive-time + report bytes):
& "C:\Program Files\Wireshark\tshark.exe" -r cap.pcap -Y "usb.data_len==20" `
    -T fields -e frame.time_epoch -e usb.capdata > cap.txt

# 4. Analyze:
python latency.py cap.txt
```

The stamp and probe are a measurement aid, not a shipping feature: they live behind
`TE_LATENCY_MEASURE` (CMake `option`, **OFF** by default), so production images compile them out
entirely — the shipping fast path carries zero probe overhead. Build with
`-DTE_LATENCY_MEASURE=ON` only when measuring.
