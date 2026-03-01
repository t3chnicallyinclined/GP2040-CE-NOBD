# Tap Sync Window - GP2040-CE Debounce Replacement

## What This Is

A replacement for the default debounce logic in GP2040-CE firmware that adds a **sync window** for simultaneous button presses. Designed for fighting games (MVC2, Street Fighter, etc.) where pressing two buttons at the "same time" often registers as two separate frames — causing dropped dashes, supers, and assists.

## The Problem

When you press LP+HP for a dash in MVC2, your fingers don't hit both buttons on the exact same microsecond. One button lands a few milliseconds before the other. The game reads inputs per frame at 60fps (~16.67ms per frame), so if the two presses straddle a frame boundary, the game sees LP on one frame and HP on the next — and you get a punch instead of a dash.

## How It Works

The sync window buffers **new button presses** (0→1 transitions) for a configurable window (default ~8ms). If a second button press arrives during that window, both are committed together on the same frame.

Key behaviors:
- **New presses**: Buffered in the sync window so near-simultaneous presses land together
- **Releases** (1→0): Pass through **instantly** — no added latency for charge moves (Megaman buster, Sentinel drones, etc.)
- **Holds**: Completely unaffected — once a press is committed, the button stays held with zero interference
- **Config mode**: Bypassed entirely so the web UI always works
- **Slider = 0**: Raw passthrough, no sync buffering at all

## Files Changed

### `src/gp2040.cpp` — Core sync logic
Replaced the `debounceGpioGetAll()` function. The original was a simple frame-rate throttle. The new version:
1. Detects truly new presses (`raw & ~prev & ~sync_new`)
2. Passes releases through instantly (`gamepad->debouncedGpio &= ~just_released`)
3. Accumulates new presses into a sync buffer (`sync_new |= just_pressed`)
4. Commits all buffered presses when the window expires (`gamepad->debouncedGpio |= sync_new`)

### `www/src/Locales/en/SettingsPage.jsx` — UI label
Renamed "Debounce Delay in milliseconds" to "Tap Sync Window in milliseconds" in the web UI settings page.

### `build_fw.ps1` — Build script
PowerShell build script for Windows. Sets up the MSVC environment, overrides the `GP2040_BOARDCONFIG` env var (which otherwise overrides CMake `-D` flags), and builds with Ninja.

## Configuration

In the GP2040-CE web UI (hold S2 on boot, navigate to `http://192.168.7.1`):
- Go to **Settings**
- Set **Tap Sync Window** slider:
  - **0 ms** = raw passthrough (no sync)
  - **~8 ms** = recommended for fighting games (MVC2, SF6, etc.)
  - **10-12 ms** = if you still get dropped simultaneous presses
  - Higher values add more latency to initial button presses, so keep it as low as works for you

## Building

```powershell
# From the GP2040-CE directory
powershell -ExecutionPolicy Bypass -File build_fw.ps1
```

Output: `build/GP2040-CE_0.7.11_RP2040AdvancedBreakoutBoard.uf2`

## Flashing

1. Unplug the board
2. Hold BOOTSEL and plug in via USB
3. Copy the `.uf2` file to the `RPI-RP2` drive that appears
4. Board auto-reboots with new firmware

## Board

Built for **RP2040 Advanced Breakout Board**. To build for a different board, change `RP2040AdvancedBreakoutBoard` in `build_fw.ps1` to your board's config directory name under `configs/`.
