"""
MVC2 NAOMI RAM Base Finder v4 — Full HP Signature
===================================================
Scans for the UNIQUE pattern of a fresh MVC2 match:
  6 health bytes all = 0x90 at EXACT known spacing.

P1C1 HP: base + 0x268760
P1C2 HP: base + 0x2692A8  (+0xB48 from P1C1)
P1C3 HP: base + 0x269DF0  (+0xB48 from P1C2)
P2C1 HP: base + 0x268D04  (+0x5A4 from P1C1)
P2C2 HP: base + 0x26984C  (+0xB48 from P2C1)
P2C3 HP: base + 0x26A394  (+0xB48 from P2C2)

Finding 6 bytes all = 0x90 at these exact separations is
extremely unlikely by chance. This is a fingerprint.

RUN THIS RIGHT WHEN A MATCH STARTS (before anyone takes damage).
"""
import sys
import time
import ctypes
import ctypes.wintypes
import struct

try:
    import pymem
except ImportError:
    print("pip install pymem")
    sys.exit(1)

PROCESS_NAME = "MarvelVsCapcomFightingCollection.exe"

# HP offsets from NAOMI base
HP_OFFSETS = [
    0x268760,  # P1 char 1
    0x2692A8,  # P1 char 2
    0x269DF0,  # P1 char 3
    0x268D04,  # P2 char 1
    0x26984C,  # P2 char 2
    0x26A394,  # P2 char 3
]

# Relative to P1C1 HP offset
HP_DELTAS = [off - 0x268760 for off in HP_OFFSETS]
# = [0, 0xB48, 0x1690, 0x5A4, 0x10EC, 0x1C34]

FULL_HP = 0x90

# Other validation offsets
OFF_IN_MATCH = 0x289624
OFF_TIMER    = 0x289630
OFF_P1_METER = 0x28964A
OFF_P1_CHAR  = 0x268341
OFF_P2_CHAR  = 0x2688E5
OFF_P1_POSX  = 0x268374

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

MEM_COMMIT = 0x1000
READABLE = (0x02, 0x04, 0x08, 0x20, 0x40, 0x80)

class MBI(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_ulonglong),
        ("AllocationBase", ctypes.c_ulonglong),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("__a1", ctypes.wintypes.DWORD),
        ("RegionSize", ctypes.c_ulonglong),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
        ("__a2", ctypes.wintypes.DWORD),
    ]

def get_regions(pm):
    k32 = ctypes.windll.kernel32
    regions = []
    addr = 0
    mbi = MBI()
    while addr < 0x7FFFFFFFFFFF:
        if k32.VirtualQueryEx(pm.process_handle, ctypes.c_ulonglong(addr), ctypes.byref(mbi), ctypes.sizeof(mbi)) == 0:
            break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE and mbi.RegionSize >= 0x2A0000:
            regions.append((mbi.BaseAddress, mbi.RegionSize))
        addr = mbi.BaseAddress + mbi.RegionSize
    return regions

