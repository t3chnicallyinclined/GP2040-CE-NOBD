"""
MVC2 Telemetry Reader — Steam Fighting Collection
===================================================
Reads MVC2 game state from the Steam Fighting Collection process memory.

Usage:
    1. Run Cheat Engine walkthrough (WALKTHROUGH.md) to find NAOMI_RAM_BASE
    2. Update PROCESS_NAME and NAOMI_RAM_BASE below
    3. pip install pymem
    4. python mvc2_reader.py

Once the base address is stable (via pointer chain), this script reads
all game state at 60fps and outputs it as JSON for the ranking system.
"""

import sys
import time
import json
import struct
from dataclasses import dataclass, asdict
from typing import Optional

try:
    import pymem
    import pymem.process
except ImportError:
    print("ERROR: pymem not installed. Run: pip install pymem")
    print("       (requires Python 3.7+)")
    sys.exit(1)

# =============================================================
# CONFIGURATION — Update these after running WALKTHROUGH.md
# =============================================================

# The game's process name (find via Task Manager when the game is running)
# Common names for MT Framework games: "nativePCx64.exe", "MVCFC.exe", etc.
PROCESS_NAME = "nativePCx64.exe"  # UPDATE THIS

# NAOMI RAM base address (found via Cheat Engine)
# Set to 0 to enable auto-scan mode (scans for timer value)
NAOMI_RAM_BASE = 0  # UPDATE THIS after CE walkthrough

# Polling rate
POLL_HZ = 60  # Read game state N times per second

# Output mode: "console", "json", "websocket"
OUTPUT_MODE = "console"

# =============================================================
# MVC2 NAOMI RAM OFFSETS (from MVC2_MEMORY_MAP.md)
# =============================================================

# Match state
OFF_IN_MATCH    = 0x289624
OFF_TIMER       = 0x289630
OFF_STAGE       = 0x289638
OFF_P1_METER    = 0x28964A
OFF_P2_METER    = 0x28964B
OFF_FRAME_COUNT = 0x1F9D80

# Player 1 characters (base, ID, HP, position, facing)
P1_CHARS = [
    {"base": 0x268340, "id": 0x268341, "hp": 0x268760, "hp_recover": 0x268764,
     "pos_x": 0x268374, "pos_y": 0x268378, "vel_x": 0x26839C, "vel_y": 0x2683A0,
     "facing": 0x268450, "alive": 0x25835C, "frame_val": 0x268494},
    {"base": 0x268E88, "id": 0x268E89, "hp": 0x2692A8,
     "facing": 0x268F98},
    {"base": 0x2699D0, "id": 0x2699D1, "hp": 0x269DF0,
     "facing": 0x269AE0},
]

# Player 2 characters
P2_CHARS = [
    {"base": 0x2688E4, "id": 0x2688E5, "hp": 0x268D04,
     "pos_x": 0x268918, "pos_y": 0x26891C, "vel_x": 0x268940, "vel_y": 0x268944,
     "facing": 0x2689F4},
    {"base": 0x26942C, "id": 0x26942D, "hp": 0x26984C,
     "facing": 0x26953C},
    {"base": 0x269F74, "id": 0x269F75, "hp": 0x26A394,
     "facing": 0x26A084},
]

# Character select
OFF_P1_CURSOR_COL = 0x2FB2BC
OFF_P1_CURSOR_ROW = 0x2FB2BE

# Full health value
FULL_HP = 0x90  # 144 decimal

