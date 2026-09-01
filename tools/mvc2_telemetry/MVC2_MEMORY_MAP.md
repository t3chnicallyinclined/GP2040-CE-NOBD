# MVC2 Master Memory Map

All addresses below are **NAOMI RAM offsets** — the offset from the start of the emulated 32MB NAOMI RAM block.

To convert between formats:
- **DC Physical** → offset: subtract `0x0C000000` (e.g., `0x0C268760` → `0x268760`)
- **DEmul host** → offset: subtract `0x2C000000` (e.g., `0x2C268760` → `0x268760`)
- **Flycast host** → subtract Flycast base (from log: `BASE xxxxxxxx`)
- **Steam Fighting Collection** → subtract the NAOMI RAM base found via Cheat Engine (see WALKTHROUGH.md)

All values confirmed across multiple sources: Jesuszilla CE scripts, lord-yoshi trainer, flycast-dojo-training, CodeBreaker codes, and our own flycast_mvc2_probe.lua.

---

## Match State

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x289624` | byte  | In Match                 | 1 = fighting, 0 = menu/select     |
| `0x289630` | byte  | Game Timer               | Counts down from 99               |
| `0x289638` | byte  | Stage ID                 | Stage number                       |
| `0x28964A` | byte  | P1 Super Meter           | 0-5 (number of bars)              |
| `0x28964B` | byte  | P2 Super Meter           | 0-5 (number of bars)              |
| `0x1F9CD0` | dword | P1 Active Player Struct  | Pointer to active character data  |
| `0x1F9CD4` | dword | P2 Active Player Struct  | Pointer to active character data  |
| `0x1F9D80` | dword | Frames Drawn             | Frame counter (in-match)          |
| `0x3496B0` | dword | Total Frames Drawn       | Frame counter (global)            |

---

## Player 1 — Character 1 (Point)

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x268340` | byte  | Active Char Slot         | Which char is point (0/1/2)       |
| `0x268341` | byte  | Character ID             | Character enum value              |
| `0x25835C` | byte  | Alive                    | 0 = dead, nonzero = alive         |
| `0x26846C` | byte  | Visible                  | Whether char is on screen         |
| `0x268374` | float | Position X               |                                    |
| `0x268378` | float | Position Y               |                                    |
| `0x26839C` | float | Velocity X               |                                    |
| `0x2683A0` | float | Velocity Y               |                                    |
| `0x268450` | byte  | Facing Right             | 1 = facing right, 0 = facing left|
| `0x268494` | dword | Frame Value              | Current animation frame (hex)     |
| `0x2684A8` | dword | Animation Pointer        | Ptr to anim data (+0x20000000)   |
| `0x2684B0` | dword | Hitbox Table Pointer     | Ptr to hitbox data               |
| `0x268500` | dword | Hitbox Choice Pointer    |                                    |
| `0x268760` | byte  | Health                   | 0x90 (144) = full, 0x00 = dead   |
| `0x268764` | byte  | Health Recover (red HP)  | Recoverable health amount         |
| `0x268AA0` | dword | Attack Pointer           | Ptr to current attack data        |

---

## Player 1 — Character 2 (Assist 1)

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x268E88` | byte  | Active Char Slot         |                                    |
| `0x268E89` | byte  | Character ID             |                                    |
| `0x268F98` | byte  | Facing Right             |                                    |
| `0x2692A8` | byte  | Health                   | 0x90 = full                       |

---

## Player 1 — Character 3 (Assist 2)

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x2699D0` | byte  | Active Char Slot         |                                    |
| `0x2699D1` | byte  | Character ID             |                                    |
| `0x269AE0` | byte  | Facing Right             |                                    |
| `0x269DF0` | byte  | Health                   | 0x90 = full                       |

---

## Player 2 — Character 1 (Point)

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x2688E4` | byte  | Active Char Slot         |                                    |
| `0x2688E5` | byte  | Character ID             |                                    |
| `0x268918` | float | Position X               |                                    |
| `0x26891C` | float | Position Y               |                                    |
| `0x268940` | float | Velocity X               |                                    |
| `0x268944` | float | Velocity Y               |                                    |
| `0x2689F4` | byte  | Facing Right             |                                    |
| `0x268D04` | byte  | Health                   | 0x90 = full                       |

---

## Player 2 — Character 2 (Assist 1)

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x26942C` | byte  | Active Char Slot         |                                    |
| `0x26942D` | byte  | Character ID             |                                    |
| `0x26953C` | byte  | Facing Right             |                                    |
| `0x26984C` | byte  | Health                   | 0x90 = full                       |

