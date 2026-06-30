"""
MVC2 NAOMI RAM Base Finder
===========================
Scans MarvelVsCapcomFightingCollection.exe memory to find the NAOMI RAM base.

Strategy:
  1. Enumerate all memory regions in the process
  2. Look for regions ~32MB (NAOMI RAM size) that are readable
  3. For each candidate, check if known offsets contain expected values:
     - offset 0x289624 (in_match): should be 0 or 1
     - offset 0x289630 (timer): should be 0-99
     - offset 0x268760 (P1 HP): should be 0x00-0x90
     - offset 0x268D04 (P2 HP): should be 0x00-0x90
  4. If multiple values match simultaneously, we found the base

Run this while MVC2 is loaded (menu or match).
"""

import sys
import ctypes
import ctypes.wintypes
import struct

try:
    import pymem
    import pymem.process
    import pymem.memory
except ImportError:
    print("ERROR: pip install pymem")
    sys.exit(1)

PROCESS_NAME = "MarvelVsCapcomFightingCollection.exe"

# Known NAOMI RAM offsets
OFF_IN_MATCH = 0x289624
OFF_TIMER    = 0x289630
OFF_STAGE    = 0x289638
OFF_P1_HP    = 0x268760
OFF_P2_HP    = 0x268D04
OFF_P1_METER = 0x28964A
OFF_P2_METER = 0x28964B
OFF_P1_CHAR  = 0x268341
OFF_P2_CHAR  = 0x2688E5
OFF_P1_ACTIVE = 0x268340

FULL_HP = 0x90  # 144

# Windows constants
MEM_COMMIT = 0x1000
PAGE_READWRITE = 0x04
PAGE_READONLY = 0x02
PAGE_EXECUTE_READ = 0x20
PAGE_EXECUTE_READWRITE = 0x40
PAGE_WRITECOPY = 0x08
PAGE_EXECUTE_WRITECOPY = 0x80

READABLE_PROTECTIONS = (
    PAGE_READWRITE, PAGE_READONLY, PAGE_EXECUTE_READ,
    PAGE_EXECUTE_READWRITE, PAGE_WRITECOPY, PAGE_EXECUTE_WRITECOPY,
)

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


def validate_base(pm, base):
    """Check if a candidate base address has valid MVC2 data."""
    try:
        in_match = pm.read_uchar(base + OFF_IN_MATCH)
        timer = pm.read_uchar(base + OFF_TIMER)
        p1_hp = pm.read_uchar(base + OFF_P1_HP)
        p2_hp = pm.read_uchar(base + OFF_P2_HP)
        p1_meter = pm.read_uchar(base + OFF_P1_METER)
        p2_meter = pm.read_uchar(base + OFF_P2_METER)
        p1_char = pm.read_uchar(base + OFF_P1_CHAR)
        p2_char = pm.read_uchar(base + OFF_P2_CHAR)
        stage = pm.read_uchar(base + OFF_STAGE)
    except Exception:
        return None

    # Validation rules
    score = 0
    details = {}

    # in_match must be 0 or 1
    if in_match in (0, 1):
        score += 2
        details["in_match"] = in_match

    # timer must be 0-99
    if 0 <= timer <= 99:
        score += 2
        details["timer"] = timer

    # HP values must be 0-144 (0x90)
    if 0 <= p1_hp <= FULL_HP:
        score += 1
        details["p1_hp"] = f"0x{p1_hp:02X} ({p1_hp})"
    if 0 <= p2_hp <= FULL_HP:
        score += 1
        details["p2_hp"] = f"0x{p2_hp:02X} ({p2_hp})"

    # Meter must be 0-5
    if 0 <= p1_meter <= 5:
        score += 1
        details["p1_meter"] = p1_meter
    if 0 <= p2_meter <= 5:
        score += 1
        details["p2_meter"] = p2_meter

    # Character IDs must be in valid range (0x00-0x3A for MVC2's 56 chars)
    if 0 <= p1_char <= 0x3A:
        score += 1
        details["p1_char"] = f"0x{p1_char:02X}"
    if 0 <= p2_char <= 0x3A:
        score += 1
        details["p2_char"] = f"0x{p2_char:02X}"

    # Stage must be reasonable (0-30ish)
    if 0 <= stage <= 30:
        score += 1
        details["stage"] = stage

    # Strong signal: if in a match, HP should be > 0 and timer > 0
    if in_match == 1 and timer > 0 and p1_hp > 0 and p2_hp > 0:
        score += 5  # Big bonus for consistent match state

    # Strong signal: if NOT in match, timer is often 0 or 99
    if in_match == 0 and timer in (0, 99):
        score += 3

    return {"score": score, "details": details, "base": base}


