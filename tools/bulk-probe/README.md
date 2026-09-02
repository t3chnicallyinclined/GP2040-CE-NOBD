# Track B — Phase 0: the bulk poll-rate probe

**The one question Track B rests on:** can a PC pull data off USB Full-Speed *faster* than the
1 kHz interrupt-poll floor (~500 µs average)? If yes, we can drop input latency to ~30–125 µs by
streaming input over a **bulk** endpoint and tight-polling it from a companion app. This probe
answers that — cheaply, in isolation — before we build anything else.

- **Firmware** (`bulk_probe.uf2`): a standalone RP2040 that presents **one WinUSB bulk IN endpoint**
  and streams a 16-byte payload (sequence counter + device µs timestamp) as fast as the host drains
  it. Auto-binds WinUSB via an MS OS 2.0 descriptor — **no driver install**.
- **PC probe** (`probe.exe`): opens the device and tight-polls the bulk IN, reporting **reads/sec**
  (the effective poll rate), payloads/sec, dropped sequences, and the implied latency.

It's deliberately *not* wired into the gp2040-te stick — same RP2040 + USB-FS, so the rate it proves
is the rate the stick could hit. If Phase 0 says ~8–16k reads/sec, we integrate it for real (Phase 1+).

## Run it (2 minutes)

1. **Flash** a spare Pico (or any RP2040): BOOTSEL, drag **`bulk_probe.uf2`**. The LED blinks = alive.
   It appears in Device Manager as **"Track-B Bulk Probe"**.
   - **Driver binding:** on the test host the MS OS 2.0 auto-bind did *not* fire — the device sat
     at **Code 28** ("no driver"). Bind WinUSB manually with **[Zadig](https://zadig.akeo.ie)**:
     select *Track-B Bulk Probe* → target driver **WinUSB** → Install. *(Fixing the firmware
     auto-bind is a Phase-1 to-do.)* Zadig registers its **own** device-interface GUID, not the
     firmware's — so if `probe.exe` then says "Device not found," read the GUID under the device's
     *Device Parameters\DeviceInterfaceGUIDs* and set `GUID_DEV` in `probe.c` to match, then rebuild.
2. **Run** `probe.exe` (optionally `probe.exe 10` for a 10-second run):
   ```
   probe.exe
   ```
3. Read the result:
   ```
   reads (polls):   NNNNN  ->  NNNNN reads/sec   (THE poll rate)
   implied latency: ~NN us avg
   vs USB-FS interrupt floor: 1000 reads/sec, ~500 us avg.
   ```

## Reading the result

| reads/sec | verdict |
|---|---|
| **~8,000–16,000+** | ✅ Track B is real — go build Phase 1 (~60–125 µs vs the field's 500 µs–1 ms) |
| ~2,000–8,000 | 🟡 still a win, but the host is throttling — worth tuning (overlapped I/O, RAW_IO already on) |
| ~1,000 | ❌ no better than interrupt polling — Track B doesn't help on this host |

`drops` should be ~0 (the counter is contiguous). Non-zero means the host missed packets — a
reliability signal to note.

## Measured result — ✅ GO (2026-07-26, RP2040 Advanced Breakout Board, AMD / Win11)

```
reads (polls):  8,966 reads/sec    payloads: 35,865/sec    drops: 0    implied latency: ~56 us
```

**~9k reads/sec, zero drops** — decisively past the 1 kHz interrupt floor (~9× faster). Each read
batched ~4 payloads, so the wire has more headroom (a double-buffered reader should push higher).
This proves the **stick→host** hop only; the **host→game** half (a high-rate virtual HID + whether
the game reads it fresh) is Phase 1's to measure. Two bugs were fixed getting here: the probe now
opens the handle `FILE_FLAG_OVERLAPPED` (WinUSB rejects a sync handle) and uses overlapped reads.

## Build from source

**Firmware:**
```bash
export PICO_SDK_PATH=C:/Users/trist/projects/GP2040-CE/build/_deps/pico_sdk-src
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -Dpicotool_DIR=C:/Users/trist/projects/gp2040-te/build/_deps/picotool
cmake --build build            # -> build/bulk_probe.uf2
```

**PC probe** (Developer Command Prompt for VS):
```
cl probe.c /O2 /link winusb.lib setupapi.lib      # -> probe.exe
```

## What Phase 0 does NOT measure

It measures the **transport ceiling** (how fast the host can poll a bulk endpoint), not end-to-end
button→game latency. The streamed timestamp includes device-side FIFO dwell, so treat "implied
latency" as an order-of-magnitude sanity check, not a final number — that's Phase 3, on the real
integrated build. Phase 0 exists only to green-light or kill the approach.
