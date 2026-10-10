# Input self-test: no switch, no wiring, no finger

The `selftest` image makes the firmware drive its **own** button pins on a fixed 1 s
schedule, so the edge IRQ, the sync window / debounce front stage, the ISR report build
and the USB path are exercised with the switches, the cable and the player all out of
the loop. A "press" is the pin pulled low by its own output driver, exactly what a closed
switch does; a "release" is the pin returned to input. The pin is never driven high, so a
switch that is physically connected can never be shorted.

The schedule is in [`src/input/selftest.cpp`](../../src/input/selftest.cpp) and mirrored
in `selftest.py`. It covers the exact failures nobd-25 had: a lone tap whose release waited
for the next input, a same-button double tap whose second press vanished, a 2 ms chord that
must land in one report, a 10 ms tap that must register, and a 3 ms tap that must be dropped
with sync on and present with sync off.

## Run it

```
build.bat selftest
```

Flash `build\GP2040-CE-NOBD_*_RP2040AdvancedBreakoutBoard.uf2` (hold BOOTSEL while plugging
in, copy onto RPI-RP2). The stimulus starts 3 s after boot and repeats forever. Then, with
the stick enumerated as an XInput pad:

```
python tools\selftest\selftest.py                 # sync on (default config), 15 s
python tools\selftest\selftest.py --sync off      # after turning sync off / debounce on in webconfig
python tools\selftest\selftest.py -v              # print every cycle
```

Exit 0 is a pass. Exit 1 prints which cycle failed and why (a sequence mismatch lists the
expected and observed mask sequences; a stuck hold names the mask and how long it stayed
down). Exit 2 means it saw too little data: capture longer, or the stimulus has not started.

The XInput mode needs nothing installed and resolves about 1 ms, enough for every pass/fail
check and for the tap-width proof. For the microsecond picture, capture passively with
USBPcap while the test runs and feed the export in:

```
tshark -r cap.pcap -Y "usb.data_len==20" -T fields -e frame.time_epoch -e usb.capdata > cap.txt
python tools\selftest\selftest.py --pcap cap.txt
```

That also reads the device-side edge stamps the measure build puts in the report, so the
stimulus width is printed from the RP2040's own clock.

## Locating a dead stage

`build.bat selftest diag` builds the same stimulus with the main loop polling every tick
(no idle-skip) and two counters on the right stick that the XInput API exposes: RX is the
doorbell edge-IRQ count, RY is the stimulus step count. The script prints both over the
capture. Steps not climbing means the stimulus never started; steps climbing with IRQs flat
means the edge interrupt is not seeing the pins; both climbing with no button transitions
means the report path dropped them. In this variant the report is loop-owned, so use the
plain `selftest` image for the shipping-path verdict.

## What it cannot see

The switch itself. Bounce, a worn contact or a bad crimp show up in play and not here, by
design: this isolates the firmware. Run this first; if it passes and play still feels wrong,
the problem is in the hardware in front of the pin.