# =============================================================
# CHARACTER ID TABLE
# =============================================================
CHAR_NAMES = {
    0x00: "Ryu",        0x01: "Zangief",    0x02: "Guile",
    0x03: "Morrigan",   0x04: "Anakaris",   0x05: "Strider",
    0x06: "Cyclops",    0x07: "Wolverine",  0x08: "Psylocke",
    0x09: "Iceman",     0x0A: "Rogue",      0x0B: "Captain America",
    0x0C: "Spider-Man", 0x0D: "Hulk",       0x0E: "Venom",
    0x0F: "Dr. Doom",   0x10: "Tron Bonne", 0x11: "Jill",
    0x12: "Hayato",     0x13: "Ruby Heart", 0x14: "SonSon",
    0x15: "Amingo",     0x16: "Marrow",     0x17: "Cable",
    0x18: "Abyss 1",    0x19: "Abyss 2",    0x1A: "Abyss 3",
    0x1B: "Chun-Li",    0x1C: "Megaman",    0x1D: "Roll",
    0x1E: "Akuma",      0x1F: "B.B. Hood",  0x20: "Felicia",
    0x21: "Charlie",    0x22: "Sakura",     0x23: "Dan",
    0x24: "Cammy",      0x25: "Dhalsim",    0x26: "M. Bison",
    0x27: "Ken",        0x28: "Gambit",     0x29: "Juggernaut",
    0x2A: "Storm",      0x2B: "Sabretooth", 0x2C: "Magneto",
    0x2D: "Shuma-Gorath",0x2E: "War Machine",0x2F: "Silver Samurai",
    0x30: "Omega Red",  0x31: "Spiral",     0x32: "Colossus",
    0x33: "Iron Man",   0x34: "Sentinel",   0x35: "Blackheart",
    0x36: "Thanos",     0x37: "Jin",        0x38: "Captain Commando",
    0x39: "Bone Wolverine", 0x3A: "Servbot",
}


# =============================================================
# DATA CLASSES
# =============================================================

@dataclass
class CharState:
    char_id: int = 0
    char_name: str = ""
    hp: int = 0
    hp_pct: float = 0.0
    hp_recover: int = 0
    pos_x: float = 0.0
    pos_y: float = 0.0
    vel_x: float = 0.0
    vel_y: float = 0.0
    facing_right: bool = True


@dataclass
class PlayerState:
    chars: list = None
    meter: int = 0
    active_char: int = 0

    def __post_init__(self):
        if self.chars is None:
            self.chars = []


@dataclass
class MatchState:
    in_match: bool = False
    timer: int = 0
    stage: int = 0
    frame: int = 0
    p1: PlayerState = None
    p2: PlayerState = None

    def __post_init__(self):
        if self.p1 is None:
            self.p1 = PlayerState()
        if self.p2 is None:
            self.p2 = PlayerState()


# =============================================================
# READER CLASS
# =============================================================

