# Cheat Engine Walkthrough: Finding MVC2 NAOMI RAM in Steam Fighting Collection

## Goal

Find where the Steam Fighting Collection's emulator maps the 32MB NAOMI RAM block
in host process memory. Once found, every offset in `MVC2_MEMORY_MAP.md` works directly.

---

## Prerequisites

- Cheat Engine 7.5+ installed
- Marvel vs Capcom Fighting Collection installed on Steam
- A controller or keyboard to play MVC2

---

## Phase 1: Initial Setup (5 minutes)

### Step 1 — Find the process name

1. Launch the Fighting Collection from Steam
2. Select MVC2 from the game menu and start it
3. Open **Task Manager** (Ctrl+Shift+Esc) → Details tab
4. Look for the game process — likely named something like:
   - `MVCFC.exe`
   - `Marvel vs. Capcom Fighting Collection.exe`
   - `nativePCx64.exe` (MT Framework convention)
   - `game.exe`
5. **Write down the exact process name** — you'll need it for the companion app

### Step 2 — Attach Cheat Engine

1. Open Cheat Engine
2. Click the **computer icon** (top-left) → "Select a process to open"
3. Find and select the game process from Step 1
4. Click "Open"

### Step 3 — Configure scan settings

1. Go to **Edit → Settings → Scan Settings**
2. Check **"MEM_MAPPED"** — emulators often use memory-mapped regions
3. Also ensure **"MEM_PRIVATE"** and **"MEM_IMAGE"** are checked
4. Click OK

---

## Phase 2: Find the Timer Address (10 minutes)

The game timer is the easiest value to find because it visibly counts down.

### Step 4 — Start a match

1. In MVC2, go to Arcade mode or VS mode
2. Start a match — the timer at the top shows "99" counting down
3. **Let the timer tick down to about 90** so you have a known value

### Step 5 — First scan

1. In Cheat Engine, set:
   - **Value Type:** Byte
   - **Scan Type:** Exact Value
   - **Value:** whatever the timer currently shows (e.g., 90)
2. Click **"First Scan"**
3. You'll get thousands of results — that's normal

### Step 6 — Filter by changing value

1. Wait for the timer to tick down a few more seconds (e.g., to 85)
2. In Cheat Engine:
   - **Value:** 85 (the current timer value)
   - Click **"Next Scan"**
3. The results list shrinks dramatically
4. **Repeat** 2-3 more times as the timer ticks down
5. You should be down to 1-5 results

### Step 7 — Identify the real timer

If you have multiple results:
1. Double-click each to add it to the address list (bottom pane)
2. Watch which one updates in real-time matching the on-screen timer
3. The correct address is the one that tracks perfectly

**Write down this address.** Example: `0x7FF4AF529630`

---

## Phase 3: Calculate the NAOMI RAM Base (5 minutes)

### Step 8 — The math

The timer's known NAOMI offset is `0x289630` (from our memory map).

```
NAOMI_RAM_BASE = timer_address - 0x289630
```

Example:
```
timer found at:     0x7FF4AF529630
known offset:     - 0x0000289630
                  ================
NAOMI_RAM_BASE:     0x7FF4AF2A0000
```

### Step 9 — Validate with known addresses

Verify by manually checking other known values:

1. In Cheat Engine, click **"Add Address Manually"** (bottom-left)
2. Enter: `NAOMI_RAM_BASE + 0x289624` → should be `1` during a match, `0` in menus (in_match)
3. Enter: `NAOMI_RAM_BASE + 0x268760` → should be `0x90` (144) at full health for P1 char 1 (type: Byte)
4. Enter: `NAOMI_RAM_BASE + 0x268D04` → P2 char 1 health (Byte)
5. Enter: `NAOMI_RAM_BASE + 0x28964A` → P1 super meter (Byte, 0-5)

**If all these values check out, you've found the base.** Hit your opponent and watch the health value decrease. Use a super and watch the meter drop.

### Step 10 — Test position values (bonus)

1. Add `NAOMI_RAM_BASE + 0x268374` as **Float** → P1 X position
2. Add `NAOMI_RAM_BASE + 0x268378` as **Float** → P1 Y position
3. Move your character — the values should update in real-time
4. Jump — Y position should change

---

## Phase 4: Make It Persistent (ASLR) (15-30 minutes)

The base address changes every time the game launches (ASLR). We need a stable way to find it.

### Method A — Pointer Scan (recommended)

