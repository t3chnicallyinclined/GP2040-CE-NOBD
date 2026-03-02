"""
NOBD Sync Window Tester
=======================
Tests whether simultaneous button presses arrive on the same gamepad poll.

Usage:
  1. Plug in your stick
  2. Run: python test_sync.py
  3. Press LP+HP (or any two buttons) repeatedly
  4. The script shows whether each pair landed on the SAME poll or DIFFERENT polls
  5. Compare results with sync window ON (~8ms) vs OFF (0ms)

Press Ctrl+C to stop and see summary stats.
"""

import pygame
import time
import sys

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
print("=" * 60)
print("  NOBD SYNC WINDOW TESTER")
print("=" * 60)
print()
print("Press two buttons at the same time (like LP+HP for a dash).")
print("The script will tell you if they arrived together or apart.")
print()
print("Press Ctrl+C to stop and see stats.")
print("-" * 60)

# Track button states
prev_buttons = [False] * joy.get_numbuttons()
pending_press = None  # (button_index, timestamp)
PAIR_WINDOW = 0.050   # 50ms window to detect "intended" simultaneous presses

# Stats
same_poll = 0
diff_poll = 0
diff_deltas = []

try:
    while True:
        pygame.event.pump()

        # Read current button states
        curr_buttons = [joy.get_button(i) for i in range(joy.get_numbuttons())]

        # Find newly pressed buttons this poll
        just_pressed = []
        for i in range(joy.get_numbuttons()):
            if curr_buttons[i] and not prev_buttons[i]:
                just_pressed.append(i)

        now = time.perf_counter()

        if len(just_pressed) >= 2:
            # Two or more buttons arrived on the SAME poll
            same_poll += 1
            btns = "+".join(f"B{b}" for b in just_pressed)
            print(f"  SAME POLL  | {btns} arrived together (0.000ms apart) "
                  f"[sync: {same_poll}, split: {diff_poll}]")
            pending_press = None

        elif len(just_pressed) == 1:
            if pending_press is not None:
                delta = now - pending_press[1]
                if delta < PAIR_WINDOW:
                    # Two buttons arrived on DIFFERENT polls but close together
                    diff_poll += 1
                    delta_ms = delta * 1000
                    diff_deltas.append(delta_ms)
                    print(f"  DIFF POLL  | B{pending_press[0]} then B{just_pressed[0]} "
                          f"({delta_ms:.1f}ms apart) "
                          f"[sync: {same_poll}, split: {diff_poll}]")
                    pending_press = None
                else:
                    # Previous press was a solo press, start new pending
                    pending_press = (just_pressed[0], now)
            else:
                pending_press = (just_pressed[0], now)

        # Expire old pending press
        if pending_press and (now - pending_press[1]) > PAIR_WINDOW:
            pending_press = None

        prev_buttons = curr_buttons
        time.sleep(0.0005)  # 0.5ms poll — faster than the stick's 1ms USB poll

except KeyboardInterrupt:
    print()
    print("=" * 60)
    print("  RESULTS")
    print("=" * 60)
    total = same_poll + diff_poll
    if total > 0:
        pct = (same_poll / total) * 100
        print(f"  Same poll (synced):    {same_poll:4d}  ({pct:.0f}%)")
        print(f"  Different polls:       {diff_poll:4d}  ({100-pct:.0f}%)")
        print(f"  Total pairs detected:  {total:4d}")
        if diff_deltas:
            avg = sum(diff_deltas) / len(diff_deltas)
            print(f"  Avg split delta:       {avg:.1f}ms")
            print(f"  Max split delta:       {max(diff_deltas):.1f}ms")
        print()
        if pct >= 90:
            print("  Sync window is WORKING GREAT!")
        elif pct >= 70:
            print("  Sync window is helping. Try increasing the slider.")
        else:
            print("  Many splits detected. Check your sync window setting.")
    else:
        print("  No button pairs detected. Press two buttons at once!")
    print("=" * 60)

pygame.quit()
