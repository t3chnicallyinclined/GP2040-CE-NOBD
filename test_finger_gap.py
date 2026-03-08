"""
NOBD Finger Gap + Stray Press Tester
=====================================
Measures the time gap between simultaneous button presses (like LP+HP
for a dash) AND detects stray single-button presses that could cause
unwanted jabs/punches during wavedashing.

Usage:
  1. Plug in your stick
  2. Run: python test_finger_gap.py
  3. Press two buttons at the same time, over and over
  4. The script shows gaps AND flags any stray single presses
  5. Press Ctrl+C to see summary stats

Requires: pip install pygame
"""

import pygame
import time
import sys
import math
from collections import defaultdict

# --- Configuration ---
PAIR_WINDOW = 0.050       # 50ms - max gap to count as simultaneous pair
BOUNCE_THRESHOLD = 0.005  # 5ms - release-to-repress shorter than this = bounce
USB_FRAME_MS = 1.0        # 1ms USB polling rate for pre-fire detection

pygame.init()
pygame.joystick.init()

if pygame.joystick.get_count() == 0:
    print("No gamepad detected! Plug in your stick and try again.")
    sys.exit(1)

joy = pygame.joystick.Joystick(0)
joy.init()
print(f"Connected: {joy.get_name()}")
print(f"Buttons: {joy.get_numbuttons()}")
print()
print("=" * 65)
print("  NOBD FINGER GAP + STRAY PRESS TESTER")
print("=" * 65)
print()
print("Press two buttons at the same time (like LP+HP for a dash).")
print("This measures finger gaps AND detects stray single presses.")
print()
print("Press Ctrl+C to stop and see full stats.")
print("-" * 65)

# --- State ---
gaps = []                    # gap_ms for each detected pair
strays = []                  # (button_id, solo_ms, reason) for each stray
bounces = []                 # (button_id, off_duration_ms) for each bounce
pair_count = 0

pending_press_time = None    # perf_counter timestamp of first press
pending_press_button = None  # button id of first press

# Per-button state: {button_id: {held, last_press, last_release}}
button_states = {}

def get_button_state(btn):
    if btn not in button_states:
        button_states[btn] = {
            'held': False,
            'last_press': None,
            'last_release': None,
        }
    return button_states[btn]

def record_stray(btn, solo_ms, reason):
    strays.append((btn, solo_ms, reason))
    print(f"  >>> STRAY: Btn {btn} solo for {solo_ms:.1f}ms ({reason})")

def record_bounce(btn, off_ms):
    bounces.append((btn, off_ms))
    print(f"  !! BOUNCE: Btn {btn} re-pressed after {off_ms:.1f}ms off")

try:
    while True:
        now = time.perf_counter()

        # --- Timeout check: pending press expired without a pair ---
        if pending_press_time is not None:
            elapsed = now - pending_press_time
            if elapsed > PAIR_WINDOW:
                record_stray(pending_press_button, elapsed * 1000, "no pair arrived")
                pending_press_time = None
                pending_press_button = None

        # --- Process events ---
        for event in pygame.event.get():
            if event.type == pygame.JOYBUTTONDOWN:
                now = time.perf_counter()
                btn = event.button
                bs = get_button_state(btn)

                # Bounce detection: re-press very quickly after release
                if bs['last_release'] is not None:
                    off_duration = (now - bs['last_release']) * 1000
                    if off_duration < BOUNCE_THRESHOLD * 1000:
                        record_bounce(btn, off_duration)

                bs['held'] = True
                bs['last_press'] = now

                if pending_press_time is None:
                    # First button of a potential pair
                    pending_press_time = now
                    pending_press_button = btn
                elif btn == pending_press_button:
                    # Same button pressed again - refresh timestamp, not a pair
                    pending_press_time = now
                else:
                    # Second (different) button arrived
                    gap_ms = (now - pending_press_time) * 1000

                    if gap_ms <= PAIR_WINDOW * 1000:
                        # Pair detected
                        gaps.append(gap_ms)
                        pair_count = len(gaps)
                        avg = sum(gaps) / pair_count
                        mn = min(gaps)
                        mx = max(gaps)

                        pre_fire = ""
                        if gap_ms >= USB_FRAME_MS:
                            frames = int(gap_ms / USB_FRAME_MS)
                            pre_fire = f"  ** PRE-FIRE: btn {pending_press_button} solo ~{frames} frame(s)"

                        print(f"  #{pair_count:3d}  Btn {pending_press_button}+{btn}  "
                              f"gap: {gap_ms:5.1f}ms  "
                              f"(avg: {avg:.1f}ms  min: {mn:.1f}ms  max: {mx:.1f}ms)"
                              f"{pre_fire}")

                        pending_press_time = None
                        pending_press_button = None
                    else:
                        # Gap too large - previous pending was a stray
                        record_stray(pending_press_button, gap_ms, "no pair arrived")
                        pending_press_time = now
                        pending_press_button = btn

            elif event.type == pygame.JOYBUTTONUP:
                now = time.perf_counter()
                btn = event.button
                bs = get_button_state(btn)
                bs['held'] = False
                bs['last_release'] = now

                # If the released button is the pending press, it's a stray
                if pending_press_button is not None and btn == pending_press_button:
                    solo_ms = (now - pending_press_time) * 1000
                    record_stray(btn, solo_ms, "released before pair")
                    pending_press_time = None
                    pending_press_button = None

        time.sleep(0.0001)  # 0.1ms polling

