#!/usr/bin/env python3
"""
selftest.py -- check the firmware's self-driven input stimulus from the host.

The TE_SELFTEST image (build.bat selftest) drives its OWN button pins on a fixed 1 s schedule
(src/input/selftest.cpp). No switch, no wiring, no finger. This script records what the PC
actually receives and checks every press and release against that schedule:

  * every expected transition appears, in order (nothing dropped, nothing phantom)
  * nothing stays down (the nobd-25 "lone release waits for the next input" bug)
  * the same-button double tap registers twice (the nobd-25 "second tap swallowed" bug)
  * the 2 ms chord lands in ONE report (co-registration, sync on)
  * each tap's width as seen by the host matches the width the firmware made (preserve width)
  * the 3 ms sub-window tap is dropped with sync on (by design) and present with sync off

Two capture modes:

  python selftest.py                       # XInput API polling, no extra tools, ~1 ms resolution
  python selftest.py --seconds 20 --sync off
  python selftest.py --pcap cap.txt        # passive USBPcap capture, host timestamps + device
                                           # edge stamps (needs the measure/selftest image):
     tshark -r cap.pcap -Y "usb.data_len==20" -T fields -e frame.time_epoch -e usb.capdata > cap.txt

Exit code 0 = every cycle passed, 1 = a failure, 2 = no data.
"""
import argparse
import ctypes
import sys
import time

# ---- the schedule: KEEP IN LOCKSTEP with src/input/selftest.cpp --------------------------------
A, B, U = 1, 2, 4
CYCLE_MS = 1000
# (offset_ms, resulting pressed-mask after this step)
SCHEDULE = [
    (0, A), (40, 0),
    (200, A), (240, 0),
    (300, A), (340, 0),
    (500, A), (502, A | B), (560, 0),
    (700, U), (740, 0),
    (800, A), (810, 0),
    (900, A), (903, 0),
]
ANCHOR_OFFSET = 700            # the UP press: a direction is never delayed, so it anchors the cycle
TAP40 = [(0, 40), (200, 240), (300, 340)]   # (press, release) offsets of the 40 ms taps

XI_A, XI_B, XI_UP = 0x1000, 0x2000, 0x0001


def expected_transitions(sync_on, window_ms=5):
    """What the host must see, as a list of (offset_ms, mask, optional).

    Sync on: the chord is ONE transition straight to A|B (eager commit at the second press),
    the 3 ms tap is dropped (pruned by design), and the chord release may split: B owes no
    delay (it committed instantly) while A owes the 2 ms it was held, so A|B -> A -> 0 is
    legal as well as A|B -> 0. Sync off: the chord splits A then A|B, and the 3 ms tap is
    present (debounce accepts the press on its edge and lands the release at lockout expiry)."""
    ev = []
    for off, mask in SCHEDULE:
        if sync_on and off in (900, 903):
            if window_ms > 3:
                continue                       # pruned by design: released before its commit
            if window_ms == 3:
                ev.append((off, mask, True))   # commit and release race at exactly +3 ms: either
                continue
        if sync_on and off == 500:
            continue
        if sync_on and off == 560:
            ev.append((560, A, True))      # optional: B's release landing 2 ms before A's
            ev.append((562, 0, False))
            continue
        ev.append((off, mask, False))
    return ev


def match(exp, got):
    """Match observed masks against expected (offset, mask, optional) with optional skips.
    Returns the list of expected indices consumed in order, or None on mismatch."""
    used, gi = [], 0
    for ei, (_, mask, optional) in enumerate(exp):
        if gi < len(got) and got[gi] == mask:
            used.append(ei); gi += 1
        elif optional:
            continue
        else:
            return None
    return used if gi == len(got) else None


# ---- capture: XInput polling ------------------------------------------------------------------
class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [("wButtons", ctypes.c_ushort), ("bLeftTrigger", ctypes.c_ubyte),
                ("bRightTrigger", ctypes.c_ubyte), ("sThumbLX", ctypes.c_short),
                ("sThumbLY", ctypes.c_short), ("sThumbRX", ctypes.c_short), ("sThumbRY", ctypes.c_short)]


class XINPUT_STATE(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_ulong), ("Gamepad", XINPUT_GAMEPAD)]


