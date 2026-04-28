# GP2040-CE V2 Ultimate — The Fastest Fighting Game PCB Ever Made

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
- **Cost:** ~$4.50
- **JLCPCB:** C785552 (basic part = cheaper assembly)

#### U4: W5500 — The Network Card
- **What:** Hardware TCP/UDP Ethernet controller with 8 sockets
- **Package:** QFN-48 (7×7mm)
- **Why this chip:** Has a complete network stack built into silicon. The RP2040 doesn't need to run any networking code — it just writes "send this UDP packet" to a register and the W5500 handles everything (IP headers, checksums, ARP, etc). Response time: ~12 microseconds from "packet arrived" to "data in RP2040 memory."
- **Why not WiFi?** WiFi adds 2-10ms of unpredictable latency (buffering, retransmits, interference). Ethernet is deterministic — every packet takes the same time. For fighting games where 1 frame = 16.67ms, WiFi jitter is unacceptable.
- **Cost:** ~$1.50
- **JLCPCB:** C32843 (basic part)

---

### Crystals (2-3 tiny rocks)

A crystal is literally a piece of quartz cut to vibrate at an exact frequency when voltage is applied. Think of it as a tuning fork for electronics — it provides the heartbeat that keeps everything synchronized.

#### Y1: 12MHz Crystal (for RP2040)
- **What:** Vibrates 12 million times per second
- **Why 12MHz?** The RP2040's internal PLL (Phase-Locked Loop) multiplies this up: 12MHz × 10.4 = 125MHz (the default CPU speed). The PLL is like a gear ratio — small input, big output.
- **Package:** 3225 (3.2×2.5mm) — tiny 4-pad SMD
- **Needs:** 2× load capacitors (33pF each) — these help the crystal start oscillating cleanly

#### Y2: 25MHz Crystal (for W5500)
- **What:** Vibrates 25 million times per second
- **Why 25MHz?** Ethernet runs at 25MHz internally (the MII clock). The W5500 needs this exact frequency.
- **Needs:** 2× load capacitors (20pF each)

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

#### J1, J2: USB-C Receptacles (×2)
- **Why two?** One for gaming (STM32, 8000Hz to console/PC), one for configuration (RP2040, web config + firmware updates). No MUX chip needed — cleaner and more reliable.
- **Each needs:** 2× 5.1kΩ resistors on CC1/CC2 pins (tells the host "I'm a device, give me power")
- **Cost:** ~$0.15 each

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

#### SW2: Boot Button
- **What:** Hold during power-on to enter firmware update mode
- **For RP2040:** Enters UF2 bootloader (drag-and-drop firmware update)

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

#### R_MAPLE1-4: 1kΩ Series Resistors (×4)
- **What:** Current-limiting resistors on Dreamcast Maple Bus data lines
- **Why:** The Dreamcast outputs 5V signals. The RP2040 can only handle 3.3V. These resistors limit the current that flows through the RP2040's internal protection diodes when 5V is present, keeping it within safe limits (~1.7mA vs the ~2mA max).
- **Without them:** The RP2040 works fine (many boards skip this), but the excess voltage slowly degrades the chip over months/years. The $0.01 resistors make it safe forever.

---

### Resistors Summary

| Ref | Value | Qty | Purpose |
|-----|-------|-----|---------|
| R_CC1-4 | 5.1kΩ | 4 | USB-C CC identification (2 per port) |
| R_SDA, R_SCL | 4.7kΩ | 2 | I2C bus pull-ups for OLED display |
| R_MAPLE1-4 | 1kΩ | 4 | Maple Bus 5V protection |
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
| LEDs + pull-ups | 15mA | Misc overhead |
| **Total** | **~282mA** | AP2112K provides 600mA — plenty of headroom |

---

## 5. USB

### Port 1: Gaming (STM32F730 USB HS)
- **Speed:** 480 Mbps (High Speed)
- **Polling rate:** 8000Hz (0.125ms between reports)
- **Protocol:** USB HID gamepad
- **Connects to:** PS4, Xbox, Switch, PC
- **The USB HS PHY** is built into the STM32F730 silicon. The D+ and D- signals go directly from the chip to the USB-C connector — no external components needed (except ESD protection).

### Port 2: Configuration (RP2040 USB FS)
- **Speed:** 12 Mbps (Full Speed)
- **Purpose:** Web configuration, firmware updates (UF2 drag-and-drop), RP2040-native modes
- **Not used during gameplay** — this is the "maintenance port"

### Why not one port with a switch?
The original V2 design used a TS3USB221 MUX chip to share one USB-C between both chips. This works but adds complexity (chip, routing, mode-switch logic, signal degradation). Two ports is simpler, cheaper, and you can have both plugged in simultaneously (the Schottky diodes prevent power backfeed).

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

### Dual-Port for Online Play

For online MvC2 (Marvel vs. Capcom 2), we need to control **two** Dreamcast controller ports — one for the local player, one for the remote player whose inputs arrive over Ethernet.

| Port | GPIO Pair | Purpose |
|------|-----------|---------|
| Port A (P1) | GPIO 23 + 24 | Local player — YOUR buttons |
| Port B (P2) | GPIO 4 + 5 | Remote player — inputs from network |