class MVC2Reader:
    def __init__(self, process_name: str, naomi_base: int = 0):
        self.process_name = process_name
        self.naomi_base = naomi_base
        self.pm: Optional[pymem.Pymem] = None
        self.prev_state: Optional[MatchState] = None
        self.match_count = 0

    def connect(self) -> bool:
        """Attach to the game process."""
        try:
            self.pm = pymem.Pymem(self.process_name)
            print(f"[+] Attached to {self.process_name} (PID: {self.pm.process_id})")
            return True
        except pymem.exception.ProcessNotFound:
            print(f"[-] Process '{self.process_name}' not found.")
            print(f"    Is the game running? Check Task Manager for the exact process name.")
            return False
        except pymem.exception.CouldNotOpenProcess:
            print(f"[-] Could not open process. Try running as Administrator.")
            return False

    def _read_byte(self, offset: int) -> int:
        try:
            return self.pm.read_uchar(self.naomi_base + offset)
        except Exception:
            return 0

    def _read_word(self, offset: int) -> int:
        try:
            return self.pm.read_ushort(self.naomi_base + offset)
        except Exception:
            return 0

    def _read_dword(self, offset: int) -> int:
        try:
            return self.pm.read_uint(self.naomi_base + offset)
        except Exception:
            return 0

    def _read_float(self, offset: int) -> float:
        try:
            return self.pm.read_float(self.naomi_base + offset)
        except Exception:
            return 0.0

    def auto_find_base(self) -> bool:
        """
        Scan process memory for the NAOMI RAM block by looking for
        known byte patterns at known offsets relative to each other.

        Strategy: scan all memory regions for a byte with value matching
        the in_match flag pattern, then validate surrounding offsets.
        This is a fallback — using a pointer chain from CE is better.
        """
        print("[*] Auto-scan not yet implemented.")
        print("[*] Run the Cheat Engine walkthrough (WALKTHROUGH.md) to find the base.")
        print("[*] Then set NAOMI_RAM_BASE in this script.")
        return False

    def read_char_state(self, char_offsets: dict) -> CharState:
        """Read a single character's state."""
        cs = CharState()
        cs.char_id = self._read_byte(char_offsets["id"])
        cs.char_name = CHAR_NAMES.get(cs.char_id, f"Unknown(0x{cs.char_id:02X})")
        cs.hp = self._read_byte(char_offsets["hp"])
        cs.hp_pct = round((cs.hp / FULL_HP) * 100, 1) if FULL_HP > 0 else 0

        # Extended fields (only available for point characters with full data)
        if "hp_recover" in char_offsets:
            cs.hp_recover = self._read_byte(char_offsets["hp_recover"])
        if "pos_x" in char_offsets:
            cs.pos_x = round(self._read_float(char_offsets["pos_x"]), 2)
            cs.pos_y = round(self._read_float(char_offsets["pos_y"]), 2)
        if "vel_x" in char_offsets:
            cs.vel_x = round(self._read_float(char_offsets["vel_x"]), 2)
            cs.vel_y = round(self._read_float(char_offsets["vel_y"]), 2)
        if "facing" in char_offsets:
            cs.facing_right = self._read_byte(char_offsets["facing"]) == 1

        return cs

    def read_state(self) -> MatchState:
        """Read complete match state."""
        state = MatchState()
        state.in_match = self._read_byte(OFF_IN_MATCH) == 1
        state.timer = self._read_byte(OFF_TIMER)
        state.stage = self._read_byte(OFF_STAGE)
        state.frame = self._read_dword(OFF_FRAME_COUNT)

        # Player 1
        state.p1.meter = self._read_byte(OFF_P1_METER)
        state.p1.active_char = self._read_byte(P1_CHARS[0]["base"])
        state.p1.chars = [self.read_char_state(c) for c in P1_CHARS]

        # Player 2
        state.p2.meter = self._read_byte(OFF_P2_METER)
        state.p2.active_char = self._read_byte(P2_CHARS[0]["base"])
        state.p2.chars = [self.read_char_state(c) for c in P2_CHARS]

        return state

    def detect_events(self, state: MatchState) -> list:
        """Detect match events by comparing to previous state."""
        events = []
        prev = self.prev_state

        if prev is None:
            self.prev_state = state
            return events

        # Match start
        if state.in_match and not prev.in_match:
            self.match_count += 1
            events.append({
                "event": "match_start",
                "match_num": self.match_count,
                "stage": state.stage,
                "p1_team": [c.char_name for c in state.p1.chars],
                "p2_team": [c.char_name for c in state.p2.chars],
            })

        # Match end
        if not state.in_match and prev.in_match:
            # Determine winner by who has HP remaining
            p1_total_hp = sum(c.hp for c in prev.p1.chars)
            p2_total_hp = sum(c.hp for c in prev.p2.chars)
            winner = "P1" if p1_total_hp > p2_total_hp else "P2"
            events.append({
                "event": "match_end",
                "match_num": self.match_count,
                "winner": winner,
                "p1_hp_remaining": [c.hp for c in prev.p1.chars],
                "p2_hp_remaining": [c.hp for c in prev.p2.chars],
                "time_remaining": prev.timer,
            })

        # Character KO
        if state.in_match and prev.in_match:
            for i, (cur, prv) in enumerate(zip(state.p1.chars, prev.p1.chars)):
                if prv.hp > 0 and cur.hp == 0:
                    events.append({"event": "ko", "player": "P1", "char_slot": i, "char": prv.char_name})
            for i, (cur, prv) in enumerate(zip(state.p2.chars, prev.p2.chars)):
                if prv.hp > 0 and cur.hp == 0:
                    events.append({"event": "ko", "player": "P2", "char_slot": i, "char": prv.char_name})

        self.prev_state = state
        return events


# =============================================================
# OUTPUT FORMATTERS
# =============================================================

def format_hp_bar(hp: int, width: int = 20) -> str:
    """ASCII health bar."""
    filled = int((hp / FULL_HP) * width)
    return f"[{'#' * filled}{'.' * (width - filled)}]"


