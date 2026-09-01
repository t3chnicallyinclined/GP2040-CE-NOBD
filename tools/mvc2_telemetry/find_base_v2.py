"""
MVC2 NAOMI RAM Base Finder v2 — Delta-based
=============================================
Instead of pattern matching static values, this finds memory that CHANGES.

Strategy:
  1. Take two memory snapshots 2 seconds apart while the game is being played
  2. Find bytes that changed between snapshots
  3. Among changed bytes, look for ones where the delta matches timer countdown
  4. Calculate base from the timer address

This REQUIRES the game to be actively playing (not paused).
"""
import sys
import time
import ctypes
import ctypes.wintypes

try:
    import pymem
    import pymem.memory
except ImportError:
    print("pip install pymem")
    sys.exit(1)

PROCESS_NAME = "MarvelVsCapcomFightingCollection.exe"

# Known timer offset
OFF_TIMER = 0x289630
OFF_IN_MATCH = 0x289624
OFF_P1_HP = 0x268760
OFF_P2_HP = 0x268D04
OFF_P1_METER = 0x28964A
OFF_P1_CHAR = 0x268341
OFF_P2_CHAR = 0x2688E5

FULL_HP = 0x90

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


def get_readable_regions(pm):
    """Enumerate all readable committed memory regions."""
    kernel32 = ctypes.windll.kernel32
    handle = pm.process_handle
    regions = []
    address = 0
    max_address = 0x7FFFFFFFFFFF
    mbi = MEMORY_BASIC_INFORMATION()
    mbi_size = ctypes.sizeof(mbi)

    while address < max_address:
        result = kernel32.VirtualQueryEx(
            handle, ctypes.c_ulonglong(address), ctypes.byref(mbi), mbi_size
        )
        if result == 0:
            break
        if (mbi.State == MEM_COMMIT and
            mbi.Protect in READABLE_PROTECTIONS and
            mbi.RegionSize >= 0x300000):  # Only regions big enough to contain our offsets
            regions.append((mbi.BaseAddress, mbi.RegionSize))
        address = mbi.BaseAddress + mbi.RegionSize

    return regions


def read_region_safe(pm, address, size):
    """Read a memory region, returning None on failure."""
    try:
        return pm.read_bytes(address, size)
    except Exception:
        return None


def validate_base(pm, base):
    """Thorough validation of a candidate base address."""
    try:
        in_match = pm.read_uchar(base + OFF_IN_MATCH)
        timer = pm.read_uchar(base + OFF_TIMER)
        p1_hp = pm.read_uchar(base + OFF_P1_HP)
        p2_hp = pm.read_uchar(base + OFF_P2_HP)
        p1_meter = pm.read_uchar(base + OFF_P1_METER)
        p1_char = pm.read_uchar(base + OFF_P1_CHAR)
        p2_char = pm.read_uchar(base + OFF_P2_CHAR)

        valid = True
        if in_match not in (0, 1): valid = False
        if timer > 99: valid = False
        if p1_hp > FULL_HP: valid = False
        if p2_hp > FULL_HP: valid = False
        if p1_meter > 5: valid = False
        if p1_char > 0x3A: valid = False

        return {
            "valid": valid,
            "in_match": in_match,
            "timer": timer,
            "p1_hp": p1_hp,
            "p2_hp": p2_hp,
            "p1_meter": p1_meter,
            "p1_char": p1_char,
            "p2_char": p2_char,
        }
    except:
        return {"valid": False}