def scan_for_base(pm):
    """Scan all readable memory regions for the NAOMI RAM base."""
    print(f"[*] Scanning process memory for NAOMI RAM...")
    print(f"[*] Looking for valid MVC2 game state at known offsets...")
    print()

    kernel32 = ctypes.windll.kernel32
    handle = pm.process_handle

    candidates = []
    address = 0
    max_address = 0x7FFFFFFFFFFF  # User-space limit for 64-bit

    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)

    regions_checked = 0
    total_readable = 0

    while address < max_address:
        result = kernel32.VirtualQueryEx(
            handle, ctypes.c_ulonglong(address), ctypes.byref(mbi), mbi_size
        )
        if result == 0:
            break

        region_base = mbi.BaseAddress
        region_size = mbi.RegionSize

        # Only check committed, readable regions
        if (mbi.State == MEM_COMMIT and
            mbi.Protect in READABLE_PROTECTIONS and
            region_size >= 0x2A0000):  # At least large enough for our highest offset (~2.7MB)

            total_readable += region_size
            regions_checked += 1

            # Try this region's base as a NAOMI RAM base
            result = validate_base(pm, region_base)
            if result and result["score"] >= 8:
                candidates.append(result)

            # Also try offsets within large regions (NAOMI RAM might start mid-region)
            if region_size >= 0x2000000:  # 32MB+ regions
                # Try aligned offsets
                for offset in range(0, min(region_size, 0x10000000), 0x10000):
                    candidate_base = region_base + offset
                    if candidate_base + OFF_TIMER + 1 > region_base + region_size:
                        break
                    result = validate_base(pm, candidate_base)
                    if result and result["score"] >= 8:
                        candidates.append(result)

        address = region_base + region_size

    print(f"[*] Checked {regions_checked} memory regions ({total_readable / (1024*1024):.1f} MB readable)")
    return candidates


def main():
    print("=" * 55)
    print("  MVC2 NAOMI RAM Base Finder")
    print("  Target: MarvelVsCapcomFightingCollection.exe")
    print("=" * 55)
    print()

    try:
        pm = pymem.Pymem(PROCESS_NAME)
        print(f"[+] Attached to {PROCESS_NAME} (PID: {pm.process_id})")
    except pymem.exception.ProcessNotFound:
        print(f"[-] Process not found. Is the game running?")
        sys.exit(1)
    except pymem.exception.CouldNotOpenProcess:
        print(f"[-] Could not open process. Run as Administrator.")
        sys.exit(1)

    print()
    candidates = scan_for_base(pm)

    if not candidates:
        print("\n[-] No candidates found with score >= 8")
        print("[!] Make sure MVC2 is loaded (not just the collection menu)")
        print("[!] Try starting a match first, then run this script again")
        return

    # Sort by score descending
    candidates.sort(key=lambda x: x["score"], reverse=True)

    # Deduplicate (nearby addresses often score similarly)
    seen = set()
    unique = []
    for c in candidates:
        # Round to nearest 64KB for dedup
        key = c["base"] >> 16
        if key not in seen:
            seen.add(key)
            unique.append(c)

    print(f"\n[+] Found {len(unique)} candidate(s):\n")
    for i, c in enumerate(unique[:10]):  # Show top 10
        print(f"  #{i+1} Score: {c['score']:2d}  Base: 0x{c['base']:012X}")
        for k, v in c["details"].items():
            print(f"       {k}: {v}")
        print()

    # Best candidate
    best = unique[0]
    print(f"{'='*55}")
    print(f"  BEST MATCH: NAOMI_RAM_BASE = 0x{best['base']:X}")
    print(f"  Score: {best['score']}")
    print(f"{'='*55}")
    print()

    # Full validation of best candidate
    base = best["base"]
    print(f"[*] Full validation at base 0x{base:X}:")
    try:
        in_match = pm.read_uchar(base + OFF_IN_MATCH)
        timer = pm.read_uchar(base + OFF_TIMER)
        p1_hp = pm.read_uchar(base + OFF_P1_HP)
        p2_hp = pm.read_uchar(base + OFF_P2_HP)
        p1_meter = pm.read_uchar(base + OFF_P1_METER)
        p2_meter = pm.read_uchar(base + OFF_P2_METER)
        p1_char = pm.read_uchar(base + OFF_P1_CHAR)
        p2_char = pm.read_uchar(base + OFF_P2_CHAR)
        p1_active = pm.read_uchar(base + OFF_P1_ACTIVE)
        stage = pm.read_uchar(base + OFF_STAGE)

        print(f"    in_match:  {in_match}")
        print(f"    timer:     {timer}")
        print(f"    stage:     {stage}")
        print(f"    p1_hp:     0x{p1_hp:02X} ({p1_hp}/{FULL_HP})")
        print(f"    p2_hp:     0x{p2_hp:02X} ({p2_hp}/{FULL_HP})")
        print(f"    p1_meter:  {p1_meter}")
        print(f"    p2_meter:  {p2_meter}")
        print(f"    p1_char:   0x{p1_char:02X}")
        print(f"    p2_char:   0x{p2_char:02X}")
        print(f"    p1_active: {p1_active}")

        # Try reading position as float
        p1_x = pm.read_float(base + 0x268374)
        p1_y = pm.read_float(base + 0x268378)
        print(f"    p1_pos:    ({p1_x:.1f}, {p1_y:.1f})")

    except Exception as e:
        print(f"    Error: {e}")

    print()
    print(f"  >>> Update mvc2_reader.py with:")
    print(f'  PROCESS_NAME = "{PROCESS_NAME}"')
    print(f"  NAOMI_RAM_BASE = 0x{base:X}")


if __name__ == "__main__":
    main()
