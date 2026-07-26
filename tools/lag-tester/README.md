# lag-tester — external controller-latency rig (RP2040 + MAX3421E)

An external, single-clock latency tester on a spare Pico + a MAX3421E USB Host FeatherWing.
It mirrors the [inputlag.science](https://inputlag.science/controller/methodology) method — a
microcontroller + a MAX3421E USB Host Shield that both **presses a button** and **hosts the
controller** — but times on the RP2040's **1 µs** clock instead of 1 ms histogram buckets.

**What it measures:** button edge → the first USB report that shows the press, on **one clock**
(no host/device offset), from **outside** the DUT (not the firmware measuring itself). This is
the third, independent instrument alongside the on-die probe and the USBPcap capture — and the
same chip inputlag.science uses, so a stock controller's number is directly comparable to their
database.

> Status: **compiles + links; hardware bring-up pending.** The USB glue is lifted from TinyUSB's
> proven RP2040↔MAX3421E BSP; expect a little iteration on the bench (pin/jumper checks, DUT
> enumeration). Report what the serial log shows and we tune from there.

## Bill of materials

| Role | Board |
|---|---|
| **Tester** | spare Pico (RP2040) + **MAX3421E USB Host FeatherWing** |
| **DUT A** | the gp2040-te stick (our firmware) |
| **DUT B** | the extra GP2040, **stock-flashed** (baseline control) |
| plus | jumper wires, a USB-serial adapter (or the spare Pico as a debugprobe) to read results |

## Wiring

**Common ground first** — tester GND ↔ FeatherWing GND ↔ DUT GND. The stimulus is referenced to it.

### Tester Pico ↔ MAX3421E FeatherWing (SPI0)
| Pico pin | → | FeatherWing |
|---|---|---|
| GP18 (SPI0 SCK) | → | SCK |
| GP19 (SPI0 TX)  | → | MOSI |
| GP16 (SPI0 RX)  | ← | MISO |
| GP17 (GPIO)     | → | CS |
| GP20 (GPIO IRQ) | ← | IRQ / INT |
| 3V3(OUT)        | → | 3V3 (logic) |
| VBUS (pin 40, 5V) | → | USB / 5V (powers the DUT through the host jack) |
| GND | ↔ | GND |

- The FeatherWing's **CS** and **IRQ** are just two signal pads — wire them to the Pico GPIOs above
  (check your FeatherWing's silk; adjust `MAX3421_CS_PIN` / `MAX3421_INTR_PIN` in `main.cpp` if it
  routes them elsewhere). The MAX3421E **RES** must be high to run; the Adafruit USB Host FeatherWing
  handles reset itself — if yours exposes RES and it isn't pulled up, tie it to 3V3.

### Tester Pico ↔ DUT
| Pico pin | → | DUT |
|---|---|---|
| GP14 (open-drain stimulus) | → | **B1 button input** (GPIO 8 on RP2040AdvancedBreakoutBoard) |
| GND | ↔ | GND |

- The DUT's **USB cable** plugs into the **FeatherWing's USB-A host jack**.
- We drive B1 = XInput **"A"** (report `buttons2` bit `0x10`). Pick a different button by changing
  `STIMULUS_PIN` (which DUT pin you clip to) and `MASK_A`/`RPT_BTN2` (which report bit to watch).

### Tester Pico → PC (results)
UART0 **TX = GP0** @ 115200 8N1 → a USB-serial adapter's RX (+ GND). Or flash the spare Pico as a
`debugprobe` and use its UART bridge. (USB-CDC output can't coexist with `tinyusb_host` in the SDK,
so the readout is UART for now.)

## Build

```bash
cd tools/lag-tester
export PICO_SDK_PATH=C:/Users/trist/projects/GP2040-CE/build/_deps/pico_sdk-src
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -Dpicotool_DIR=C:/Users/trist/projects/gp2040-te/build/_deps/picotool
cmake --build build --parallel
# -> build/lag_tester.uf2
```

**Tester board:** a plain Pico (RP2040) or a **Pico 2 W (RP2350)** both work — the MAX3421E is an
external SPI host, so the MCU is irrelevant, and the 40-pin layout is identical (same wiring). For
the Pico 2 W add `-DPICO_BOARD=pico2_w` (build into a separate dir, e.g. `build_pico2w`). Note the
Pico W / 2 W have **no GPIO onboard LED** (it's on the wireless chip), so there's no LED heartbeat
there — the serial log (`# DUT mounted`, histograms) is the liveness signal.

## Run

1. **Flash** the tester: BOOTSEL the spare Pico, drag `lag_tester.uf2`.
2. Open the serial terminal (115200). The onboard LED blinks = alive.
3. Plug the **DUT** into the FeatherWing jack. You should see `# DUT mounted`.
4. It auto-presses B1 every ~7–13 ms (de-synced from the 1 ms poll) and prints a histogram every
   1000 samples: `min / median / mean / p99 / max / stddev` + a 50 µs-bucket histogram.

## The experiment

- Run **DUT A (gp2040-te)**, sync **OFF**, XInput mode → record a few runs.
- Swap in **DUT B (stock GP2040)**, same wiring/button → record.
- **Head-to-head on the identical instrument.** Also sanity-check DUT B against inputlag.science's
  published GP2040 number to externally calibrate the whole rig.

## Tuning knobs (`main.cpp`)

`N_SAMPLES` (report interval) · `PRESS_TIMEOUT_US` · `NBUCKET`/`BUCKET_US` (histogram) ·
the `schedule_next_press()` cadence · SPI clock (`spi_init`, 4 MHz).