def print_console(state: MatchState, events: list):
    """Pretty-print to console."""
    for event in events:
        if event["event"] == "match_start":
            team1 = " / ".join(event["p1_team"])
            team2 = " / ".join(event["p2_team"])
            print(f"\n{'='*60}")
            print(f"  MATCH {event['match_num']} START — Stage {event['stage']}")
            print(f"  P1: {team1}")
            print(f"  P2: {team2}")
            print(f"{'='*60}")
        elif event["event"] == "match_end":
            print(f"\n  >>> {event['winner']} WINS! (T:{event['time_remaining']}) <<<\n")
        elif event["event"] == "ko":
            print(f"  ** KO: {event['player']} {event['char']} eliminated! **")

    if state.in_match:
        p1c = state.p1.chars
        p2c = state.p2.chars
        p1_point = p1c[state.p1.active_char] if state.p1.active_char < 3 else p1c[0]
        p2_point = p2c[state.p2.active_char] if state.p2.active_char < 3 else p2c[0]

        # Compact one-line display
        line = (
            f"T:{state.timer:02d} | "
            f"P1 {p1_point.char_name:>12s} "
            f"{format_hp_bar(p1c[0].hp, 10)} {p1c[0].hp:3d} "
            f"{format_hp_bar(p1c[1].hp, 10)} {p1c[1].hp:3d} "
            f"{format_hp_bar(p1c[2].hp, 10)} {p1c[2].hp:3d} "
            f"M:{state.p1.meter} | "
            f"P2 {p2_point.char_name:>12s} "
            f"{format_hp_bar(p2c[0].hp, 10)} {p2c[0].hp:3d} "
            f"{format_hp_bar(p2c[1].hp, 10)} {p2c[1].hp:3d} "
            f"{format_hp_bar(p2c[2].hp, 10)} {p2c[2].hp:3d} "
            f"M:{state.p2.meter}"
        )
        # Overwrite same line for clean display
        print(f"\r{line}", end="", flush=True)


def print_json(state: MatchState, events: list):
    """Output as JSON lines (for piping to ranking server)."""
    for event in events:
        print(json.dumps(event))
    if state.in_match:
        print(json.dumps({
            "type": "state",
            "timer": state.timer,
            "frame": state.frame,
            "p1": {
                "meter": state.p1.meter,
                "active": state.p1.active_char,
                "chars": [{"name": c.char_name, "hp": c.hp, "hp_pct": c.hp_pct}
                          for c in state.p1.chars]
            },
            "p2": {
                "meter": state.p2.meter,
                "active": state.p2.active_char,
                "chars": [{"name": c.char_name, "hp": c.hp, "hp_pct": c.hp_pct}
                          for c in state.p2.chars]
            },
        }))


# =============================================================
# MAIN
# =============================================================

def main():
    print("=" * 50)
    print("  MVC2 Telemetry Reader v0.1")
    print("  GP-RETRO-ONLINE Project")
    print("=" * 50)

    reader = MVC2Reader(PROCESS_NAME, NAOMI_RAM_BASE)

    if not reader.connect():
        sys.exit(1)

    if reader.naomi_base == 0:
        print()
        print("[!] NAOMI_RAM_BASE is not set.")
        print("[!] Follow WALKTHROUGH.md to find it with Cheat Engine.")
        print("[!] Then update NAOMI_RAM_BASE in this script.")
        if not reader.auto_find_base():
            sys.exit(1)

    print(f"[+] NAOMI RAM base: 0x{reader.naomi_base:X}")
    print(f"[+] Polling at {POLL_HZ} Hz")
    print(f"[+] Output mode: {OUTPUT_MODE}")
    print(f"[+] Validating addresses...")

    # Quick validation
    in_match = reader._read_byte(OFF_IN_MATCH)
    timer = reader._read_byte(OFF_TIMER)
    p1_hp = reader._read_byte(P1_CHARS[0]["hp"])
    print(f"    in_match={in_match}, timer={timer}, p1_hp={p1_hp}")

    if p1_hp == 0 and timer == 0 and in_match == 0:
        print("[!] All values are 0 — base address may be wrong.")
        print("[!] Make sure MVC2 is loaded (not in collection menu).")
    else:
        print("[+] Values look valid!")

    print()
    print("Reading game state... (Ctrl+C to stop)")
    print()

    output_fn = print_json if OUTPUT_MODE == "json" else print_console
    interval = 1.0 / POLL_HZ

    try:
        while True:
            t_start = time.perf_counter()

            state = reader.read_state()
            events = reader.detect_events(state)
            output_fn(state, events)

            # Precise timing
            elapsed = time.perf_counter() - t_start
            sleep_time = interval - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    except KeyboardInterrupt:
        print("\n\n[+] Stopped. Matches observed:", reader.match_count)
    except pymem.exception.MemoryReadError:
        print("\n[-] Memory read error — game may have closed.")


if __name__ == "__main__":
    main()
