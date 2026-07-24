# GP2040-TE — Target Structure (the overhaul map)

**Branch:** `gp2040-te`. This is the target tree we migrate *toward*, incrementally,
keeping the build green at every step. Organizing principle: the NOBD **buffer +
reflector** architecture — one input pipeline writes one buffer; reflectors read it.

## Scope of the overhaul
- **Reorganized:** our ~130 source files under `src/` + `headers/`.
- **NOT touched:** `lib/` (16 vendored deps — tinyusb, lwip, nanopb, OneBitDisplay…),
  `configs/` (53 board-config data files), `build*/`, `docs/`, `www/`. Moving vendored
  code or board data is pure risk, zero simplicity gain.
- **Headers:** co-located with their `.cpp` per module (nobd-zero-v2 style, no separate
  `headers/` tree) — but done LAST, per module, because it churns `#include` paths. The
  `.cpp` reorg comes first (low risk: only CMake source paths change; includes still
  resolve via the `headers/` include dir until we co-locate).

## Target tree (`src/`)

```
src/
  input/            # THE PIPELINE → the one buffer (single source of truth)
    gamepad.*         # the buffer + read (remap physical→logical) + process (SOCD ONCE)
    pipeline.*        # scan → debounce → sync-window → turbo → macros glue (from core0)
    addons/           # input-processing addons (turbo, macro, analog, socd-slider,
                      #   tilt, reverse, rotary, wii-ext, snes, i2c-analog, pcf8575…)
  reflect/          # THE REFLECTORS: read the buffer → present to the ONE active target
    usb/              # xinput, ps4, ps3, switch, switchpro, hid, xbone, xboxog, psclassic, keyboard
    retro/            # dreamcast(maple), neogeo, pcengine, mdmini, egret, astro
    lan/              # NEW — PIO 10BASE-T raw-Ethernet reflector (Track B)
    manager.*         # selects the active reflector (was DriverManager)
  platform/         # the machine: cores, boot, storage, safety
    main.*            # entry
    core0.* / core1.* # the two loops (was gp2040.cpp / gp2040aux.cpp)
    system, watchdog, faultcapture, storage, config, peripherals
  ui/               # Core1 presentation, off the input path
    display/          # SSD1306 + screens/elements/fonts
    leds/             # animationstation + player/board LEDs
  net/              # ethernet / W6100 support consumed by reflect/lan
  hal/              # board seam: i2c/spi/flash/gpio interfaces
lib/     configs/     # UNCHANGED (vendored deps + board data)
```

## Why this is "simplicity through engineering"
- **One data flow:** `input/` writes `gamepad` (SOCD once) → `reflect/*` read it. Today
  the USB path build+sends in the loop while DC uses a PIO responder AND reflects
  pre-SOCD state — the reorg makes them uniform and fixes the DC-skips-SOCD bug.
- **New reflectors drop in:** `reflect/lan/` (PIO Ethernet) is just another reader of the
  buffer — no special case.
- **The folder tells the story:** anyone opening `src/` sees pipeline → buffer → reflectors.

## Migration order (each step: move → update CMake → build-green → commit)
1. **`input/`** — the buffer + pipeline core (also where the A1 RAM-pin / A2 debounce / SOCD-once work lands). *Start here — highest value.*
2. **`reflect/usb/` + `reflect/manager`** — the USB reflectors; make them read the buffer, SOF-synced (A3).
3. **`reflect/retro/`** — the console reflectors; unify DC onto the buffer (SOCD fix).
4. **`reflect/lan/`** — NEW PIO 10BASE-T reflector (Track B).
5. **`platform/`, `ui/`, `net/`, `hal/`** — the supporting subsystems (mechanical; lowest priority).
6. **Co-locate headers** per module (last; the `#include`-churn step).

Vendored `lib/` and `configs/` stay put throughout.