The RP2040's PIO can run multiple state machines simultaneously. Each port gets its own PIO program instance. The firmware already supports this (`PortState` struct with per-port packet buffers).

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
| **4** | **Maple Bus Port B — SDCKA (through 1kΩ)** | **Remote player controller (online play)** |
| **5** | **Maple Bus Port B — SDCKB (through 1kΩ)** | **Must be GPIO 4+1 (consecutive pair)** |
| 6 | Button: R2 (Right Trigger) | Shared with STM32 |
| 7 | Button: B2 | Shared with STM32 |
| 8 | Button: B1 | Shared with STM32 |
| 9 | Button: L1 (Left Bumper) | Shared with STM32 |
| 10 | Button: R1 (Right Bumper) | Shared with STM32 |
| 11 | Button: B4 | Shared with STM32 |
| 12 | Button: B3 | Shared with STM32 |
| 13 | Button: S2 (Start) | Shared with STM32 |
| **14** | **Button: L2 (Left Trigger)** | **Moved here from GPIO 5 to free Maple Port B** |
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

```
90mm × 45mm, 4-layer PCB

TOP VIEW:
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│  [USB-C #1]  [USB-C #2]  [BOOT] [RESET]  [OLED 4-pin]             │
│   Gaming      Config      SW2    SW1       J6                       │
│                                                                      │
│  [RP2040]   [W25Q128]   [12MHz]  [AP2112K]      [RJ45 Retro]      │
│   U1         U2          Y1       U5              J3                │
│                                                                      │
│  [STM32F730] [8MHz]                               [RJ45 Ethernet]  │
│   U3          Y3        [Brook 20-pin Header]      J4              │
│                          J5                                         │
│  [LED]                                                              │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘

BOTTOM VIEW:
┌──────────────────────────────────────────────────────────────────────┐
│                                                                      │
│  [W5500]    [25MHz]                                                 │
│   U4         Y2                                                     │
│                                                                      │
│  [Decoupling capacitors distributed near each IC]                   │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘

4-LAYER STACKUP (cross-section):
  Layer 1 (Top):     Signal traces, chips, connectors
  Layer 2 (Inner 1): Solid ground plane (GND) — UNBROKEN under high-speed signals
  Layer 3 (Inner 2): 3.3V power plane
  Layer 4 (Bottom):  W5500, passive components, low-speed traces
```

**Why 4 layers?** The inner ground plane is critical for USB HS (480MHz signals need a solid reference plane beneath them) and reduces electromagnetic interference. 2-layer boards can work for simple designs but would be a nightmare to route with USB HS differential pairs and Ethernet.

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
| 5 | AP2112K-3.3 regulator | SOT-23-5 | 1 | $0.05 | C51118 |
| 6 | 12MHz crystal | 3225 | 1 | $0.04 | C115962 |
| 7 | 8MHz crystal | 3225 | 1 | $0.04 | C115962 |
| 8 | 25MHz crystal | 3225 | 1 | $0.10 | C115962 |
| 9 | USB-C receptacle | SMD | 2 | $0.30 | C2688138 |
| 10 | RJ45 retro (no magnetics) | TH | 1 | $0.11 | C3000202 |
| 11 | RJ45 Ethernet (magnetics) | TH | 1 | $0.80 | C395988 |
| 12 | USBLC6-2SC6 ESD | SOT-23-6 | 2 | $0.16 | C7519 |
| 13 | B5819W Schottky diode | SOD-123 | 2 | $0.04 | C82544 |
| 14 | 500mA polyfuse | 0805 | 2 | $0.07 | C116170 |
| 15 | 20-pin Brook header | 2×10 TH | 1 | $0.10 | C35165 |
| 16 | 4-pin OLED header | JST-PH | 1 | $0.02 | C131337 |
| 17 | Reset button | B3U-1000P | 1 | $0.03 | C231330 |
| 18 | Boot button | B3U-1000P | 1 | $0.03 | C231330 |
| 19 | Status LED | Yellow 0603 | 1 | $0.01 | C72043 |
| 20 | 100nF caps | 0402 | 22 | $0.02 | C1525 |
| 21 | 10µF caps | 0603 | 4 | $0.02 | C19702 |
| 22 | 4.7µF cap | 0603 | 1 | $0.01 | C19666 |
| 23 | 1µF caps | 0603 | 2 | $0.01 | C15849 |
| 24 | 33pF caps | 0402 | 2 | $0.002 | C1560 |
| 25 | 20pF caps | 0402 | 4 | $0.004 | C1554 |
| 26 | 5.1kΩ resistors | 0402 | 4 | $0.004 | C25905 |
| 27 | 4.7kΩ resistors | 0402 | 2 | $0.002 | C25900 |
| 28 | 10kΩ resistors | 0402 | 3 | $0.003 | C25744 |
| 29 | 1kΩ resistors | 0402 | 4 | $0.004 | C11702 |
| 30 | 33Ω resistor | 0402 | 1 | $0.001 | C25111 |
| 31 | 330Ω resistor | 0402 | 1 | $0.001 | C25079 |
| | **TOTAL COMPONENTS** | | **~55** | **~$9.20** | |

### Cost to get 5 boards made at JLCPCB

| Item | Cost |
|------|------|
| Components (55 parts × 5 boards) | ~$46 |
| PCB fabrication (4-layer, 90×45mm, 5 boards) | ~$12 |
| SMT assembly (both sides, 5 boards) | ~$25 |
| Shipping | ~$15 |
| **Total for 5 complete boards** | **~$98 ($19.60 each)** |

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
