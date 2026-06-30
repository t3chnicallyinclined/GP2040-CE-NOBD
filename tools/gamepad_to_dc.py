"""
Xbox Gamepad → Dreamcast via UDP → Pico W → UART → GP2040-CE

Reads Xbox controller input, maps to DC buttons, sends MFP frames
over UDP at 60Hz. Plug in an Xbox controller and play on real DC.

Usage: python gamepad_to_dc.py [pico_ip]
Default IP: 192.168.1.46
"""

import pygame
import socket
import sys
import time

PICO_IP = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.46"
PICO_PORT = 4977

# DC button masks (active-LOW: 0 = pressed, 1 = released)
DC_BTN_A     = 0x0004
DC_BTN_B     = 0x0002
DC_BTN_X     = 0x0400
DC_BTN_Y     = 0x0200
DC_BTN_START = 0x0008
DC_BTN_UP    = 0x0010
DC_BTN_DOWN  = 0x0020
DC_BTN_LEFT  = 0x0040
DC_BTN_RIGHT = 0x0080
DC_BTN_C     = 0x0001  # L1/LB
DC_BTN_Z     = 0x0100  # R1/RB

pygame.init()
pygame.joystick.init()

if pygame.joystick.get_count() == 0:
    print("No gamepad found. Plug in an Xbox controller.")
    sys.exit(1)

joy = pygame.joystick.Joystick(0)
joy.init()
print(f"Gamepad: {joy.get_name()}")
print(f"Sending to {PICO_IP}:{PICO_PORT}")
print("Press Ctrl+C to stop\n")

# Xbox button indices (SDL mapping)
# 0=A, 1=B, 2=X, 3=Y, 4=LB, 5=RB, 6=Back, 7=Start, 8=LS, 9=RS
XBOX_TO_DC = {
    0: DC_BTN_A,      # A → DC A
    1: DC_BTN_B,      # B → DC B
    2: DC_BTN_X,      # X → DC X
    3: DC_BTN_Y,      # Y → DC Y
    4: DC_BTN_C,      # LB → DC C (L1)
    5: DC_BTN_Z,      # RB → DC Z (R1)
    7: DC_BTN_START,  # Start → DC Start
}

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

try:
    while True:
        pygame.event.pump()

        # Buttons
        buttons = 0xFFFF  # all released
        for xbox_btn, dc_mask in XBOX_TO_DC.items():
            if joy.get_button(xbox_btn):
                buttons &= ~dc_mask  # active-LOW: clear bit = pressed

        # D-pad (hat)
        if joy.get_numhats() > 0:
            hat_x, hat_y = joy.get_hat(0)
            if hat_x < 0: buttons &= ~DC_BTN_LEFT
            if hat_x > 0: buttons &= ~DC_BTN_RIGHT
            if hat_y > 0: buttons &= ~DC_BTN_UP
            if hat_y < 0: buttons &= ~DC_BTN_DOWN

        # Left stick as d-pad (deadzone 0.5)
        lx = joy.get_axis(0)
        ly = joy.get_axis(1)
        if lx < -0.5: buttons &= ~DC_BTN_LEFT
        if lx >  0.5: buttons &= ~DC_BTN_RIGHT
        if ly < -0.5: buttons &= ~DC_BTN_UP
        if ly >  0.5: buttons &= ~DC_BTN_DOWN

        # Triggers (axis 4=LT, 5=RT on most controllers, range -1 to 1)
        lt = 0
        rt = 0
        if joy.get_numaxes() > 4:
            lt_raw = joy.get_axis(4)
            rt_raw = joy.get_axis(5)
            lt = max(0, int((lt_raw + 1) * 127.5))  # -1..1 → 0..255
            rt = max(0, int((rt_raw + 1) * 127.5))

        # Right stick → DC analog (axis 2=RX, 3=RY)
        jx = 0x80  # center
        jy = 0x80
        if joy.get_numaxes() > 3:
            jx = int((joy.get_axis(2) + 1) * 127.5)
            jy = int((joy.get_axis(3) + 1) * 127.5)

        # Build 12-byte MFP frame
        frame = bytes([
            0, 0, 0, 0, 0, 0,           # filler
            buttons & 0xFF,              # buttons_lo
            (buttons >> 8) & 0xFF,       # buttons_hi
            rt & 0xFF,                   # right trigger
            lt & 0xFF,                   # left trigger
            jx & 0xFF,                   # analog X
            jy & 0xFF,                   # analog Y
        ])

        sock.sendto(frame, (PICO_IP, PICO_PORT))
        time.sleep(1/60)  # 60Hz

except KeyboardInterrupt:
    # Send released on exit
    released = bytes([0,0,0,0,0,0, 0xFF,0xFF, 0x00,0x00, 0x80,0x80])
    for _ in range(10):
        sock.sendto(released, (PICO_IP, PICO_PORT))
        time.sleep(0.016)
    print("\nStopped.")
finally:
    sock.close()
    pygame.quit()