except KeyboardInterrupt:
    print()
    print("=" * 65)
    print("  RESULTS")
    print("=" * 65)

    # --- Finger Gap Stats ---
    print()
    print("  --- FINGER GAP STATS ---")
    if len(gaps) == 0:
        print("  No simultaneous presses detected.")
    else:
        avg = sum(gaps) / len(gaps)
        mn = min(gaps)
        mx = max(gaps)
        gaps_sorted = sorted(gaps)
        median = gaps_sorted[len(gaps_sorted) // 2]

        buckets = [0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 50]
        counts = [0] * len(buckets)

        for g in gaps:
            for i in range(len(buckets) - 1):
                if g < buckets[i + 1]:
                    counts[i] += 1
                    break
            else:
                counts[-1] += 1

        print(f"  Total pairs:    {len(gaps)}")
        print(f"  Average gap:    {avg:.1f}ms")
        print(f"  Median gap:     {median:.1f}ms")
        print(f"  Fastest:        {mn:.1f}ms")
        print(f"  Slowest:        {mx:.1f}ms")
        print()
        print("  Distribution:")
        for i in range(len(buckets) - 1):
            if counts[i] > 0:
                bar = "#" * counts[i]
                pct = counts[i] / len(gaps) * 100
                print(f"    {buckets[i]:2d}-{buckets[i+1]:2d}ms: {counts[i]:3d} ({pct:4.1f}%) {bar}")

        print()
        recommended = max(3, math.ceil(avg) + 1)
        zero_count = sum(1 for g in gaps if g < 0.1)
        zero_pct = zero_count / len(gaps) * 100

        if zero_pct > 50:
            print(f"  *** OBD / MACRO DETECTED ***")
            print(f"  {zero_pct:.0f}% of presses had 0ms gap — likely OBD or a macro button.")
            print(f"  Turn off OBD to measure your natural finger gap.")
            print()

        print(f"  Recommended NOBD slider: {recommended}ms")
        print(f"  (based on your average gap of {avg:.1f}ms + 1ms headroom)")

    # --- Pre-fire Analysis ---
    if len(gaps) > 0:
        pre_fire_count = sum(1 for g in gaps if g >= USB_FRAME_MS)
        pre_fire_pct = pre_fire_count / len(gaps) * 100
        print()
        print("  --- PRE-FIRE ANALYSIS ---")
        print(f"  Pairs with pre-fire (gap >= {USB_FRAME_MS}ms):  "
              f"{pre_fire_count} / {len(gaps)}  ({pre_fire_pct:.1f}%)")
        if pre_fire_count > 0:
            pre_fire_gaps = [g for g in gaps if g >= USB_FRAME_MS]
            avg_pf = sum(pre_fire_gaps) / len(pre_fire_gaps)
            print(f"  Avg pre-fire duration: {avg_pf:.1f}ms")
        print(f"  (first button was solo for 1+ USB frames before second arrived)")

    # --- Stray Press Stats ---
    print()
    print("  --- STRAY PRESS STATS ---")
    if len(strays) == 0:
        print("  No stray presses detected. Clean inputs!")
    else:
        total_sequences = len(gaps) + len(strays)
        stray_pct = len(strays) / total_sequences * 100 if total_sequences > 0 else 0
        print(f"  Total strays:   {len(strays)}")
        print(f"  Stray rate:     {len(strays)} / {total_sequences} sequences = {stray_pct:.1f}%")

        # Per-button breakdown
        by_button = defaultdict(list)
        for btn, solo_ms, reason in strays:
            by_button[btn].append((solo_ms, reason))

        print()
        print("  Per-button breakdown:")
        for btn in sorted(by_button.keys()):
            entries = by_button[btn]
            avg_solo = sum(s for s, _ in entries) / len(entries)
            print(f"    Btn {btn}:  {len(entries)} stray(s)  (avg solo: {avg_solo:.1f}ms)")

        # By reason
        by_reason = defaultdict(int)
        for _, _, reason in strays:
            by_reason[reason] += 1

        print()
        print("  By reason:")
        for reason, count in sorted(by_reason.items()):
            print(f"    {reason}: {count}")

        # Solo durations
        solo_durations = [s for _, s, _ in strays]
        print()
        print(f"  Solo durations:  min: {min(solo_durations):.1f}ms  "
              f"max: {max(solo_durations):.1f}ms  "
              f"avg: {sum(solo_durations)/len(solo_durations):.1f}ms")

    # --- Bounce Detection ---
    print()
    print("  --- BOUNCE DETECTION ---")
    if len(bounces) == 0:
        print("  No bounces detected.")
    else:
        print(f"  Total bounces:  {len(bounces)}")
        by_btn = defaultdict(list)
        for btn, off_ms in bounces:
            by_btn[btn].append(off_ms)
        for btn in sorted(by_btn.keys()):
            offs = by_btn[btn]
            avg_off = sum(offs) / len(offs)
            print(f"    Btn {btn}: {len(offs)} bounce(s) (avg off-time: {avg_off:.1f}ms)")
        print()
        print("  NOTE: Bounces during release can cause strays in firmware.")
        print("        If bounce off-time < NOBD sync window, firmware filters it.")
        print("        If bounce persists >= sync window, firmware may commit it.")

    print()
    print("=" * 65)

pygame.quit()
