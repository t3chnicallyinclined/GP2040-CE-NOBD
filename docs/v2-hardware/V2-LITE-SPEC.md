# V2 Lite Board — Design Specification

Simplified V2 board: RP2040 + STM32F730 + W6100 Ethernet. 63 components on 90×45mm 4-layer PCB.

## Architecture

```
                    ┌─────────────┐
  USB-C ──→ MUX ──→│ STM32F730   │──→ 8000Hz USB HID reports
              │     │ (LQFP-64)  │
              │     │ reads GPIOs │
              └──→  │ directly    │
                    └──────┬──────┘
                           │ SPI (config/VMU sync)
                    ┌──────┴──────┐
  RJ45 Retro ──→   │   RP2040    │──→ Dreamcast Maple Bus (PIO0)
  RJ45 Ethernet ←→ │ W6100 SPI1  │──→ Online play (W6100)
  OLED ←──────────→ │ I2C         │
                    └─────────────┘
```

**Mode switching:** TS3USB221 MUX selects STM32 (default, gaming) or RP2040 (boot button held, flashing/web config).

**Button reading:** Both chips read the same 13 button GPIOs via shared traces + vias.

**Dual Maple Bus:** GPIO 4+5 (Port B, remote player) + GPIO 23+24 (Port A, local player) for online MvC2.

---

## Why STM32F730 Instead of STM32F723

| | STM32F723ZET6 | STM32F730R8T6 |
|---|---|---|
| Package | LQFP-144 (20×20mm) | **LQFP-64 (10×10mm)** |
| VDD pins | 11 | **4** |
| VCAP pins | 2 | **1** |
| Decoupling caps | ~21 | **~13** |
| Flash | 512KB | 64KB (enough for USB transport) |
| USB HS PHY | Internal | **Same internal PHY** |
| Core | Cortex-M7 216MHz | Same |
| Cost | ~$11.33 | **~$4.50** |

Same integrated USB HS PHY — no external ULPI needed. The 64KB flash is sufficient because the STM32 only handles USB HS HID transport; GP2040-CE runs on the RP2040.

---

## Component List (63 total)

### A. RP2040 Core (18 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| U1 | RP2040 | QFN-56 7×7mm | 1 |
| U2 | W25Q128JVSIQ | SOIC-8 (16MB flash) | 1 |
| X1 | Crystal | 12MHz 3225 | 1 |
| U3 | AMS1117-3.3 | SOT-223 (3.3V LDO) | 1 |
| C_XA, C_XB | Crystal load | 33pF 0402 | 2 |
| C_BULK1, C_BULK2 | Bulk decoupling | 10µF 0603 | 2 |
| C_VDD1–C_VDD10 | IO/core/USB decoupling | 100nF 0402 | 10 |

### B. STM32F730R8T6 — 8000Hz USB HS (14 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| U_ST | STM32F730R8T6 | LQFP-64 10×10mm | 1 |
| C_STVDD1–4 | VDD decoupling | 100nF 0402 | 4 |
| C_STVDDA | VDDA decoupling | 100nF 0402 | 1 |
| C_STVDDUSB | VDDUSB decoupling | 100nF 0402 | 1 |
| C_STNRST | NRST filter | 100nF 0402 | 1 |
| C_STVCAP | VCAP1 | 2.2µF 0603 | 1 |
| C_STVDDA_B | VDDA bulk | 1µF 0603 | 1 |
| C_STVDDUSB_B | VDDUSB bulk | 1µF 0603 | 1 |
| C_STVDD_B | VDD bulk | 4.7µF 0603 | 1 |
| R_STNRST | NRST pull-up | 10kΩ 0402 | 1 |
| -- | BOOT0 | Hardwired to GND | 0 |

**Crystal:** Shared with RP2040's 12MHz (via oscillator buffer) or dedicated (+3 parts).

### C. USB-C + MUX (6 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| USB1 | USB-C receptacle | GCT USB4105 | 1 |
| U_MUX | TS3USB221 | SOT-23-6 | 1 |
| R_CC1, R_CC2 | CC pull-down | 5.1kΩ 0402 | 2 |
| D1 | ESD protection | USBLC6-2SC6 SOT-23-6 | 1 |
| F1 | Polyfuse | 500mA 0805 | 1 |

### D. Level Shifter + RJ45 Retro (4 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| U_LS | 74LVC8T245PW | TSSOP-24 | 1 |
| C_LSA, C_LSB | Decoupling | 100nF 0402 | 2 |
| J1 | RJ45 retro (no magnetics) | Amphenol RJHSE538X | 1 |

