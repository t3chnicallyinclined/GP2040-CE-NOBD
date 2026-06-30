"""
MVC2 NAOMI RAM Base Finder v3
==============================
Looks for the HP pattern: two bytes at KNOWN relative offsets that BOTH
represent health (0x01-0x90 range), where at least one DECREASED between
snapshots (you hit someone).

The key insight: P1_HP at offset 0x268760 and P2_HP at offset 0x268D04
are separated by exactly 0x5A4 bytes. Finding two bytes at that exact
spacing, both in HP range, with one decreasing = extremely strong signal.

HIT THE DUMMY BETWEEN SNAPSHOTS!
"""
import sys
import time
import ctypes
import ctypes.wintypes

try:
    import pymem
except ImportError:
    print("pip install pymem")
    sys.exit(1)

PROCESS_NAME = "MarvelVsCapcomFightingCollection.exe"

# The separation between P1 char1 HP and P2 char1 HP
HP_SEPARATION = 0x268D04 - 0x268760  # = 0x5A4

# Offsets to validate once we have a candidate
OFF_IN_MATCH = 0x289624
OFF_TIMER    = 0x289630
OFF_P1_HP    = 0x268760
OFF_P2_HP    = 0x268D04
OFF_P1_METER = 0x28964A
OFF_P1_CHAR  = 0x268341
OFF_P2_CHAR  = 0x2688E5
OFF_P1_POSX  = 0x268374
OFF_P1_POSY  = 0x268378

FULL_HP = 0x90

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
READABLE_PROTECTIONS = (0x02, 0x04, 0x08, 0x20, 0x40, 0x80)

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_ulonglong),
        ("AllocationBase", ctypes.c_ulonglong),
        ("AllocationProtect", ctypes.wintypes.DWORD),
        ("__alignment1", ctypes.wintypes.DWORD),
        ("RegionSize", ctypes.c_ulonglong),
        ("State", ctypes.wintypes.DWORD),
        ("Protect", ctypes.wintypes.DWORD),
        ("Type", ctypes.wintypes.DWORD),
        ("__alignment2", ctypes.wintypes.DWORD),
    ]

def get_regions(pm, min_size=0x300000):
    kernel32 = ctypes.windll.kernel32
    regions = []
    address = 0
    mbi = MEMORY_BASIC_INFORMATION()
    while address < 0x7FFFFFFFFFFF:
        if kernel32.VirtualQueryEx(pm.process_handle, ctypes.c_ulonglong(address), ctypes.byref(mbi), ctypes.sizeof(mbi)) == 0:
            break
        if mbi.State == MEM_COMMIT and mbi.Protect in READABLE_PROTECTIONS and mbi.RegionSize >= min_size:
            regions.append((mbi.BaseAddress, mbi.RegionSize))
        address = mbi.BaseAddress + mbi.RegionSize
    return regions

def read_safe(pm, addr, size):
    try:
        return pm.read_bytes(addr, size)
    except:
        return None

def validate_full(pm, base):
    """Full validation with strict checks."""
    try:
        in_match = pm.read_uchar(base + OFF_IN_MATCH)
        timer = pm.read_uchar(base + OFF_TIMER)
        p1_hp = pm.read_uchar(base + OFF_P1_HP)
        p2_hp = pm.read_uchar(base + OFF_P2_HP)
        p1_meter = pm.read_uchar(base + OFF_P1_METER)
        p1_char = pm.read_uchar(base + OFF_P1_CHAR)
        p2_char = pm.read_uchar(base + OFF_P2_CHAR)
    except:
        return None

    # STRICT: in training mode, in_match should be 1
    if in_match not in (0, 1): return None
    if p1_hp > FULL_HP: return None
    if p2_hp > FULL_HP: return None
    if p1_meter > 5: return None
    if p1_char > 0x3A: return None
    if p2_char > 0x3A: return None

    # MUST have at least one non-zero HP (we're in a match)
    if in_match == 1 and p1_hp == 0 and p2_hp == 0: return None

    # Position sanity (floats should be reasonable game coordinates)
    try:
        import struct
        px_bytes = pm.read_bytes(base + OFF_P1_POSX, 4)
        py_bytes = pm.read_bytes(base + OFF_P1_POSY, 4)
        px = struct.unpack('<f', px_bytes)[0]
        py = struct.unpack('<f', py_bytes)[0]
        # MVC2 screen coords are roughly -200 to +600 range
        pos_valid = (-1000 < px < 1000 and -500 < py < 1000)
    except:
        px, py = 0.0, 0.0
        pos_valid = False

    return {
        "in_match": in_match,
        "timer": timer,
        "p1_hp": p1_hp,
        "p2_hp": p2_hp,
        "p1_meter": p1_meter,
        "p1_char": CHAR_NAMES.get(p1_char, f"?{p1_char:02X}"),
        "p2_char": CHAR_NAMES.get(p2_char, f"?{p2_char:02X}"),
        "p1_pos": (round(px, 1), round(py, 1)),
        "pos_valid": pos_valid,
    }