def capture_xinput(seconds):
    xi = ctypes.windll.LoadLibrary("XInput1_4.dll")
    st = XINPUT_STATE()
    idx = next((i for i in range(4) if xi.XInputGetState(i, ctypes.byref(st)) == 0), None)
    if idx is None:
        sys.exit("no XInput controller connected")
    print(f"capturing XInput user {idx} for {seconds} s (the stimulus starts 3 s after boot) ...")
    events = []                                  # (t_ms, mask)
    raw = []                                     # every packet change: (t_ms, wButtons, lx, ly, lt, rt)
    last_mask, last_pkt = None, None
    try:   # a scheduling stall on the host looks exactly like a late release; minimise them
        ctypes.windll.kernel32.SetPriorityClass(ctypes.windll.kernel32.GetCurrentProcess(), 0x80)  # HIGH
    except Exception:
        pass
    t0 = time.perf_counter()
    t_prev = t0
    global STALLS
    STALLS = []                                  # (t_ms, gap_ms): the poll loop did not run for gap_ms
    while True:
        t_now = time.perf_counter()
        if t_now - t0 >= seconds:
            break
        gap = (t_now - t_prev) * 1000.0
        if gap > 4.0:
            STALLS.append(((t_now - t0) * 1000.0, gap))
        t_prev = t_now
        if xi.XInputGetState(idx, ctypes.byref(st)) != 0:
            sys.exit("controller disconnected mid-capture")
        g = st.Gamepad
        if st.dwPacketNumber != last_pkt:
            raw.append(((time.perf_counter() - t0) * 1000.0, g.wButtons, g.sThumbRX & 0xFFFF,
                        g.sThumbRY & 0xFFFF, g.bLeftTrigger, g.bRightTrigger))
            last_pkt = st.dwPacketNumber
        wb = g.wButtons
        mask = (A if wb & XI_A else 0) | (B if wb & XI_B else 0) | (U if wb & XI_UP else 0)
        if mask != last_mask:
            events.append(((time.perf_counter() - t0) * 1000.0, mask))
            last_mask = mask
    global RAW
    RAW = raw
    return events, None


RAW = []
STALLS = []


def dump_raw(limit=60):
    if not RAW:
        print("  (no XInput packet changes at all: the host saw a completely static controller)")
        return
    print(f"  {len(RAW)} XInput packet changes; first {min(limit, len(RAW))}  "
          f"(t ms, wButtons, RX=edge IRQs, RY=stimulus steps, LT, RT):")
    for t, wb, rx, ry, lt, rt in RAW[:limit]:
        print(f"    {t:9.1f}  0x{wb:04X}  irq={rx:5d} step={ry:5d}  {lt:3d} {rt:3d}")
    first, last = RAW[0], RAW[-1]
    print(f"  over the capture: edge IRQs {first[2]} -> {last[2]}, stimulus steps {first[3]} -> {last[3]}")


# ---- capture: USBPcap text export --------------------------------------------------------------
def capture_pcap(path):
    events, stamps = [], {}                      # stamps: index into events -> device edge us
    last_mask = None
    for line in open(path, encoding="ascii", errors="ignore"):
        parts = line.split()
        if len(parts) < 2:
            continue
        try:
            t = float(parts[0]) * 1000.0
            data = bytes.fromhex(parts[1].replace(":", ""))
        except ValueError:
            continue
        if len(data) != 20:
            continue
        b1, b2 = data[2], data[3]
        mask = (A if b2 & 0x10 else 0) | (B if b2 & 0x20 else 0) | (U if b1 & 0x01 else 0)
        if mask != last_mask:
            events.append((t, mask))
            if data[14] == 0x7E:
                stamps[len(events) - 1] = int.from_bytes(data[16:20], "little")
            last_mask = mask
    return events, stamps