def main():
    print("=" * 60)
    print("  MVC2 NAOMI RAM Base Finder v2 (Delta-based)")
    print("  MAKE SURE THE GAME IS ACTIVELY PLAYING (NOT PAUSED)")
    print("=" * 60)
    print()

    pm = pymem.Pymem(PROCESS_NAME)
    print(f"[+] Attached to PID {pm.process_id}")

    # Get all readable regions
    regions = get_readable_regions(pm)
    print(f"[+] Found {len(regions)} readable regions >= 3MB")

    total_mb = sum(r[1] for r in regions) / (1024*1024)
    print(f"[+] Total scannable: {total_mb:.0f} MB")
    print()
    print("[*] Taking snapshot 1...")

    # Read all regions
    snapshot1 = {}
    for base, size in regions:
        # Read in 1MB chunks to avoid huge allocations
        chunk_size = 1024 * 1024
        for offset in range(0, size, chunk_size):
            read_size = min(chunk_size, size - offset)
            addr = base + offset
            data = read_region_safe(pm, addr, read_size)
            if data:
                snapshot1[addr] = data

    print(f"[+] Snapshot 1: {len(snapshot1)} chunks")
    print("[*] Waiting 3 seconds... PLAY THE GAME!")
    time.sleep(3)

    print("[*] Taking snapshot 2...")
    snapshot2 = {}
    for addr in snapshot1:
        data = read_region_safe(pm, addr, len(snapshot1[addr]))
        if data:
            snapshot2[addr] = data

    print(f"[+] Snapshot 2: {len(snapshot2)} chunks")
    print()

    # Find timer candidates: bytes that decreased by 1-3 (timer ticks ~1/sec)
    print("[*] Searching for timer-like decreasing bytes...")
    timer_candidates = []

    for addr in snapshot1:
        if addr not in snapshot2:
            continue
        d1 = snapshot1[addr]
        d2 = snapshot2[addr]
        for i in range(len(d1)):
            b1 = d1[i]
            b2 = d2[i]
            # Timer should decrease by 1-5 in 3 seconds
            if b1 != b2 and 1 <= (b1 - b2) <= 5 and 10 <= b1 <= 99:
                candidate_timer_addr = addr + i
                candidate_base = candidate_timer_addr - OFF_TIMER
                # Quick sanity: base must be page-aligned-ish
                # and the HP offset must be readable
                result = validate_base(pm, candidate_base)
                if result["valid"]:
                    timer_candidates.append({
                        "timer_addr": candidate_timer_addr,
                        "base": candidate_base,
                        "timer_val_1": b1,
                        "timer_val_2": b2,
                        **result,
                    })

    if timer_candidates:
        print(f"\n[+] Found {len(timer_candidates)} timer candidates with valid bases!\n")
        # Deduplicate
        seen = set()
        unique = []
        for c in timer_candidates:
            if c["base"] not in seen:
                seen.add(c["base"])
                unique.append(c)

        for i, c in enumerate(unique[:5]):
            print(f"  #{i+1} Base: 0x{c['base']:012X}")
            print(f"       Timer: {c['timer_val_1']} -> {c['timer_val_2']} (addr 0x{c['timer_addr']:X})")
            print(f"       in_match={c['in_match']} P1_HP={c['p1_hp']} P2_HP={c['p2_hp']}")
            print(f"       P1_meter={c['p1_meter']} P1_char=0x{c['p1_char']:02X} P2_char=0x{c['p2_char']:02X}")
            print()

        best = unique[0]
        print(f"{'='*60}")
        print(f"  NAOMI_RAM_BASE = 0x{best['base']:X}")
        print(f"{'='*60}")

        # Live poll to confirm
        print(f"\n[*] Live polling to confirm (5 reads, 1s apart)...")
        base = best["base"]
        for i in range(5):
            r = validate_base(pm, base)
            if r["valid"]:
                print(f"  [{i}] T={r['timer']:2d} P1_HP={r['p1_hp']:3d} P2_HP={r['p2_hp']:3d} match={r['in_match']}")
            time.sleep(1)

    else:
        print("\n[-] No timer candidates found.")
        print("[!] Make sure you're IN A MATCH and actively playing (not paused).")
        print("[!] The timer must be counting down for this to work.")

        # Fallback: look for ANY changed bytes in interesting ranges
        print("\n[*] Fallback: scanning for any changed bytes...")
        changed_count = 0
        changed_regions = []
        for addr in snapshot1:
            if addr not in snapshot2:
                continue
            d1 = snapshot1[addr]
            d2 = snapshot2[addr]
            changes = sum(1 for i in range(len(d1)) if d1[i] != d2[i])
            if changes > 0:
                changed_count += changes
                changed_regions.append((addr, len(d1), changes))

        print(f"  Total changed bytes: {changed_count}")
        print(f"  Changed regions: {len(changed_regions)}")
        if changed_regions:
            changed_regions.sort(key=lambda x: x[2], reverse=True)
            print(f"\n  Top changed regions:")
            for addr, size, changes in changed_regions[:10]:
                print(f"    0x{addr:012X} size={size:,} changed={changes}")

if __name__ == "__main__":
    main()