def main():
    print("=" * 60)
    print("  MVC2 NAOMI RAM Base Finder v3 (HP Pattern)")
    print("  HIT THE DUMMY between snapshots!")
    print("=" * 60)
    print()

    pm = pymem.Pymem(PROCESS_NAME)
    print(f"[+] Attached to PID {pm.process_id}")

    regions = get_regions(pm)
    print(f"[+] {len(regions)} readable regions >= 3MB")
    total_mb = sum(r[1] for r in regions) / (1024*1024)
    print(f"[+] Total: {total_mb:.0f} MB")

    # Snapshot 1
    print("\n[*] Snapshot 1...")
    snap1 = {}
    for base, size in regions:
        chunk = 1024 * 1024
        for off in range(0, size, chunk):
            addr = base + off
            rsize = min(chunk, size - off)
            data = read_safe(pm, addr, rsize)
            if data:
                snap1[addr] = data

    print(f"[+] {len(snap1)} chunks captured")
    print("\n>>> HIT THE DUMMY NOW! Waiting 3 seconds... <<<\n")
    time.sleep(3)

    # Snapshot 2
    print("[*] Snapshot 2...")
    snap2 = {}
    for addr in snap1:
        data = read_safe(pm, addr, len(snap1[addr]))
        if data:
            snap2[addr] = data

    # Strategy: find any byte that decreased (HP damage) in range 1-0x90,
    # then check if there's a valid HP value 0x5A4 bytes later
    print("[*] Scanning for HP-like changes with P1/P2 separation pattern...")

    results = []
    for addr in snap1:
        if addr not in snap2:
            continue
        d1 = snap1[addr]
        d2 = snap2[addr]
        sz = len(d1)

        for i in range(sz):
            b1 = d1[i]
            b2 = d2[i]
            # Look for a byte that DECREASED and is in HP range
            if b1 == b2: continue
            if not (1 <= b2 < b1 <= FULL_HP): continue

            # This byte decreased in HP range. Could be P2 HP being hit.
            # Check if P1 HP (0x5A4 bytes earlier) is also valid
            p1_hp_offset_in_chunk = i - HP_SEPARATION
            if 0 <= p1_hp_offset_in_chunk < sz:
                p1_val = d2[p1_hp_offset_in_chunk]
                if 1 <= p1_val <= FULL_HP:
                    # Found a pair! Calculate base
                    # This byte = P2 HP at offset OFF_P2_HP from base
                    candidate_p2_addr = addr + i
                    candidate_base = candidate_p2_addr - OFF_P2_HP

                    result = validate_full(pm, candidate_base)
                    if result:
                        result["base"] = candidate_base
                        result["hp_change"] = f"{b1} -> {b2}"
                        results.append(result)

            # Also check if THIS byte is P1 HP (P2 should be 0x5A4 later)
            p2_hp_offset_in_chunk = i + HP_SEPARATION
            if 0 <= p2_hp_offset_in_chunk < sz:
                p2_val = d2[p2_hp_offset_in_chunk]
                if 0 <= p2_val <= FULL_HP:
                    candidate_p1_addr = addr + i
                    candidate_base = candidate_p1_addr - OFF_P1_HP

                    result = validate_full(pm, candidate_base)
                    if result:
                        result["base"] = candidate_base
                        result["hp_change"] = f"{b1} -> {b2}"
                        results.append(result)

    # Dedup
    seen = set()
    unique = []
    for r in results:
        if r["base"] not in seen:
            seen.add(r["base"])
            unique.append(r)

    if unique:
        print(f"\n[+] Found {len(unique)} validated candidate(s)!\n")
        for i, r in enumerate(unique[:10]):
            pos_marker = " <-- POSITION VALID" if r["pos_valid"] else ""
            print(f"  #{i+1} Base: 0x{r['base']:012X}{pos_marker}")
            print(f"       match={r['in_match']} T={r['timer']} HP_change={r['hp_change']}")
            print(f"       P1={r['p1_char']} HP={r['p1_hp']} | P2={r['p2_char']} HP={r['p2_hp']}")
            print(f"       Meter={r['p1_meter']} Pos=({r['p1_pos'][0]}, {r['p1_pos'][1]})")
            print()

        # Live polling on best candidate(s) with position valid
        best = next((r for r in unique if r["pos_valid"]), unique[0])
        base = best["base"]
        print(f"{'='*60}")
        print(f"  NAOMI_RAM_BASE = 0x{base:X}")
        print(f"{'='*60}")
        print(f"\n[*] Live polling (move around + hit dummy)...")
        for i in range(8):
            r = validate_full(pm, base)
            if r:
                print(f"  [{i}] T={r['timer']:2d} P1={r['p1_char']:>10s} HP={r['p1_hp']:3d} "
                      f"P2={r['p2_char']:>10s} HP={r['p2_hp']:3d} "
                      f"Pos=({r['p1_pos'][0]:6.1f},{r['p1_pos'][1]:6.1f})")
            time.sleep(1)
    else:
        print("\n[-] No candidates found.")
        print("[!] Make sure you HIT THE DUMMY so HP changes!")
        print("[!] If in training, dummy HP may auto-recover -- try hitting harder")

if __name__ == "__main__":
    main()
