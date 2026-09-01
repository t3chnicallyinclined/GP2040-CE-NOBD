"""
Quick validation: read from top candidates multiple times to see which one
has values that change consistently with gameplay.
"""
import sys
import time

try:
    import pymem
except ImportError:
    print("pip install pymem")
    sys.exit(1)

PROCESS_NAME = "MarvelVsCapcomFightingCollection.exe"

# Offsets
OFF_IN_MATCH = 0x289624
OFF_TIMER    = 0x289630
OFF_STAGE    = 0x289638
OFF_P1_HP    = 0x268760
OFF_P2_HP    = 0x268D04
OFF_P1_METER = 0x28964A
OFF_P2_METER = 0x28964B
OFF_P1_CHAR  = 0x268341
OFF_P2_CHAR  = 0x2688E5

CHAR_NAMES = {
    0x00: "Ryu", 0x01: "Zangief", 0x02: "Guile", 0x03: "Morrigan",
    0x04: "Anakaris", 0x05: "Strider", 0x06: "Cyclops", 0x07: "Wolverine",
    0x08: "Psylocke", 0x09: "Iceman", 0x0A: "Rogue", 0x0B: "Cap America",
    0x0C: "Spider-Man", 0x0D: "Hulk", 0x0E: "Venom", 0x0F: "Dr. Doom",
    0x10: "Tron Bonne", 0x11: "Jill", 0x12: "Hayato", 0x13: "Ruby Heart",
    0x14: "SonSon", 0x15: "Amingo", 0x16: "Marrow", 0x17: "Cable",
    0x1B: "Chun-Li", 0x1C: "Megaman", 0x1D: "Roll", 0x1E: "Akuma",
    0x1F: "B.B. Hood", 0x20: "Felicia", 0x21: "Charlie", 0x22: "Sakura",
    0x23: "Dan", 0x24: "Cammy", 0x25: "Dhalsim", 0x26: "M. Bison",
    0x27: "Ken", 0x28: "Gambit", 0x29: "Juggernaut", 0x2A: "Storm",
    0x2B: "Sabretooth", 0x2C: "Magneto", 0x2D: "Shuma", 0x2E: "War Machine",
    0x2F: "Silver Samurai", 0x30: "Omega Red", 0x31: "Spiral",
    0x32: "Colossus", 0x33: "Iron Man", 0x34: "Sentinel", 0x35: "Blackheart",
    0x36: "Thanos", 0x37: "Jin", 0x38: "Cap Commando", 0x39: "BoneWolv",
    0x3A: "Servbot",
}

# Top candidates from scan
CANDIDATES = [
    0x43890000,
    0x7FFF7DA92000,
]

def read_state(pm, base):
    try:
        in_match = pm.read_uchar(base + OFF_IN_MATCH)
        timer = pm.read_uchar(base + OFF_TIMER)
        stage = pm.read_uchar(base + OFF_STAGE)
        p1_hp = pm.read_uchar(base + OFF_P1_HP)
        p2_hp = pm.read_uchar(base + OFF_P2_HP)
        p1_meter = pm.read_uchar(base + OFF_P1_METER)
        p2_meter = pm.read_uchar(base + OFF_P2_METER)
        p1_char = pm.read_uchar(base + OFF_P1_CHAR)
        p2_char = pm.read_uchar(base + OFF_P2_CHAR)
        return {
            "in_match": in_match,
            "timer": timer,
            "stage": stage,
            "p1_hp": p1_hp,
            "p2_hp": p2_hp,
            "p1_meter": p1_meter,
            "p2_meter": p2_meter,
            "p1_char": CHAR_NAMES.get(p1_char, f"?{p1_char:02X}"),
            "p2_char": CHAR_NAMES.get(p2_char, f"?{p2_char:02X}"),
        }
    except Exception as e:
        return {"error": str(e)}

def main():
    pm = pymem.Pymem(PROCESS_NAME)
    print(f"Attached to PID {pm.process_id}")
    print(f"Reading {len(CANDIDATES)} candidates, 5 samples each (1 sec apart)...\n")

    for base in CANDIDATES:
        print(f"=== Base: 0x{base:012X} ===")
        for i in range(5):
            s = read_state(pm, base)
            if "error" in s:
                print(f"  [{i}] ERROR: {s['error']}")
                break
            print(
                f"  [{i}] match={s['in_match']} T={s['timer']:2d} "
                f"P1={s['p1_char']:>12s} HP={s['p1_hp']:3d} M={s['p1_meter']} | "
                f"P2={s['p2_char']:>12s} HP={s['p2_hp']:3d} M={s['p2_meter']} "
                f"STG={s['stage']}"
            )
            time.sleep(1)
        print()

if __name__ == "__main__":
    main()