1. With the timer address still in your list, right-click it → **"Pointer scan for this address"**
2. In the pointer scan dialog:
   - Max level: 5
   - Max offset: 4096
   - Click OK
3. This takes a few minutes — CE searches for pointer chains from static module bases
4. **Save the pointer scan results**
5. **Restart the game** completely
6. Re-attach CE to the new process
7. Find the timer again (quick scan since you know the value)
8. In the pointer scan results: **"Rescan memory"** → enter the new timer address
9. This eliminates pointer chains that don't survive restarts
10. Repeat restart+rescan 2-3 times — you'll narrow down to stable pointer chains
11. The surviving pointer chain is your **permanent base address finder**

### Method B — AOB Signature Scan (alternative)

1. With the timer address known, go to **Memory View** (Ctrl+B from CE main)
2. Navigate to a code address that READS the timer (set a breakpoint on the timer address)
3. When the breakpoint hits, you're in the emulator's read function
4. Copy ~20 bytes of the machine code around that instruction
5. This byte pattern is your **AOB signature** — it doesn't change between runs
6. In your companion app: scan for this pattern → extract the base address from the surrounding code

### Method C — Module + offset (simplest, but fragile)

1. Note the game's module base: in CE, go to **Memory View → View → Enumerate DLL's and Symbols**
2. Find the main .exe module and its base address
3. Calculate: `pointer_offset = address_of_some_static_pointer - module_base`
4. In your app: `base = GetModuleHandle("game.exe") + pointer_offset`, then follow the pointer

---

## Phase 5: Save Your Work

### Step 11 — Save the CE table

1. In Cheat Engine: **File → Save As**
2. Save as `MVC2_Steam_FC.CT`
3. This saves all your addresses, pointer chains, and notes

### Step 12 — Document the base address method

Once you find a stable pointer chain or AOB pattern, add it to this file:

```
PROCESS NAME: ____________
MODULE NAME:  ____________

POINTER CHAIN TO NAOMI RAM BASE:
  "module.exe" + 0x______ → +0x___ → +0x___ → NAOMI_BASE

OR AOB SIGNATURE:
  XX XX XX XX ?? ?? XX XX XX XX
  (base found at: pattern_address + offset)
```

---

## Phase 6: Explore New Addresses (ongoing)

### Finding combo counter, hitstun, etc.

With the base established, you can discover unknown addresses:

1. **Combo counter:** Start a combo in training. Search for value "1" (byte). Hit again → search "2". Keep going.
2. **Hitstun frames:** Search for a value that decreases by 1 each frame during hitstun (use "Decreased value" scan)
3. **Input buffer:** Search for changing values when you press buttons. Cross-reference with your controller inputs.

### Using ReClass.NET for struct exploration

1. Download [ReClass.NET](https://github.com/ReClassNET/ReClass.NET)
2. Attach to the game process
3. Navigate to a known character base (e.g., `NAOMI_BASE + 0x268340`)
4. ReClass shows you every byte around it as a struct layout
5. Label known fields, identify unknown ones by watching what changes during gameplay
6. Export as C struct for use in the companion app

---

## Quick Reference

| What you're looking for | Scan for | Type | Known offset |
|------------------------|----------|------|--------------|
| Timer (easiest start) | Exact value counting down | Byte | `0x289630` |
| P1 Health | Value that drops when hit | Byte | `0x268760` |
| P2 Health | Same, for player 2 | Byte | `0x268D04` |
| In Match flag | 1 during fight, 0 in menu | Byte | `0x289624` |
| P1 X Position | Float that changes as you walk | Float | `0x268374` |
| Super Meter | 0-5 as you build meter | Byte | `0x28964A` |

---

## Troubleshooting

**"I found 0 results after first scan"**
- Make sure MEM_MAPPED is enabled in scan settings
- Try scanning as 2-byte or 4-byte instead of byte
- The timer might be stored as a different type internally

**"The base address changes every launch"**
- That's ASLR — normal. Use Method A (pointer scan) to find a stable chain.

**"Values don't match after calculating base"**
- Double-check your subtraction
- The game might use a different memory layout than standard NAOMI — try searching for the known HP value (0x90 = 144 for full health) directly

**"Cheat Engine can't attach to the process"**
- Run CE as Administrator
- Disable Steam Overlay (can interfere)
- Check if the game has Enigma Protector DRM — CE usually works through it, but you may need to wait until the game fully loads
