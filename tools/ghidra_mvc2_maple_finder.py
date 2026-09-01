# Ghidra Script: MvC2 Maple DMA Finder
# @author GP-RETRO
# @category Dreamcast
# @description Find Maple Bus DMA setup code in MvC2 1ST_READ.BIN
#
# Setup: Import 1ST_READ.BIN as Raw Binary, SuperH4:LE:32:default, base 0x8C010000
# Run: Script Manager > ghidra_mvc2_maple_finder.py
#
# What it finds:
#   - References to SB_MDSTAR (0xA05F6C04) — Maple DMA chain start
#   - References to SB_MDST (0xA05F6C18) — DMA trigger
#   - References to SB_MDTSEL (0xA05F6C10) — Vblank trigger select
#   - References to game state addresses (health, timer, meter)
#   - Labels everything for easy navigation

from ghidra.program.model.symbol import SourceType

# Dreamcast Holly/SB registers (P2 uncached area)
SB_REGISTERS = {
    0xA05F6C04: "SB_MDSTAR",    # Maple DMA start address
    0xA05F6C10: "SB_MDTSEL",    # DMA trigger select (0=CPU, 1=vblank)
    0xA05F6C14: "SB_MDEN",      # DMA enable
    0xA05F6C18: "SB_MDST",      # DMA start (write 1 to trigger)
    0xA05F6C80: "SB_MSYS",      # Maple system control
    0xA05F6C84: "SB_MST",       # Maple status
    0xA05F6C8C: "SB_MDAPRO",    # DMA address protection
}

# MvC2 game state addresses (physical, US version T1212N)
MVC2_ADDRESSES = {
    0x0C289624: "MVC2_in_match",
    0x0C289630: "MVC2_timer",
    0x0C289638: "MVC2_stage_id",
    0x0C28964A: "MVC2_p1_meter",
    0x0C28964B: "MVC2_p2_meter",
    0x0C268760: "MVC2_p1_char1_hp",
    0x0C2692A8: "MVC2_p1_char2_hp",
    0x0C269DF0: "MVC2_p1_char3_hp",
    0x0C268D04: "MVC2_p2_char1_hp",
    0x0C26984C: "MVC2_p2_char2_hp",
    0x0C26A394: "MVC2_p2_char3_hp",
    0x0C268341: "MVC2_p1_char1_id",
    0x0C268E89: "MVC2_p1_char2_id",
    0x0C2699D1: "MVC2_p1_char3_id",
    0x0C2688E5: "MVC2_p2_char1_id",
    0x0C2FB2BC: "MVC2_p1_cursor_col",
    0x0C2FB2BE: "MVC2_p1_cursor_row",
}

# Maple Bus controller function code
MAPLE_FUNC_CONTROLLER = 0x01000000

def search_for_constant(program, value, name):
    """Search for a 32-bit constant in the binary (as LE bytes in SH-4 literal pools)."""
    memory = program.getMemory()
    listing = program.getListing()
    results = []

    # SH-4 loads constants from literal pools: mov.l @(disp,PC), Rn
    # The constant is stored as 4 LE bytes somewhere after the instruction.
    b0 = (value >> 0)  & 0xFF
    b1 = (value >> 8)  & 0xFF
    b2 = (value >> 16) & 0xFF
    b3 = (value >> 24) & 0xFF
    pattern = bytes([b0, b1, b2, b3])

    addr = memory.getMinAddress()
    while addr is not None:
        found = memory.findBytes(addr, pattern, None, True, monitor)
        if found is None:
            break
        results.append(found)
        addr = found.add(1)

    return results


def main():
    program = currentProgram
    bookmark_mgr = program.getBookmarkManager()
    symbol_table = program.getSymbolTable()
    memory = program.getMemory()

    println("=" * 60)
    println("  GP-RETRO MvC2 Maple DMA Finder")
    println("=" * 60)

    # --- Search for SB register constants ---
    println("\n--- Maple DMA Registers ---")
    for addr_val, name in SB_REGISTERS.items():
        hits = search_for_constant(program, addr_val, name)
        if hits:
            println("  %s (0x%08X): %d references" % (name, addr_val, len(hits)))
            for hit in hits:
                bookmark_mgr.setBookmark(hit, "Analysis", "MapleDMA",
                    "%s constant (0x%08X)" % (name, addr_val))
                # Try to label the address
                try:
                    symbol_table.createLabel(hit, "lit_%s" % name, SourceType.ANALYSIS)
                except:
                    pass
                println("    @ %s" % str(hit))

                # Find what instruction references this literal pool entry.
                # SH-4 mov.l @(disp,PC),Rn has limited range (~1KB).
                # Search backwards for mov.l instructions that could reference this.
                # (This is approximate — Ghidra's auto-analysis does a better job)
        else:
            println("  %s (0x%08X): NOT FOUND" % (name, addr_val))

    # --- Search for MvC2 game state addresses ---
    println("\n--- MvC2 Game State Addresses ---")
    for addr_val, name in MVC2_ADDRESSES.items():
        hits = search_for_constant(program, addr_val, name)
        if hits:
            println("  %s (0x%08X): %d references" % (name, addr_val, len(hits)))
            for hit in hits[:5]:  # limit output
                bookmark_mgr.setBookmark(hit, "Analysis", "GameState",
                    "%s (0x%08X)" % (name, addr_val))
                try:
                    symbol_table.createLabel(hit, "lit_%s" % name, SourceType.ANALYSIS)
                except:
                    pass
                println("    @ %s" % str(hit))
            if len(hits) > 5:
                println("    ... and %d more" % (len(hits) - 5))
        else:
            println("  %s (0x%08X): NOT FOUND" % (name, addr_val))

    # --- Search for controller function code ---
    println("\n--- Maple Function Code (Controller = 0x01000000) ---")
    hits = search_for_constant(program, MAPLE_FUNC_CONTROLLER, "MFID_Controller")
    if hits:
        println("  Found %d references to 0x01000000" % len(hits))
        for hit in hits[:10]:
            bookmark_mgr.setBookmark(hit, "Analysis", "MapleFunc",
                "MFID_Controller (0x01000000)")
            println("    @ %s" % str(hit))
    else:
        println("  NOT FOUND (try searching manually)")

    # --- Search for Maple command byte patterns ---
    # CMD9 = 0x09 in the command header. In a DMA descriptor, the command header
    # word has cmd=9 in bits [7:0] (LE byte 0). Search for words where byte 0 = 0x09.
    # This is too broad for a byte search, but we can look for specific patterns.
    println("\n--- Summary ---")
    println("  Look at SB_MDSTAR references to find the DMA chain setup code.")
    println("  The function that writes to SB_MDSTAR is the Maple DMA trigger.")
    println("  Trace backwards from there to find the DMA chain buffer.")
    println("  The buffer contains CMD9 descriptors we need to extend.")
    println("")
    println("  Next steps:")
    println("  1. Double-click SB_MDSTAR bookmarks to navigate")
    println("  2. Find the function that writes to 0xA05F6C04")
    println("  3. That function sets up the DMA chain in RAM")
    println("  4. Find where CMD9 data (func=0x01000000) is written")
    println("  5. That's the injection point for telemetry")
    println("")
    println("  The health/timer addresses show where game state is updated.")
    println("  Cross-reference to find the main game loop.")
    println("=" * 60)


main()