def main():
    print("=" * 60)
    print("  MVC2 NAOMI RAM Base Finder v4")
    print("  Run at START of match (all HP = full)")
    print("=" * 60)
    print()

    pm = pymem.Pymem(PROCESS_NAME)
    print(f"[+] PID {pm.process_id}")

    regions = get_regions(pm)
    total = sum(r[1] for r in regions)
    print(f"[+] {len(regions)} regions, {total/(1024*1024):.0f} MB")
    print()

    # First: scan ALL memory for bytes = 0x90, record their addresses
    print("[*] Scanning for 0x90 bytes (full HP marker)...")
    hp90_addrs = []

    for reg_base, reg_size in regions:
        chunk_size = 4 * 1024 * 1024  # 4MB chunks
        for chunk_off in range(0, reg_size, chunk_size):
            addr = reg_base + chunk_off
            rsize = min(chunk_size, reg_size - chunk_off)
            try:
                data = pm.read_bytes(addr, rsize)
            except:
                continue

            # Find all 0x90 bytes
            pos = 0
            while True:
                pos = data.find(b'\x90', pos)
                if pos == -1:
                    break
                hp90_addrs.append(addr + pos)
                pos += 1

    print(f"[+] Found {len(hp90_addrs)} instances of 0x90")

    # For each 0x90, assume it's P1C1 HP and check if the other 5 HP slots
    # are also 0x90 at the known offsets
    print("[*] Checking HP signature pattern (6x 0x90 at exact spacing)...")

    candidates = []
    hp90_set = set(hp90_addrs)

    for p1c1_hp_addr in hp90_addrs:
        # Check all 6 HP addresses relative to this being P1C1 HP
        all_match = True
        for delta in HP_DELTAS[1:]:  # Skip first (delta=0, already 0x90)
            if (p1c1_hp_addr + delta) not in hp90_set:
                all_match = False
                break

        if all_match:
            # All 6 HP bytes are 0x90! Calculate base
            candidate_base = p1c1_hp_addr - 0x268760

            # Validate other fields
            try:
                in_match = pm.read_uchar(candidate_base + OFF_IN_MATCH)
                timer = pm.read_uchar(candidate_base + OFF_TIMER)
                p1_meter = pm.read_uchar(candidate_base + OFF_P1_METER)
                p1_char = pm.read_uchar(candidate_base + OFF_P1_CHAR)
                p2_char = pm.read_uchar(candidate_base + OFF_P2_CHAR)

                # Strict validation
                if in_match not in (0, 1): continue
                if timer > 99: continue
                if p1_meter > 5: continue
                if p1_char > 0x3A: continue
                if p2_char > 0x3A: continue

                p1_name = CHAR_NAMES.get(p1_char, f"?{p1_char:02X}")
                p2_name = CHAR_NAMES.get(p2_char, f"?{p2_char:02X}")

                # Try reading position
                try:
                    px = struct.unpack('<f', pm.read_bytes(candidate_base + OFF_P1_POSX, 4))[0]
                except:
                    px = 0.0

                candidates.append({
                    "base": candidate_base,
                    "in_match": in_match,
                    "timer": timer,
                    "p1_meter": p1_meter,
                    "p1_char": p1_name,
                    "p2_char": p2_name,
                    "p1_x": px,
                    "strong": in_match == 1 and timer > 0,
                })
            except:
                continue

    if not candidates:
        print("\n[-] No candidates with 6x 0x90 HP signature found.")
        print("[!] Are all characters at full health? (start of round)")
        print("[!] Try running right as 'FIGHT!' appears on screen")
        return

    # Sort: strong matches first, then by in_match=1
    candidates.sort(key=lambda c: (c["strong"], c["in_match"], c["timer"]), reverse=True)

    # Dedup
    seen = set()
    unique = []
    for c in candidates:
        if c["base"] not in seen:
            seen.add(c["base"])
            unique.append(c)

    print(f"\n[+] {len(unique)} candidate(s) with full HP signature!\n")
    for i, c in enumerate(unique[:15]):
        marker = " <<<< STRONG" if c["strong"] else ""
        print(f"  #{i+1} Base: 0x{c['base']:012X}{marker}")
        print(f"       match={c['in_match']} T={c['timer']} M={c['p1_meter']}")
        print(f"       P1={c['p1_char']} P2={c['p2_char']} PosX={c['p1_x']:.1f}")
        print()

    best = unique[0]
    base = best["base"]
    print(f"{'='*60}")
    print(f"  NAOMI_RAM_BASE = 0x{base:X}")
    print(f"{'='*60}")

    # Live poll
    print(f"\n[*] Live polling -- PLAY THE GAME, hit someone!")
    for i in range(10):
        try:
            in_m = pm.read_uchar(base + OFF_IN_MATCH)
            t = pm.read_uchar(base + OFF_TIMER)
            h1 = pm.read_uchar(base + 0x268760)
            h2 = pm.read_uchar(base + 0x268D04)
            m1 = pm.read_uchar(base + OFF_P1_METER)
            px = struct.unpack('<f', pm.read_bytes(base + OFF_P1_POSX, 4))[0]
            print(f"  [{i}] match={in_m} T={t:2d} P1_HP={h1:3d} P2_HP={h2:3d} M={m1} X={px:.1f}")
        except Exception as e:
            print(f"  [{i}] error: {e}")
        time.sleep(1)

if __name__ == "__main__":
    main()