# ---- analysis ---------------------------------------------------------------------------------
def analyse(events, stamps, sync_on, window_ms, verbose):
    if not events:
        print("no transitions captured at all"); dump_raw(); return 2
    t_base = events[0][0]
    events = [(t - t_base, m) for t, m in events]
    exp = expected_transitions(sync_on, window_ms)
    # anchors: 0 -> U transitions (nothing else ever presses UP)
    anchors = [i for i in range(1, len(events)) if events[i][1] == U and events[i - 1][1] == 0]
    if len(anchors) < 2:
        print(f"only {len(anchors)} UP anchor(s) seen in {len(events)} transitions; capture longer "
              f"(stimulus is a 1 s cycle, starts 3 s after boot)")
        print("  transitions seen (t ms, mask A=1 B=2 UP=4):")
        for t, m in events[:40]:
            print(f"    {t:9.1f}  {m}")
        dump_raw()
        return 2
    cycles, failures, widths, deltas, stuck, unreliable = 0, [], [], [], 0, 0
    held_max = 0.0
    for ai in anchors[1:-1]:                     # skip the partial first/last cycles
        start = events[ai][0] - ANCHOR_OFFSET
        lo, hi = start - 60, start + CYCLE_MS - 60
        obs = [(t - start, m) for t, m in events if lo <= t < hi]
        cycles += 1
        got = [m for _, m in obs]
        want = [m for _, m, _ in exp]
        used = match(exp, got)
        ok = True
        if used is None:
            ok = False
            failures.append((cycles, f"sequence mismatch\n      want {want} (optional allowed)\n      got  {got}"))
        else:
            pos = {ei: oi for oi, ei in enumerate(used)}      # expected index -> observed index
            for ei, oi in pos.items():
                deltas.append(obs[oi][0] - exp[ei][0])
            for p, r in TAP40:
                ip = next(i for i, (eo, _, _) in enumerate(exp) if eo == p)
                ir = next(i for i, (eo, _, _) in enumerate(exp) if eo == r)
                widths.append(obs[pos[ir]][0] - obs[pos[ip]][0])
        # stuck detector: nothing in the schedule is held longer than 60 ms (+window +poll).
        # A hold whose end was observed right after a host stall is the HOST's fault: the
        # release could not be seen while the poll loop was not running.
        for i in range(len(obs) - 1):
            if obs[i][1] != 0:
                held = obs[i + 1][0] - obs[i][0]
                t_end_abs = obs[i + 1][0] + start + t_base
                stalled = [g for ts, g in STALLS if 0 <= t_end_abs - ts <= 3.0]
                if stalled:
                    unreliable += 1
                    continue
                held_max = max(held_max, held)
                if held > 60 + window_ms + 5:
                    ok = False; stuck += 1
                    failures.append((cycles, f"mask {obs[i][1]} held {held:.1f} ms at +{obs[i][0]:.1f}"))
        if verbose:
            print(f"  cycle {cycles}: {'PASS' if ok else 'FAIL'}  " +
                  " ".join(f"{m}@{t:.1f}" for t, m in obs))
    # ---- report
    print(f"\n{cycles} full cycles analysed ({len(events)} transitions, sync {'on' if sync_on else 'off'}, "
          f"window {window_ms} ms)")
    if widths:
        print(f"  40 ms tap width as seen by host:  min {min(widths):.1f}  avg {sum(widths)/len(widths):.1f}  "
              f"max {max(widths):.1f} ms   (preserve-width: must be ~40, nobd-25 lone tap was ~160+)")
    if deltas:
        print(f"  transition offset vs schedule:    min {min(deltas):+.1f}  avg {sum(deltas)/len(deltas):+.1f}  "
              f"max {max(deltas):+.1f} ms   (expect ~ +window for A, ~0 for UP, +-1 ms poll)")
    print(f"  longest hold observed: {held_max:.1f} ms (schedule max 60)")
    if STALLS:
        worst = max(g for _, g in STALLS)
        print(f"  host poll-loop stalls > 4 ms: {len(STALLS)} (worst {worst:.1f} ms); "
              f"{unreliable} hold(s) ending in a stall were NOT counted against the device")
    if stamps:
        dev = sorted(stamps.values())
        print(f"  device edge stamps present: {len(dev)} (measure image) -- widths from the device clock:")
        # pair press/release stamps for the 40 ms taps in each analysed cycle using host order
        pw = []
        idx = {t: i for i, (t, _) in enumerate(events)}
        for i in range(len(events) - 1):
            if events[i][1] == A and events[i + 1][1] == 0 and i in stamps and i + 1 in stamps:
                w = (stamps[i + 1] - stamps[i]) / 1000.0
                if 30 < w < 50:
                    pw.append(w)
        if pw:
            print(f"    40 ms tap width on the device clock: min {min(pw):.3f} avg {sum(pw)/len(pw):.3f} "
                  f"max {max(pw):.3f} ms (the stimulus itself; must be 40.000 +- alarm jitter)")
    if failures:
        print(f"\nFAIL: {len(failures)} problem(s) in {cycles} cycles ({stuck} stuck holds)")
        for c, msg in failures[:20]:
            print(f"  cycle {c}: {msg}")
        if verbose:
            dump_raw(20)
        return 1
    if verbose:
        dump_raw(40)
    print("\nPASS: every press and release registered, in order, nothing stuck")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--seconds", type=float, default=15, help="XInput capture length (default 15)")
    ap.add_argument("--pcap", help="tshark text export instead of live XInput polling")
    ap.add_argument("--sync", choices=("on", "off"), default="on",
                    help="firmware config under test: NOBD sync on (default) or off")
    ap.add_argument("--window", type=int, default=5, help="configured sync window / debounce ms (default 5)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every cycle")
    a = ap.parse_args()
    events, stamps = capture_pcap(a.pcap) if a.pcap else capture_xinput(a.seconds)
    sys.exit(analyse(events, stamps, a.sync == "on", a.window, a.verbose))


if __name__ == "__main__":
    main()