### E. W6100 Ethernet (12 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| U_ETH | W6100 | LQFP-48 7×7mm | 1 |
| J_ETH | RJ45 with magnetics | Pulse JK0654219NL | 1 |
| X_ETH | Crystal | 25MHz 3225 | 1 |
| C_ETH1–4 | VDD decoupling | 100nF 0402 | 4 |
| C_ETH5 | Bulk decoupling | 10µF 0603 | 1 |
| C_XETH_A, C_XETH_B | Crystal load | 20pF 0402 | 2 |
| R_ETHRST | RSTn pull-up | 10kΩ 0402 | 1 |
| R_SCK | SPI SCK termination | 33Ω 0402 | 1 |

### F. UI (7 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| SW1 | Reset button | B3U-1000P | 1 |
| SW2 | Boot button | B3U-1000P | 1 |
| CN19 | OLED header | 4-pin JST-PH 2mm | 1 |
| R_SDA, R_SCL | I2C pull-ups | 4.7kΩ 0402 | 2 |
| LED3 | Status LED | Yellow 0603 | 1 |
| R_LED | LED resistor | 330Ω 0402 | 1 |

### G. Connectors (2 parts)

| Ref | Part | Value/Package | Qty |
|-----|------|---------------|-----|
| H1 | Brook header | 2×10 2.54mm | 1 |
| MH1–4 | Mounting holes | M3 | 4 (mechanical) |

---

## RP2040 GPIO Map — All 30 Pins Allocated

| GPIO | Function | Notes |
|------|----------|-------|
| 0 | I2C SDA | OLED display |
| 1 | I2C SCL | OLED display |
| 2 | RJ45 retro pin 4 | Via level shifter |
| 3 | RJ45 retro pin 7 | Via level shifter |
| **4** | **Maple Bus Port B data A** | Online play — remote player (P2) |
| **5** | **Maple Bus Port B data B** | Consecutive pair with GPIO 4 |
| 6 | R2 button | Shared with STM32 |
| 7 | B2 button | Shared with STM32 |
| 8 | B1 button | Shared with STM32 |
| 9 | L1 button | Shared with STM32 |
| 10 | R1 button | Shared with STM32 |
| 11 | B4 button | Shared with STM32 |
| 12 | B3 button | Shared with STM32 |
| 13 | S2 (Start) | Shared with STM32 |
| **14** | **L2 button** | Moved from GPIO 5 to free Maple Port B |
| 15 | S1 (Select) | Shared with STM32 |
| 16 | Left | Shared with STM32 |
| 17 | Right | Shared with STM32 |
| 18 | Down | Shared with STM32 |
| 19 | Up | Shared with STM32 |
| 20 | Level shifter DIR | 74LVC8T245 direction |
| 21 | L3 / RJ45 retro pin 5 | Dual use |
| 22 | R3 / RJ45 retro pin 6 | Dual use |
| **23** | **Maple Bus Port A data A** | Local player (P1) |
| **24** | **Maple Bus Port A data B** | Consecutive pair with GPIO 23 |
| 25 | W6100 SPI CS | Active low |
| 26 | W6100 SPI SCK | SPI1, 33Ω series termination |
| 27 | W6100 SPI MOSI | SPI1 TX |
| 28 | W6100 SPI MISO | SPI1 RX |
| 29 | W6100 INT | Active low interrupt |

---

## Board Specifications

| Spec | Value |
|------|-------|
| Dimensions | 90 × 45mm |
| Layers | 4 (F.Cu / GND / +3V3 / B.Cu) |
| Thickness | 1.6mm |
| Component count | 63 |
| BOM cost (components) | ~$11.43 |
| Total cost (5-unit JLCPCB run) | ~$39–42/board |

---

## What's Cut vs Full V2

| Removed | Parts Saved |
|---------|-------------|
| F723 → F730 (LQFP-144 → LQFP-64) | -8 caps, 75% smaller |
| USB-A host port + polyfuse | -2 |
| MicroSD slot + pull-ups | -5 |
| SWD debug header | -1 |
| 6× JST option connectors | -6+ |
| RGB LED connector | -1 |
| Extra headers (USB direct, debug, 5V out) | -4 |
| **Total reduction** | **131 → 63 (52%)** |

---

## What's Preserved

- 8000Hz USB polling (STM32F730 internal USB HS PHY)
- Dreamcast Maple Bus (PIO0, GPIO 23+24)
- Dual Maple Bus for online play (GPIO 4+5 = Port B)
- W6100 Ethernet (hardware TCP/UDP, ~12µs latency)
- 13 fighting game buttons + Brook header compatibility
- I2C OLED display
- Level shifter for 5V retro protocols
- Web config via USB-C (MUX to RP2040)
- VMU saves in flash (14MB free in W25Q128)