---

## Player 2 — Character 3 (Assist 2)

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x269F74` | byte  | Active Char Slot         |                                    |
| `0x269F75` | byte  | Character ID             |                                    |
| `0x26A084` | byte  | Facing Right             |                                    |
| `0x26A394` | byte  | Health                   | 0x90 = full                       |

---

## Character Select Screen

| Offset     | Size  | Description              | Values / Notes                    |
|------------|-------|--------------------------|-----------------------------------|
| `0x2FB2BC` | word  | P1 Cursor Column         |                                    |
| `0x2FB2BE` | word  | P1 Cursor Row            |                                    |
| `0x2D71E9` | byte  | Selected Char 1 ID       |                                    |
| `0x2D7D31` | byte  | Selected Char 2 ID       |                                    |
| `0x2D8879` | byte  | Selected Char 3 ID       |                                    |

---

## Projectile System

| Field              | Value        | Notes                           |
|--------------------|--------------|----------------------------------|
| Base offset        | `0x2723C4`   | Start of projectile array (base `0x260000` + `0x123C4`) |
| Struct size        | `0x1D0`      | Bytes per projectile entry       |
| Max projectiles    | 188          | Total projectile slots           |
| Position X         | `+0x34`      | Float, relative to projectile base |
| Position Y         | `+0x38`      | Float                            |
| Velocity X         | `+0x5C`      | Float                            |
| Velocity Y         | `+0x60`      | Float                            |

First projectile position X: `0x2723F8` (= `0x2723C4 + 0x34`)

---

## Player Struct Spacing Pattern

Characters within a player follow a repeating struct pattern:

| Player | Char | Base Offset | HP Offset  | ID Offset  | Spacing from prev |
|--------|------|-------------|------------|------------|--------------------|
| P1     | C1   | `0x268340`  | `0x268760` | `0x268341` | —                  |
| P1     | C2   | `0x268E88`  | `0x2692A8` | `0x268E89` | `+0xB48`           |
| P1     | C3   | `0x2699D0`  | `0x269DF0` | `0x2699D1` | `+0xB48`           |
| P2     | C1   | `0x2688E4`  | `0x268D04` | `0x2688E5` | —                  |
| P2     | C2   | `0x26942C`  | `0x26984C` | `0x26942D` | `+0xB48`           |
| P2     | C3   | `0x269F74`  | `0x26A394` | `0x269F75` | `+0xB48`           |

Character struct size: **0xB48 bytes** (2888 bytes per character).
HP is at offset `+0x420` from the character base.
Character ID is at `+0x01` from the character base.

---

## Addresses Still To Be Discovered

These would be valuable for a full telemetry/ranking system:

- [ ] Combo hit counter (current combo length)
- [ ] Damage scaling value
- [ ] Hitstun / blockstun remaining frames
- [ ] Guard meter / guard break
- [ ] Assist lockout timer
- [ ] DHC (Delayed Hyper Combo) state
- [ ] Input buffer (what the game received)
- [ ] Round/match win count within a set
- [ ] Pushblock state
- [ ] Advancing guard state
- [ ] Snap-back state
- [ ] X-Factor / Power-up state (N/A for MVC2, relevant for MVC3)

---

## Sources

1. **Jesuszilla/cheatengine-scripts** — `MvC2.CT` (DEmul 0.57) — position, velocity, animation, hitbox, projectile data
2. **lord-yoshi/MvC2-CE-Trainer-Script** — `MvC2.CT` (DEmul 0.7) — hitbox overlay, frame stepping, macros
3. **blueminder/flycast-dojo-training** — `mvsc2.lua` — health, timer, meter, character IDs, facing direction
4. **Our flycast_mvc2_probe.lua** — health, timer, meter, character IDs, character select, Maple DMA
5. **CodeBreaker / GameHacking.org** — health addresses confirmed
6. **mamecheat.co.uk** — NAOMI MAME cheat XML addresses
