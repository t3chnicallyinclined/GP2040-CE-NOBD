# GP2040-CE V2 Ultimate — The Fastest Fighting Game PCB Ever Made

> ## ⚠️ MCU MIGRATION NOTE — 2026-06-05
>
> This document originally specified **STM32F730R8T6 (LQFP-64, 10×10mm)** as the USB HS coprocessor.
>
> We discovered late in schematic review that the **R8 (LQFP-64) does NOT have the USB HS PHY bonded out** — only LQFP-144 (Z8) and UFBGA-176 (I8) variants of STM32F730 do. Per Table 2 of [DS12536 datasheet](https://www.st.com/resource/en/datasheet/stm32f730i8.pdf), the R8/V8 packages show `USB OTG PHY HS controller (USBPHYC) = No`. The R8 is capped at USB Full-Speed (12 Mbps, 1000Hz polling), not the 8000Hz this doc claims throughout.
>
> **MCU swapped to STM32F730Z8T6 (LQFP-144, 20×20mm)** — smallest F730 variant with HS PHY bonded out. Same firmware, same family. The 8000Hz claim is now actually achievable.
>
> **Stale in this doc until comprehensively revised:**
> - All STM32 pin numbers (LQFP-64 → LQFP-144 pin map)
> - STM32 BOM line (part number, package, cost, LCSC code)
> - Power rail count (Z8 adds VDD12OTGHS, VCAP_2, VDDUSB, VDDSDMMC, VREF+ pins)
> - "75% smaller than F723" rationale (Z8 is now F723-sized)
> - "Board smaller than credit card" claim (board area unchanged but MCU footprint grows; Z8 going on bottom side to preserve compact form factor)
> - Crystal must be specified ±20 ppm or better (USB HS bus compliance)
>
> See [NOBD-ZERO-V1-FREELANCER-MCU-SWAP-060526.md](NOBD-ZERO-V1-FREELANCER-MCU-SWAP-060526.md) for the change request to the freelancer.

---

**A beginner-friendly, fully-explained design specification.**

You're building a PCB (Printed Circuit Board) that goes inside an arcade stick enclosure. When you're done, this board will:

- Read your button presses in **0.125 milliseconds** (8000Hz USB polling — 8× faster than most controllers)
- Talk directly to a **Dreamcast** with no adapter (Maple Bus protocol via PIO)
- Play **online versus matches** over Ethernet (hardware TCP/UDP, ~12 microsecond latency)
- Work on **PS4, Xbox, Switch, PC** (USB HID with authentication support)
- Be configurable via a **web browser** (GP2040-CE web config)

And it does all this with just **~55 components** on a board smaller than a credit card (90×45mm).

---

## Table of Contents

1. [How This Board Works (The Big Picture)](#1-how-this-board-works)
2. [The Two Brains — Why We Need Two Chips](#2-the-two-brains)
3. [Complete Parts List with Explanations](#3-complete-parts-list)
4. [Power System — How Everything Gets Electricity](#4-power-system)
5. [USB — Talking to Consoles and PCs](#5-usb)
6. [Dreamcast Connection — Maple Bus](#6-dreamcast-maple-bus)
7. [Ethernet — Online Play](#7-ethernet)
8. [Buttons — Reading Your Inputs](#8-buttons)
9. [Display — OLED Screen](#9-display)
10. [Pin Map — Every Wire Explained](#10-pin-map)
11. [Board Layout](#11-board-layout)
12. [EasyEDA Build Guide](#12-easyeda-build-guide)
13. [Design Decisions — Why We Chose Each Part](#13-design-decisions)

---

## 1. How This Board Works

Here's the flow of what happens when you press a button:

```
YOUR FINGER
    │
    ▼
[Button Press] ──→ GPIO pin goes LOW (0V)
    │
    ├──→ RP2040 reads it ──→ Sends to Dreamcast via Maple Bus
    │                    ──→ Sends to remote player via Ethernet
    │
    └──→ STM32F730 reads it ──→ Sends USB report to console/PC at 8000Hz
```

Both chips read the same button at the same time. The RP2040 handles retro consoles and online play. The STM32 handles modern USB gaming. They work in parallel — no waiting, no bottleneck.

---

## 2. The Two Brains

### Why can't one chip do everything?

| Feature | RP2040 | STM32F730 |
|---------|--------|-----------|
| **PIO** (Programmable I/O) | Yes — 8 state machines that can emulate any wire protocol | No — has timers but can't bit-bang Maple Bus reliably |
| **USB High Speed** (480 Mbps, 8000Hz) | No — only USB Full Speed (12 Mbps, max 1000Hz) | Yes — has a built-in USB HS radio (called a "PHY") |
| **GPIO speed** | ~8 nanoseconds per read | ~14 nanoseconds per read |
| **Flash memory** | External 16MB chip | Internal 64KB (small but enough) |

**PIO** is the RP2040's secret weapon. It has 8 tiny processors on the chip that do nothing but wiggle pins in precise patterns. This is how we speak the Dreamcast's Maple Bus protocol — a weird, proprietary wire format that no other chip can handle as cleanly. The PIO programs run independently of the CPU, so Maple Bus communication happens at hardware speed (~1-2 microseconds to respond to the Dreamcast).

**USB HS PHY** is the STM32's secret weapon. "PHY" means Physical Layer — it's the analog radio that converts digital data into electrical signals on the USB cable. USB High Speed runs at 480 Megabits per second, which allows polling the controller every 0.125 milliseconds (8000Hz). The RP2040's USB only runs at 12 Mbps (Full Speed), which limits it to 1000Hz. No software trick can fix this — it's a hardware limitation.

### How they communicate

The two chips talk via **SPI** (Serial Peripheral Interface) — a simple 4-wire bus:

```
RP2040 (master) ──SCK──→ STM32 (slave)     Clock signal ("tick tock")
                ──MOSI──→                    Data: RP2040 → STM32
                ←──MISO──                    Data: STM32 → RP2040
                ──CS────→                    "I'm talking to you"
```

The RP2040 sends gamepad state to the STM32, which packages it into USB reports. The RP2040 is the "brain" — it runs GP2040-CE firmware, handles configuration, manages VMU saves, and coordinates everything. The STM32 is the "mouth" — it just speaks USB HS really fast.

---

## 3. Complete Parts List

### The Main Chips (4 ICs)

#### U1: RP2040 — The Main Brain
- **What:** Dual-core ARM Cortex-M0+ microcontroller, 133MHz
- **Package:** QFN-56 (7×7mm) — a square chip with 56 tiny pads on the bottom (no legs)
- **Why this chip:** Only chip with PIO for Maple Bus. Runs GP2040-CE. Huge community.
- **Cost:** ~$0.70
- **JLCPCB:** C2040

#### U2: W25Q128JVSIQ — The Memory
- **What:** 16 Megabytes of flash memory (like a tiny SSD)
- **Package:** SOIC-8 (5×4mm) — a small chip with 8 legs, easy to solder
- **Why this chip:** Stores the GP2040-CE firmware, web config files, and VMU save data. 16MB is huge — firmware uses ~2MB, leaving 14MB for saves.
- **Cost:** ~$0.45
- **JLCPCB:** C97521
- **How it connects:** QSPI (Quad SPI) — 4 data lines instead of 1, so it's 4× faster. The RP2040 boots directly from this chip.

#### U3: STM32F730R8T6 — The USB Speed Demon
- **What:** ARM Cortex-M7 microcontroller, 216MHz, with built-in USB High Speed PHY
- **Package:** LQFP-64 (10×10mm) — a square chip with 64 visible legs on all 4 sides (easier to inspect solder joints than QFN)
- **Why this chip:** The ONLY affordable chip with an internal USB HS PHY. No external USB radio needed. Other STM32s (like the H7 series) need a separate $3 ULPI PHY chip + 12 extra traces.
- **Flash:** 64KB internal — enough for a lean USB HID gamepad firmware
- **Cost:** ~$3.83
- **JLCPCB:** C478453 (extended part)

#### U4: W5500 — The Network Card
- **What:** Hardware TCP/UDP Ethernet controller with 8 sockets
- **Package:** LQFP-48 (7×7mm, 0.5mm pitch)
- **Why this chip:** Has a complete network stack built into silicon. The RP2040 doesn't need to run any networking code — it just writes "send this UDP packet" to a register and the W5500 handles everything (IP headers, checksums, ARP, etc). Response time: ~12 microseconds from "packet arrived" to "data in RP2040 memory."
- **Why not WiFi?** WiFi adds 2-10ms of unpredictable latency (buffering, retransmits, interference). Ethernet is deterministic — every packet takes the same time. For fighting games where 1 frame = 16.67ms, WiFi jitter is unacceptable.
- **Cost:** ~$1.50
- **JLCPCB:** C32843 (basic part)

#### U5: FE1.1S — USB 2.0 High-Speed Hub Controller
- **What:** Internal USB 2.0 hub that lets one external USB-C connector talk to both the STM32 (USB HS, 8000Hz HID) and the RP2040 (USB FS, config + UF2) simultaneously, plus an end-user USB-A accessory port.
- **Why this chip:** Confirmed to support USB 2.0 High Speed (480 Mbps) on **all** ports (upstream + 4 downstream). This is critical — the GL850G and other common hubs only support HS upstream with FS/LS downstream, which would kill the 8000Hz feature.
- **Package:** SSOP-28 (5×5mm), single 3.3V supply.
- **Source:** Verified on JLCPCB as **C9359** (FE1.1S-BSOP28BCN, basic part, ~$0.34).
- **Crystal:** Needs an external 12 MHz crystal + 2× ~22pF load caps.
- **Connections:**
  - Upstream port → USB-C J1 D+/D- (controlled-impedance 90Ω differential pair)
  - Downstream port 1 → STM32 USB HS (OTG_HS) D+/D- on pins PB14/PB15 (controlled-impedance 90Ω differential pair)
  - Downstream port 2 → RP2040 USB FS D+/D-
  - Downstream ports 3, 4 → unused (no termination needed)

#### J2: USB-A Receptacle (PS5 Passthrough Host Port)
- **What:** Standard USB-A receptacle on the board edge, connected directly to the **STM32's OTG_FS** USB controller (PA11=D-, PA12=D+, pins 44 and 45 on the LQFP-64).
- **Why STM32 OTG_FS and not the hub:** The STM32F730R8T6 has **two completely independent USB controllers** — OTG_HS (internal HS PHY, used for 8000Hz HID device to the console via the FE1.1S hub) and OTG_FS (internal FS PHY, free for any role). Wiring the USB-A directly to STM32 OTG_FS lets the STM32 act as a USB **host** to a PS4/PS5 authentication dongle, while OTG_HS continues serving the console as a USB **device**. Both peripherals run simultaneously. This is the cleanest way to add PS5 passthrough without consuming any RP2040 GPIOs or PIO state machines.
- **Why USB-A and not USB-C:** The PS5 passthrough dongle market (Brook etc.) is overwhelmingly USB-A. Match what users actually plug in.
- **Firmware role:** STM32 firmware acts as a USB host (via TinyUSB or similar), enumerates the plugged-in dongle, proxies authentication challenges/responses between the console (via OTG_HS) and the dongle (via OTG_FS). The console sees the STM32 as an authenticated PS5 controller.
- **Power protection:** Includes a USBLC6-2SC6 ESD chip and a 500mA polyfuse on its VBUS line. VBUS rail is provided by the on-board 5V supply (from the polyfused USB-C input).
- **Reference:** [TinyUSB STM32 OTG_FS host mode](https://github.com/hathach/tinyusb) — standard library, well-supported.
- **Cost:** ~$0.50 added per board (USB-A receptacle + ESD chip + polyfuse).
- **Firmware status note:** STM32 main firmware does not include the PS auth proxy at V1 release; this feature can be added in a subsequent firmware update (delivered via the single-file atomic firmware update mechanism). The hardware is ready; firmware will follow.

#### U6: PCA9536DR — 4-Bit I²C I/O Expander (atomic firmware update enabler)
- **What:** A 4-bit I²C-controlled I/O expander used by the RP2040 to control the STM32's BOOT0 and NRST pins for in-system firmware updates.
- **Why this chip:** Allows a single .uf2 file dragged to the RP2040 to atomically update both chips. The RP2040 includes the STM32 firmware as an embedded payload; after flashing itself, it sets the PCA9536 outputs to put the STM32 into SPI bootloader mode and writes the new STM32 firmware via the existing SPI1 bus.
- **Package:** SOIC-8.
- **I²C address:** Fixed 0x41 — no conflict with the OLED on the same bus (typically 0x3C).
- **Source:** TI PCA9536DR — verify JLCPCB stock at quote time; functionally equivalent parts (e.g., MAX7321/MAX7322 alternatives) acceptable if PCA9536 is short-stocked, as long as I²C-controlled outputs are provided.
- **Pin assignments:**
  - P0 (output) → STM32 BOOT0 (pin 60) — drives HIGH for bootloader entry
  - P1 (output) → STM32 NRST (with diode-OR vs SW1 RESET so manual reset still works)
  - P2, P3 → reserved for future use
  - SDA / SCL → existing I²C bus shared with OLED (RP2040 GPIO 0, GPIO 1)

---

### Crystals (2-3 tiny rocks)

A crystal is literally a piece of quartz cut to vibrate at an exact frequency when voltage is applied. Think of it as a tuning fork for electronics — it provides the heartbeat that keeps everything synchronized.

#### Y1: 12MHz Crystal (for RP2040)
- **What:** Vibrates 12 million times per second
- **Why 12MHz?** The RP2040's internal PLL (Phase-Locked Loop) multiplies this up: 12MHz × 10.4 = 125MHz (the default CPU speed). The PLL is like a gear ratio — small input, big output.
- **Package:** 3225 (3.2×2.5mm) — tiny 4-pad SMD
- **Needs:** 2× load capacitors (15pF each) — calculated for CL~10pF crystal + ~3pF stray capacitance

#### Y2: 25MHz Crystal (for W5500)
- **What:** Vibrates 25 million times per second
- **Why 25MHz?** Ethernet runs at 25MHz internally (the MII clock). The W5500 needs this exact frequency.
- **Needs:** 2× load capacitors (18pF each) — per W5500 application note and crystal CL spec

#### Y3: 8MHz Crystal (for STM32F730) — OPTIONAL
- **What:** The STM32's external clock reference
- **Why optional?** We COULD share the RP2040's 12MHz crystal, but the validation found a startup race condition — the RP2040 takes 1-5ms to stabilize its crystal, and the STM32 would see garbage during that time. A dedicated 8MHz crystal ($0.10!) eliminates this risk entirely.
- **Why 8MHz?** The STM32's PLL multiplies: 8MHz × 27 = 216MHz CPU, and 8MHz × 6 = 48MHz USB clock. Clean integer math = exact frequencies.
- **Needs:** 2× load capacitors (20pF each)

**Decision: Include Y3.** $0.10 + 2 caps is cheap insurance against a subtle startup bug that would be maddening to debug.

---

### Power Components

#### U5: AP2112K-3.3TRG1 — Voltage Regulator
- **What:** Converts 5V (from USB) to 3.3V (what the chips need)
- **Package:** SOT-23-5 (2.9×1.6mm) — 5 tiny legs, one of the smallest standard packages
- **Max current:** 600mA — our board draws ~300mA typical, ~400mA peak, so we have plenty of headroom
- **Dropout:** 250mV — means it needs at least 3.55V input to output a stable 3.3V. With 5V USB power (minus 0.3V Schottky drop = 4.7V input), we have 1.4V of headroom.
- **Why not AMS1117?** The AMS1117 works but it's in a SOT-223 package (6.5×3.6mm) — 7× larger than the AP2112K. Same job, less board space.

#### D1, D2: B5819W — Power Protection Diodes
- **What:** Schottky diodes on each USB-C port's 5V line
- **Why CRITICAL:** The board has TWO USB-C ports. If both are plugged into different computers, without these diodes, Computer A's 5V power would flow backwards into Computer B's USB port. This can **damage USB host controllers** — potentially killing a $300 console's motherboard.
- **How they work:** A Schottky diode is a one-way valve for electricity. Current flows IN but can't flow back OUT. Each diode allows 5V from its USB-C port to reach the regulator, but blocks current from flowing back out to the other port.

```
USB-C Port 1 (5V) ──→|D1|──→─┐
                               ├──→ 4.7V bus ──→ AP2112K ──→ 3.3V
USB-C Port 2 (5V) ──→|D2|──→─┘
```

- **Voltage drop:** ~0.3V (so 5V becomes 4.7V — still plenty for the regulator)
- **Package:** SOD-123 (2.7×1.6mm)
- **Cost:** ~$0.02 each

#### Capacitors (the boring-but-essential parts)

Every chip needs **decoupling capacitors** right next to its power pins. Here's why:

When a chip switches millions of transistors, it creates tiny current spikes that last nanoseconds. These spikes travel along the power traces and create voltage dips ("noise") that can make other chips malfunction. A decoupling capacitor is like a tiny battery that absorbs these spikes locally, so the noise doesn't spread.

**Rule of thumb:** Every power pin gets a 100nF (0.1µF) ceramic capacitor within 3mm of the pin. Larger chips also get a bulk capacitor (4.7-10µF) nearby.

| Chip | 100nF caps | Bulk caps | Total |
|------|-----------|-----------|-------|
| RP2040 | 10 (6× IOVDD, 2× DVDD, 1× USB, 1× VREG) | 2× 10µF | 12 |
| STM32F730 | 7 (4× VDD, 1× VDDA, 1× VDDUSB, 1× NRST) | 1× 4.7µF + 2× 1µF | 10 |
| W5500 | 4 (VDD pins) + 1 (AVDD) | 1× 10µF | 6 |
| AP2112K | 0 (integrated) | 1× 10µF in, 1× 10µF out | 2 |
| Crystal load caps | 6 (2 per crystal × 3 crystals) | 0 | 6 |
| **Total capacitors** | | | **~36** |

Don't worry about memorizing this — EasyEDA will show you exactly where each one goes.

---

### Connectors

#### J1: USB-C Receptacle (×1, single port architecture)
- **What:** A single USB-C connector that feeds an internal USB 2.0 High-Speed hub (FE1.1S), which then splits to both the STM32 and the RP2040. The host PC/console sees both chips simultaneously over one cable.
- **CC pins:** 2× 5.1kΩ pull-down resistors on CC1/CC2 (device-mode advertisement)
- **Why one port:** Cleanest user experience. Same cable for 8000Hz tournament play (via STM32 USB HS), web configuration (via RP2040), and atomic firmware updates (drag one .uf2 file, both chips updated). No more "which port do I plug in?" confusion.
- **Cost:** ~$0.15

#### J3: RJ45 Retro Jack (no magnetics)
- **What:** Standard Ethernet-style jack, but used for Dreamcast/Saturn/retro console cables
- **Why no magnetics?** Ethernet needs magnetic isolation transformers inside the jack. Retro console signals are direct GPIO — magnetics would block them.
- **Pinout:** Directly connects to RP2040 GPIOs (through protection resistors)

#### J4: RJ45 Ethernet Jack (WITH magnetics)
- **What:** Standard Ethernet jack with built-in signal transformers
- **Why magnetics?** Ethernet requires galvanic isolation between the board and the cable. The transformers inside the jack handle this. Without them, ground loops could damage equipment or cause data errors.
- **Connects to:** W5500 TX+/TX-/RX+/RX- differential pairs

#### J5: 20-Pin Brook Header
- **What:** 2×10 pin header (2.54mm pitch) that matches the Brook UFB pinout
- **Why:** Lets you use existing arcade stick wiring harnesses designed for Brook boards. Plug-and-play compatibility with most commercial arcade sticks.

#### J6: 4-Pin OLED Header (JST-PH)
- **What:** I2C connection for a small OLED display
- **Shows:** Button inputs, mode indicator, VMU status, diagnostics

#### SW1: Reset Button
- **What:** Resets both chips simultaneously (shared NRST line)
- **Footprint:** B3U-1000P SMD tactile, JLCPCB C843670

#### SW2: BootSel Button
- **What:** Hold during power-on to enter firmware update mode
- **For RP2040:** Enters UF2 bootloader (drag-and-drop firmware update)
- **Connection:** RP2040 QSPI_SS pin (doubles as BOOTSEL function)
- **Footprint:** B3U-1000P SMD tactile, JLCPCB C843670

#### SW3: Web Config Button
- **What:** Dedicated button for entering GP2040-CE web config mode
- **Connection:** Wired in **parallel with the S2 (Select) button signal** — no additional GPIO required. Pressing SW3 = pressing S2.
- **How to use:** Hold during USB plug-in (or hold during boot) to enter web config mode. GP2040-CE firmware reads S2-held-at-boot as the trigger for web config.
- **Why:** Convenience — end users can enter web config without needing to press the fight stick S2 button, which may be inaccessible inside an enclosure.
- **Footprint:** B3U-1000P SMD tactile, JLCPCB C843670
- **Reference:** matches GP2040-CE V5.6 Advanced Breakout Board SW3 implementation.

---

### Screw Terminal Blocks (Solderless Wiring Option)

The V2 PCB includes screw terminal blocks for solderless wiring. End users can use either the Brook 20-pin header OR the screw terminals to wire their fight stick.

**All terminal blocks: 3.81mm pitch, through-hole, black — DIBO DB125-3.81-2P-BK-S (JLCPCB C430618).**

| Position | Quantity | Function |
|----------|----------|----------|
| Top edge | 11× 2-position blocks (= 22 signal positions) | Positions 1-20 wired in parallel with the Brook 20-pin header. Positions 21-22 dedicated to L3/R3 (which are not on the Brook 20-pin). The Brook header pinout already includes 5V and GND, so accessory power for LED strips, etc. is accessible directly through the corresponding screw terminal positions. |

**Total:** 11× DIBO DB125-3.81-2P-BK-S placed side-by-side along the top edge. Cost: ~$2.04.

V5.6 had additional "optional pins" / toggle switch / L3-R3 redundancy terminal blocks that required spare GPIOs to be useful. V2 has all 30 RP2040 GPIOs allocated to functional pins, so those V5.6 extras don't apply. SOCD / DP-mode toggling is handled by GP2040-CE firmware hotkeys rather than physical switches on V2.

---

### Protection Components

#### U6, U7: USBLC6-2SC6 — USB ESD Protection (×2)
- **What:** Protects USB data lines from static electricity
- **Why:** When you plug/unplug a USB cable, static discharge can be thousands of volts for a few nanoseconds. This chip clamps the voltage to safe levels.
- **One per USB-C port**
- **Package:** SOT-23-6
- **Cost:** ~$0.08 each

#### F1, F2: Polyfuses (×2)
- **What:** Resettable fuses on each USB-C VBUS line
- **Why:** If something shorts, the polyfuse heats up and increases resistance, cutting current. When the short is removed, it cools down and resets. No replacement needed.
- **Rating:** 500mA (matches USB spec)
- **Package:** 0805

#### R_RETRO1-4: 1kΩ Series Resistors (×4)
- **What:** Current-limiting resistors on the RJ45 Retro signal lines. Two for Maple Bus Port A (SDCKA on GPIO 23, SDCKB on GPIO 24). Two for the additional retro expansion lines (GPIO 4, GPIO 5)
- **Why:** The Dreamcast outputs 5V signals. The RP2040 can only handle 3.3V. These resistors limit the current that flows through the RP2040's internal protection diodes when 5V is present, keeping it within safe limits (~1.7mA vs the ~2mA max).
- **Without them:** The RP2040 works fine (many boards skip this), but the excess voltage slowly degrades the chip over months/years. The $0.01 resistors make it safe forever.

---

### Resistors Summary

| Ref | Value | Qty | Purpose |
|-----|-------|-----|---------|
| R_CC1-4 | 5.1kΩ | 4 | USB-C CC identification (2 per port) |
| R_SDA, R_SCL | 4.7kΩ | 2 | I2C bus pull-ups for OLED display |
| R_RETRO1-4 | 1kΩ | 4 | RJ45 Retro 5V protection (Maple Port A + retro expansion lines) |
| R_NRST | 10kΩ | 1 | STM32 reset pin pull-up (keeps chip running, reset button pulls LOW) |
| R_BOOT0 | 10kΩ | 1 | STM32 BOOT0 pull-down (boots from flash, not bootloader) |
| R_ETHRST | 10kΩ | 1 | W5500 reset pin pull-up |
| R_SCK | 33Ω | 1 | SPI clock series termination (reduces signal reflections at high speed) |
| R_LED | 330Ω | 1 | Status LED current limiter |
| **Total** | | **15** | |

---

## 4. Power System

```
                    ┌──────────┐
USB-C #1 (5V) ──→ │ Schottky │──→─┐
                    │ D1 (0.3V │    │
                    │  drop)   │    │    ┌─────────────┐
                    └──────────┘    ├──→ │  AP2112K    │──→ 3.3V ──→ All chips
                    ┌──────────┐    │    │  (5V→3.3V)  │
USB-C #2 (5V) ──→ │ Schottky │──→─┘    └─────────────┘
                    │ D2 (0.3V │
                    │  drop)   │         Also: 4.7V bus powers
                    └──────────┘              RP2040 internal 1.1V regulator
                                              (RP2040 handles this itself)
```

**Current budget:**

| Consumer | Typical Draw | What It's Doing |
|----------|-------------|-----------------|
| RP2040 | 40mA | Running firmware, PIO Maple Bus, SPI master |
| STM32F730 | 80mA | USB HS PHY active, reading buttons at 8000Hz |
| W5500 | 132mA | Ethernet link active, processing packets |
| W25Q128 flash | 15mA | Serving firmware pages to RP2040 |
| FE1.1S USB hub | 30mA | Routing USB packets to STM32 + RP2040 |
| PCA9536 + LEDs + pull-ups | 16mA | I/O expander idle + misc |
| **Total** | **~313mA** | AP2112K provides 600mA — plenty of headroom |

---

## 5. USB

### Single USB-C Architecture (V1 — uses FE1.1S internal hub)

```
USB-C (J1) ─→ [USBLC6-2SC6 ESD] ─→ [FE1.1S USB 2.0 HS Hub]
                                          ├──→ STM32F730 USB HS (8000Hz HID, 480Mbps)
                                          ├──→ RP2040 USB FS (config, UF2, 1000Hz)
                                          └──→ (2 unused downstream ports)
```

The host (PC/console) sees both chips simultaneously on one cable. You can be in a tournament at 8000Hz **and** open the GP2040-CE web config in a browser at the same time, all through one USB-C cable.

### STM32F730 USB HS Function (8000Hz gaming)
- **Speed:** 480 Mbps (USB 2.0 High Speed)
- **Polling rate:** 8000Hz (0.125ms between reports)
- **Protocol:** USB HID gamepad
- **Compatibility:** PS4, Xbox, Switch, PC
- The USB HS PHY is built into the STM32F730 silicon. D+ and D- traces go to FE1.1S downstream port 1 as a 90Ω differential pair (controlled impedance, <30mm length).

### RP2040 USB FS Function (config + UF2 + retro modes)
- **Speed:** 12 Mbps (Full Speed)
- **Purpose:** Web configuration, UF2 firmware drag-and-drop (atomic update of both chips), RP2040-native USB modes (Xinput, DirectInput, PS4, Switch, etc), Dreamcast Maple Bus mode, retro console modes
- **Polling rate:** up to 1000Hz on RP2040-mode gameplay
- D+/D- traces go to FE1.1S downstream port 2.

### Atomic Firmware Update Flow (single .uf2 file → both chips)
1. User holds SW2 (BOOTSEL), plugs USB-C
2. RP2040 enters UF2 mode via ROM bootloader (mass storage device visible to host)
3. User drags `nobd-zero.uf2` (single file containing RP2040 firmware + embedded STM32 binary)
4. RP2040 writes its own flash, resets, boots into the new firmware
5. RP2040 firmware reads embedded STM32 binary version, compares against running STM32 firmware via SPI handshake
6. If versions differ, RP2040 re-flashes the STM32 via its SPI ROM bootloader:
   - Sets PCA9536 P0 HIGH (drives STM32 BOOT0 HIGH for bootloader mode)
   - Pulses PCA9536 P1 LOW then HIGH (resets STM32 via NRST)
   - STM32 now in SPI1 ROM bootloader mode (per ST AN4286)
   - RP2040 writes new STM32 firmware over existing SPI1 bus
   - Sets PCA9536 P0 LOW, pulses NRST again — STM32 boots into new application firmware
7. Both chips up to date. One drag-drop. One cable. Done.

### Why a USB Hub (vs the dual-port plan)?

Earlier V0/V1 drafts of the V2 design used two separate USB-C connectors (one per MCU). This worked but had real UX cost: users had to remember "the gaming port" vs "the config port," and firmware updates required two separate flashing operations (one drag-drop per port). For NOBD-ZERO V1 we switched to a single USB-C connector behind an internal FE1.1S USB HS hub. Cost is roughly $0.42 net per board (hub chip + I/O expander minus the second USB-C and second ESD chip). The UX gain is enormous: one cable, atomic single-file firmware updates, both chips always visible to the host.

We chose **FE1.1S** specifically because it supports USB 2.0 High Speed (480 Mbps) on **all** downstream ports. Many cheaper USB hub controllers (GL850G, CH334F, CH335F) downgrade downstream ports to USB Full Speed, which would destroy the 8000Hz polling feature on the STM32. FE1.1S preserves full HS throughput end-to-end.

---

## 6. Dreamcast Maple Bus

The Dreamcast uses a proprietary protocol called **Maple Bus** to talk to controllers. It's a two-wire, half-duplex, bit-banged serial protocol running at ~2 Mbps.

### How it works on our board

```
Dreamcast Console                          Our Board
┌─────────────┐                          ┌─────────────┐
│ Port A      │     5V data lines        │ RP2040      │
│  SDCKA ─────┼──── 1kΩ ───────────────→ │ GPIO 23     │ PIO0 State Machine
│  SDCKB ─────┼──── 1kΩ ───────────────→ │ GPIO 24     │ (handles all timing
│  +5V ───────┼──→ power rail            │             │  in hardware)
│  GND ───────┼──→ ground               │             │
│  SENSE ─────┼──→ tied to GND           │             │
│             │   (tells DC: "I exist")  │             │
└─────────────┘                          └─────────────┘
```

**The 1kΩ resistors** sit between the Dreamcast's 5V signals and the RP2040's 3.3V GPIO pins. When the Dreamcast drives HIGH (5V), the resistor limits current through the RP2040's internal ESD clamp diodes to ~1.7mA — safe for continuous operation.

### Single-Port Maple Bus

The V2 PCB supports a single Maple Bus port (Port A) connected to the local Dreamcast. Online play is handled through emulator-mediated paths (MapleCast / Flycast WASM / ServerSync), not native-DC-to-native-DC injection. The "Port B" hardware approach was dropped because real-DC-to-real-DC online play suffers from state divergence (audio buffers, RNG, memory state) that cannot be reliably synchronized without emulator-level control.

| Port | GPIO Pair | Purpose |
|------|-----------|---------|
| Port A (P1) | GPIO 23 + 24 | Local player — YOUR buttons to Dreamcast |

GPIO 4 and GPIO 5 are now used as additional retro console data lines on the universal RJ45 Retro connector (pins 1 and 8), giving the connector a total of 6 controllable signal lines for supporting various retro console protocols (N64, GameCube, Saturn, PSX/PS2, NES/SNES, Genesis, etc).

---

## 7. Ethernet

### How online play works

```
Your Stick                              Opponent's Stick
┌──────────┐                          ┌──────────┐
│ RP2040   │  UDP packet (12 bytes)   │ RP2040   │
│ W5500 ───┼──→ Ethernet cable ──→────┼─── W5500 │
│          │  (button state + frame#) │          │
│ Maple    │                          │ Maple    │
│ Port B ←─┼── inject remote buttons  │ Port B ──┼→ inject remote buttons
└──────────┘                          └──────────┘
```

Each frame (~16.67ms), your stick sends a tiny UDP packet with your button state. The opponent's stick receives it and injects those buttons into Maple Bus Port B. The Dreamcast sees two controllers — your local buttons on Port A, their remote buttons on Port B.

### W5500 Connection

```
RP2040 SPI1              W5500
  GPIO 26 (SCK) ──33Ω──→ SCLK
  GPIO 27 (MOSI) ──────→ MOSI
  GPIO 28 (MISO) ←────── MISO
  GPIO 25 (CS) ─────────→ SCSn
  GPIO 29 ←───────────── INTn (interrupt: "packet arrived!")
```

The 33Ω resistor on SCK is a **series termination** — it dampens signal reflections that can cause data errors at high SPI speeds. Think of it like putting a shock absorber on a bouncy spring.

---

## 8. Buttons

### How button reading works

A button is the simplest circuit: it connects a GPIO pin to ground when pressed.

```
3.3V ──[internal pull-up]── GPIO pin ──[button]── GND
                                │
                            reads HIGH when open (not pressed)
                            reads LOW when pressed (grounded)
```

The RP2040 and STM32F730 have **internal pull-up resistors** — no external resistors needed. When the button isn't pressed, the pull-up holds the pin at 3.3V (logical HIGH). When pressed, the button connects the pin to ground (logical LOW). The chip reads "pressed" = LOW, "not pressed" = HIGH.

### Shared button traces

Both chips need to read the same buttons. In the PCB layout, each button trace goes to:
- An RP2040 GPIO pad (for Maple Bus / retro modes)
- An STM32 GPIO pad (for USB 8000Hz mode)

These are connected via **vias** (tiny plated holes that connect traces between layers). Button on top layer → via → RP2040 pad on top layer + STM32 pad routed on bottom layer.

---

## 9. Display

A 128×64 OLED display connected via I2C (2 wires: SDA for data, SCL for clock).

```
OLED Display          RP2040
  SDA ←──────────── GPIO 0
  SCL ←──────────── GPIO 1
  VCC ←──────────── 3.3V
  GND ←──────────── GND
```

The 4.7kΩ pull-up resistors on SDA and SCL are required by the I2C spec — they pull the lines HIGH when neither chip is driving them LOW. Without them, the lines would float and communication would be unreliable.

---

## 10. Pin Map — Every Wire Explained

### RP2040 — All 30 GPIOs Allocated

| GPIO | Connected To | Why |
|------|-------------|-----|
| 0 | OLED SDA + 4.7k pull-up to 3.3V | I2C data for display |
| 1 | OLED SCL + 4.7k pull-up to 3.3V | I2C clock for display |
| 2 | RJ45 Retro pin 4 (through 1kΩ) | Retro console data line |
| 3 | RJ45 Retro pin 7 (through 1kΩ) | Retro console data line |
| 4 | RJ45 Retro pin 1 (through 1kΩ) | Retro console data line (universal retro expansion) |
| 5 | RJ45 Retro pin 8 (through 1kΩ) | Retro console data line (universal retro expansion) |
| 6 | Button: R2 (Right Trigger) | Shared with STM32 |
| 7 | Button: B2 | Shared with STM32 |
| 8 | Button: B1 | Shared with STM32 |
| 9 | Button: L1 (Left Bumper) | Shared with STM32 |
| 10 | Button: R1 (Right Bumper) | Shared with STM32 |
| 11 | Button: B4 | Shared with STM32 |
| 12 | Button: B3 | Shared with STM32 |
| 13 | Button: S2 (Start) | Shared with STM32 |
| 14 | Button: L2 (Left Trigger) | Standard button placement |
| 15 | Button: S1 (Select) | Shared with STM32 |
| 16 | Button: D-pad Left | Shared with STM32 |
| 17 | Button: D-pad Right | Shared with STM32 |
| 18 | Button: D-pad Down | Shared with STM32 |
| 19 | Button: D-pad Up | Shared with STM32 |
| 20 | STM32 SPI CS (chip select) | Active LOW — tells STM32 "I'm talking to you" |
| 21 | Button: L3 (Left Stick Click) | Also connects to RJ45 Retro pin 5 |
| 22 | Button: R3 (Right Stick Click) | Also connects to RJ45 Retro pin 6 |
| **23** | **Maple Bus Port A — SDCKA (through 1kΩ)** | **Local player controller** |
| **24** | **Maple Bus Port A — SDCKB (through 1kΩ)** | **Must be GPIO 23+1 (consecutive pair)** |
| 25 | W5500 SPI CS | Active LOW — tells W5500 "I'm talking to you" |
| 26 | W5500 SPI SCK (through 33Ω) | SPI clock at up to 20MHz |
| 27 | W5500 SPI MOSI | Data: RP2040 → W5500 |
| 28 | W5500 SPI MISO | Data: W5500 → RP2040 |
| 29 | W5500 INTn | Interrupt: "a packet arrived!" (active LOW) |

**All 30 pins used. Zero wasted.**

### STM32F730R8T6 — Key Pin Assignments

| Pin | Name | Connected To | Why |
|-----|------|-------------|-----|
| 5 | PH0 (HSE_IN) | 8MHz crystal Y3 | Clock reference for PLL |
| 6 | PH1 (HSE_OUT) | 8MHz crystal Y3 | Crystal return path |
| 31 | USB_HS_DM | USB-C #1 D- | USB data minus (dedicated analog pin) |
| 32 | USB_HS_DP | USB-C #1 D+ | USB data plus (dedicated analog pin) |
| 59 | BOOT0 | 10kΩ to GND | Normal boot from flash (pull HIGH for DFU mode) |
| 64 | VDDUSB | 3.3V + 100nF + 1µF | Power for USB HS PHY (must be clean!) |
| 10 | PC2 (SPI2 MISO) | RP2040 GPIO 27 | SPI data from RP2040 |
| 11 | PC3 (SPI2 MOSI) | RP2040 GPIO 28 | SPI data to RP2040 |
| 27 | PB10 (SPI2 SCK) | RP2040 GPIO 26 | SPI clock from RP2040 |
| 37 | PC7 (SPI2 NSS) | RP2040 GPIO 20 | SPI chip select from RP2040 |
| ~20 pins | PA/PB/PC GPIOs | Button traces | Reads same 13 buttons as RP2040 |

---

## 11. Board Layout

**Target dimensions:** 96.3 × 45.31mm (matches GP2040-CE V5.6 Advanced Breakout Board exactly). Fallback if component density forces growth: up to 120 × 55mm, but mounting hole positions must remain compatible with V5.6 mounting brackets.

**Mounting holes:** Must match GP2040-CE V5.6 Advanced Breakout Board exact positions for fight stick enclosure compatibility. Reference the V5.6 Gerber file in `Hardware/Boards/GP2040-CE Official Boards/RP2040 Advanced Breakout Board/RP2040 Advanced Breakout Board - Passthrough/Hardware files/Gerber - RP2040 Advanced Breakout Board - Version 5.6 - PT.zip` for exact coordinates.

**Connector layout — split USB and RJ45 to opposite sides:**

- **Right side:** 1× USB-C (gaming, STM32) + 1× RJ45 Ethernet (W5500, with magnetics)
- **Left side:** 1× USB-C (config, RP2040) + 1× RJ45 Retro (Dreamcast/retro, no magnetics)

This distribution frees up edge space for screw terminals and mounting holes, and matches typical fight stick enclosure cable routing.

```
96.3mm × 45.31mm, 4-layer PCB (matches GP2040 V5.6 form factor)

TOP VIEW:
┌────────────────────────────────────────────────────────────────────────────┐
│ [Screw terminal row — 20-pin button signals, 2P blocks × 10]              │
│                                                                            │
│ [USB-C #2]                       [SW1]  [SW2]  [SW3]      [USB-C #1]      │
│  Config (RP2040)                  RST   BOOT   WCFG        Gaming (STM32) │
│                                                                            │
│ [RJ45 Retro]   [RP2040]  [W25Q128]   [STM32F730]   [W5500]  [RJ45 Ethernet]│
│  J3 (no mag)    U1         U2          U3           U4       J4 (w/ mag)  │
│                                                                            │
│              [12MHz Y1] [8MHz Y3]              [25MHz Y2]                  │
│              [AP2112K U5] [OLED 4-pin J6]                                  │
│ [3-pin toggle]                                          [2-pin 5V out]    │
│  block J7                                                block J8         │
│                                                                            │
│ [Brook 20-pin Header J5]      [Screw terminal row — 10-pin optional pins] │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘

BOTTOM VIEW:
┌────────────────────────────────────────────────────────────────────────────┐
│                                                                            │
│  [Decoupling capacitors distributed near each IC]                          │
│  [SPI/I2C/Maple Bus traces, retro RJ45 signal routing]                     │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘

4-LAYER STACKUP (cross-section):
  Layer 1 (Top):     Signal traces, chips, connectors
  Layer 2 (Inner 1): Solid ground plane (GND) — UNBROKEN under high-speed signals
  Layer 3 (Inner 2): 3.3V power plane
  Layer 4 (Bottom):  W5500, passive components, low-speed traces
```

**Why 4 layers?** The inner ground plane is critical for USB HS (480MHz signals need a solid reference plane beneath them) and reduces electromagnetic interference. 2-layer boards can work for simple designs but would be a nightmare to route with USB HS differential pairs and Ethernet.

### Branding and Cosmetics

| Element | Spec |
|---------|------|
| Board color (soldermask) | **Purple** |
| Silkscreen color | **Yellow** if achievable through the fab house — otherwise white (default). For JLCPCB the standard silkscreen color on purple boards is white; yellow silkscreen typically requires advanced/custom options. The freelancer should confirm yellow availability with the fab before finalizing; if not available we will accept white silkscreen for v1 production. |
| Logo / silkscreen text | **"NOBD-ZERO"** as the primary product name, large and centered on the top side. Use a stylized hand-drawn / brush-style font (similar in feel to the GP2040-CE V5.6 logo treatment). |
| Secondary silkscreen | Standard reference designators (U1, U2, R1, etc.), pin labels for the Brook header and screw terminals, version mark **"V1.0"** (first revision of the NOBD-ZERO product line), and required regulatory markings if any. |

---

## 12. EasyEDA Build Guide

### Step-by-step process (overview — each step will be a detailed walkthrough):

1. **Create new project** in EasyEDA Pro
2. **Place RP2040 + flash + crystal** — search JLCPCB library by part number
3. **Wire RP2040 power** — IOVDD, DVDD, USB_VDD to 3.3V with decoupling caps
4. **Wire flash to RP2040** — QSPI connections (6 data lines + clock + CS)
5. **Place STM32F730** — JLCPCB C785552
6. **Wire STM32 power** — VDD×4, VDDA, VDDUSB, NRST, BOOT0
7. **Wire STM32 crystal** — 8MHz on PH0/PH1
8. **Wire STM32 USB HS** — D+/D- to USB-C #1
9. **Place W5500** — JLCPCB C32843
10. **Wire W5500 power + crystal** — VDD, AVDD, 25MHz
11. **Wire W5500 SPI** — to RP2040 SPI1 pins
12. **Wire W5500 Ethernet** — TX/RX differential pairs to magnetics RJ45
13. **Place AP2112K + Schottky diodes** — power circuit
14. **Place USB-C connectors** — CC resistors, ESD protection, polyfuses
15. **Place RJ45 jacks** — retro (no magnetics) + Ethernet (with magnetics)
16. **Wire buttons** — 13 GPIOs to Brook header, shared with both chips
17. **Wire OLED header** — I2C pull-ups
18. **Place protection resistors** — Maple Bus 1kΩ series
19. **Run ERC** (Electrical Rules Check) — fix any warnings
20. **Switch to PCB editor** — place components, route traces
21. **Run DRC** (Design Rules Check) — verify JLCPCB manufacturing constraints
22. **Order from JLCPCB** — one-click export

Each step will be a separate detailed guide when you're ready to start building.

---

## 13. Design Decisions — Why We Chose Each Part

| Decision | Options Considered | Winner | Why |
|----------|-------------------|--------|-----|
| USB 8000Hz chip | STM32F723 (144-pin), STM32F730 (64-pin), CH569 | **STM32F730R8T6** | Same USB HS PHY, 75% smaller, 8 fewer caps, JLCPCB basic part |
| Ethernet chip | W6100 ($3.50, IPv6), W5500 ($1.50, IPv4) | **W5500** | Same speed, half the price, better stocked, IPv6 not needed |
| USB switching | TS3USB221 MUX, two ports | **Two USB-C ports** | Cheaper, simpler, no mode-switch logic, both ports usable simultaneously |
| Level shifter | 74LVC8T245 (20-pin IC), BSS138 MOSFETs, series resistors | **1kΩ series resistors** | 4 resistors replace a 20-pin IC. Proven in the field. $0.01 vs $0.50 |
| Voltage regulator | AMS1117 (SOT-223), AP2112K (SOT-23-5), ME6211 | **AP2112K-3.3** | Industry standard, small, 600mA is plenty, low dropout |
| Crystal sharing | Shared 12MHz, dedicated crystals | **Dedicated 8MHz for STM32** | $0.10 eliminates startup race condition. Worth it. |
| MicroSD | Include for saves, skip it | **Skip** | 14MB free in flash = 112 VMU saves. No mechanical failure point. |
| Power protection | Nothing, polyfuses, Schottky OR'ing | **Schottky diodes + polyfuses** | Prevents backfeed between USB ports. Non-negotiable safety. |
| PCB layers | 2-layer, 4-layer | **4-layer** | USB HS needs ground plane. Worth the ~$2 extra per board at JLCPCB. |

---

## Complete BOM Summary

| # | Part | Package | Qty | ~Cost | JLCPCB |
|---|------|---------|-----|-------|--------|
| 1 | RP2040 | QFN-56 | 1 | $0.70 | C2040 |
| 2 | W25Q128JVSIQ flash | SOIC-8 | 1 | $0.45 | C97521 |
| 3 | STM32F730R8T6 | LQFP-64 | 1 | $4.50 | C785552 |
| 4 | W5500 Ethernet | QFN-48 | 1 | $1.50 | C32843 |
| 4a | FE1.1S USB 2.0 HS Hub | SSOP-28 | 1 | $0.34 | C9359 |
| 4b | PCA9536DR I²C I/O expander | SOIC-8 | 1 | $0.40 | (TI PCA9536DR — verify JLCPCB part) |
| 5 | AP2112K-3.3 regulator | SOT-23-5 | 1 | $0.05 | C51118 |
| 6 | 12MHz crystal (RP2040) | 3225 | 1 | $0.04 | C115962 |
| 6a | 12MHz crystal (FE1.1S hub) | 3225 | 1 | $0.04 | C115962 |
| 7 | 8MHz crystal | 3225 | 1 | $0.04 | C115962 |
| 8 | 25MHz crystal | 3225 | 1 | $0.10 | C115962 |
| 9 | USB-C receptacle | SMD | 1 | $0.15 | C2688138 |
| 10 | RJ45 retro (no magnetics) | TH | 1 | $0.11 | C3000202 |
| 11 | RJ45 Ethernet (magnetics) | TH | 1 | $0.80 | C395988 |
| 12 | USBLC6-2SC6 ESD | SOT-23-6 | 1 | $0.08 | C7519 |
| 13 | B5819W Schottky diode | SOD-123 | 1 | $0.02 | C82544 |
| 14 | 500mA polyfuse | 0805 | 1 | $0.035 | C116170 |
| 15 | 20-pin Brook header | 2×10 TH | 1 | $0.10 | C35165 |
| 16 | 4-pin OLED header | JST-PH | 1 | $0.02 | C131337 |
| 17 | Reset button (SW1) | B3U-1000P | 1 | $0.075 | C843670 |
| 18 | BootSel button (SW2) | B3U-1000P | 1 | $0.075 | C843670 |
| 18a | Web Config button (SW3) | B3U-1000P | 1 | $0.075 | C843670 |
| 18b | Screw terminal 2P (DIBO DB125-3.81-2P-BK-S) | 3.81mm TH | 11 | $0.185 | C430618 |
| 19 | Status LED | Yellow 0603 | 1 | $0.01 | C72043 |
| 20 | 100nF caps | 0402 | 22 | $0.02 | C1525 |
| 21 | 10µF caps | 0603 | 4 | $0.02 | C19702 |
| 22 | 4.7µF cap | 0603 | 1 | $0.01 | C19666 |
| 23 | 1µF caps | 0603 | 2 | $0.01 | C15849 |
| 24 | 15pF caps (RP2040 crystal) | 0402 | 2 | $0.002 | C1548 |
| 25 | 20pF caps | 0402 | 4 | $0.004 | C1554 |
| 26 | 5.1kΩ resistors | 0402 | 4 | $0.004 | C25905 |
| 27 | 4.7kΩ resistors | 0402 | 2 | $0.002 | C25900 |
| 28 | 10kΩ resistors | 0402 | 3 | $0.003 | C25744 |
| 29 | 1kΩ resistors | 0402 | 4 | $0.004 | C11702 |
| 30 | 33Ω resistor | 0402 | 1 | $0.001 | C25111 |
| 31 | 330Ω resistor | 0402 | 1 | $0.001 | C25079 |
| | **TOTAL COMPONENTS** | | **~108** | **~$11.27** | |

> **Note:** Component count is ~108 with: (1) Web Config button SW3 — wired in parallel with S2 for convenience, no additional GPIO needed; (2) 10× 2-position screw terminal blocks for solderless wiring (~$1.85 added); (3) FE1.1S USB 2.0 HS hub (C9359, $0.34) enabling single USB-C port architecture; (4) PCA9536DR I²C I/O expander (~$0.40) on the existing OLED I²C bus, used by RP2040 to control STM32 BOOT0 + NRST for atomic single-file firmware updates; (5) one less USB-C connector and one less USBLC6 ESD chip (we now have one USB port, not two). Maple Port B was dropped from V2 (GPIO 4/5 repurposed as additional retro RJ45 signal lines). STM32 JLCPCB part number corrected to C478453 (~$3.83, extended part). RP2040 crystal load caps corrected to 15pF. W5500 crystal load caps confirmed at 18pF per W5500 application note (not 20pF — earlier doc had a typo). **STM32 SPI link must use SPI1 (PA4-PA7, pins 20-23)** to be compatible with the STM32 SPI ROM bootloader for atomic firmware updates.

### Cost to get 5 boards made at JLCPCB (updated estimate)

| Item | Cost |
|------|------|
| Components (~115 parts × 5 boards) | ~$61 |
| PCB fabrication (4-layer, 96.3×45.31mm target, 5 boards + controlled impedance) | ~$50 |
| SMT assembly (both sides, 5 boards) | ~$23 |
| Extended part fees (STM32F730R8T6 etc.) | ~$6 |
| Shipping | ~$10 |
| **Total for 5 complete boards** | **~$150 (~$30 each)** |

> Cost increased from original ~$98 estimate due to: 4-layer (was 2-layer), controlled impedance for USB HS and Ethernet, increased component count (115 vs 55), additional buttons (SW3 web config), screw terminal blocks for solderless wiring (~$3.22 per board), and STM32 extended part fee.

---

## What Makes This "The Fastest"

| Metric | This Board | Typical Fight Stick | Advantage |
|--------|-----------|-------------------|-----------|
| USB polling | 8000Hz (0.125ms) | 1000Hz (1ms) | **8× faster** |
| GPIO read | ~8ns | ~8ns (same chip) | Equal |
| Maple Bus response | 1-2µs (PIO ISR) | N/A (needs adapter) | **Native, no adapter** |
| Network latency | ~12µs (W5500 SPI) | N/A (no Ethernet) | **Hardware TCP/UDP** |
| Button-to-wire | <0.15ms total | 1-4ms typical | **6-26× faster** |

The total latency from finger press to console receiving the input: **under 0.15 milliseconds.** That's less than 1% of a single game frame (16.67ms at 60fps). Your opponent will never have a hardware advantage over you.
