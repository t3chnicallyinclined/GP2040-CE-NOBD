-- ============================================================
-- GP-RETRO MvC2 Maple DMA Probe
-- Flycast Lua Script
--
-- Purpose:
--   1. Read MvC2 game state every frame (health, timer, meter)
--   2. Walk Maple DMA descriptor chain (understand CMD9 structure)
--   3. Test extended CMD9 telemetry injection (experimental)
--
-- Usage:
--   1. Enable Lua in Flycast: Settings > Advanced > Lua Filename
--   2. Point to this file
--   3. Boot MvC2 (US version, T1212N)
--   4. Watch console output for game state + DMA chain dumps
--
-- To enable telemetry injection, set INJECT_TELEMETRY = true below.
-- This modifies the DMA chain in-memory to prove extended CMD9 works.
-- ============================================================

-- Configuration
local LOG_INTERVAL   = 60     -- Log every N frames (60 = 1/sec at 60fps)
local DUMP_DMA       = true   -- Dump Maple DMA chain on log interval
local INJECT_TELEMETRY = false -- Modify DMA chain (EXPERIMENTAL — set true to test)
local VERBOSE        = false  -- Extra debug output

-- ============================================================
-- MvC2 Known Memory Addresses (US version, DC physical)
-- Source: flycast-dojo mvsc2.lua + CodeBreaker codes + community RE
-- ============================================================
local ADDR = {
    -- Match state
    in_match    = 0x0C289624,  -- 1 = in match, 0 = menu/select
    timer       = 0x0C289630,  -- game timer (counts down)
    stage       = 0x0C289638,  -- stage ID

    -- P1 characters
    p1_meter    = 0x0C28964A,  -- super meter (0-5 bars)
    p1_char1_hp = 0x0C268760,  -- 0x90 = full, 0x00 = dead
    p1_char2_hp = 0x0C2692A8,
    p1_char3_hp = 0x0C269DF0,
    p1_char1_id = 0x0C268341,  -- character ID byte
    p1_char2_id = 0x0C268E89,
    p1_char3_id = 0x0C2699D1,
    p1_active   = 0x0C268340,  -- which char is point

    -- P2 characters
    p2_meter    = 0x0C28964B,
    p2_char1_hp = 0x0C268D04,
    p2_char2_hp = 0x0C26984C,
    p2_char3_hp = 0x0C26A394,
    p2_char1_id = 0x0C2688E5,
    p2_active   = 0x0C2688E4,

    -- Character select screen
    p1_cursor_col = 0x0C2FB2BC,
    p1_cursor_row = 0x0C2FB2BE,
    sel_char1     = 0x0C2D71E9,
    sel_char2     = 0x0C2D7D31,
    sel_char3     = 0x0C2D8879,
}

-- Maple DMA registers (Holly / System Bus)
-- Access via P2 area (0xA0000000 | phys) for uncached reads
local SB_MDSTAR = 0x005F6C04   -- DMA chain start address
local SB_MDTSEL = 0x005F6C10   -- trigger select (0=CPU, 1=vblank)
local SB_MDEN   = 0x005F6C14   -- DMA enable
local SB_MDST   = 0x005F6C18   -- DMA start

-- Maple command names for display
local CMD_NAMES = {
    [1]  = "DEV_REQ",    [2]  = "ALL_STAT",
    [3]  = "RESET",      [4]  = "SHUTDOWN",
    [9]  = "GET_COND",   [10] = "MEDIA_INFO",
    [11] = "BLK_READ",   [12] = "BLK_WRITE",
    [13] = "BLK_DONE",   [14] = "SET_COND",
}

-- ============================================================
-- Helpers
-- ============================================================
local function hex(v)
    if v == nil then return "nil" end
    return string.format("0x%08X", v)
end

local function safe_read32(addr)
    local ok, val = pcall(flycast.memory.read32, addr)
    if ok then return val end
    return nil
end

local function safe_read8(addr)
    local ok, val = pcall(flycast.memory.read8, addr)
    if ok then return val end
    return 0
end

-- Bit manipulation — auto-detect Lua version
-- Lua 5.2: bit32 library.  Lua 5.3+: native operators.  LuaJIT: bit library.
local band, bor, brshift, blshift
if bit32 then
    band = bit32.band
    bor = bit32.bor
    brshift = bit32.rshift
    blshift = bit32.lshift
elseif bit then
    band = bit.band
    bor = bit.bor
    brshift = bit.rshift
    blshift = bit.lshift
else
    -- Lua 5.3+ native ops (compiled dynamically to avoid 5.2 syntax error)
    band    = load("return function(a,b) return a & b end")()
    bor     = load("return function(a,b) return a | b end")()
    brshift = load("return function(a,n) return a >> n end")()
    blshift = load("return function(a,n) return a << n end")()
end

-- ============================================================
-- Game State Reader
-- ============================================================
local function read_game_state()
    return {
        in_match = safe_read8(ADDR.in_match),
        timer    = safe_read8(ADDR.timer),
        stage    = safe_read8(ADDR.stage),
        p1 = {
            hp    = { safe_read8(ADDR.p1_char1_hp),
                      safe_read8(ADDR.p1_char2_hp),
                      safe_read8(ADDR.p1_char3_hp) },
            meter = safe_read8(ADDR.p1_meter),
            id    = safe_read8(ADDR.p1_char1_id),
        },
        p2 = {
            hp    = { safe_read8(ADDR.p2_char1_hp),
                      safe_read8(ADDR.p2_char2_hp),
                      safe_read8(ADDR.p2_char3_hp) },
            meter = safe_read8(ADDR.p2_meter),
            id    = safe_read8(ADDR.p2_char1_id),
        },
    }
end

local function log_state(s)
    if s.in_match == 1 then
        print(string.format(
            "[F%06d] FIGHT T:%02d | P1: %3d/%3d/%3d M:%d | P2: %3d/%3d/%3d M:%d | STG:%d",
            frame_count, s.timer,
            s.p1.hp[1], s.p1.hp[2], s.p1.hp[3], s.p1.meter,
            s.p2.hp[1], s.p2.hp[2], s.p2.hp[3], s.p2.meter,
            s.stage))
    else
        print(string.format("[F%06d] MENU (in_match=%d)", frame_count, s.in_match))
    end
end

-- ============================================================
-- Maple DMA Chain Walker
-- ============================================================
local function walk_dma_chain()
    -- Try reading SB_MDSTAR from different address mappings
    local mdstar = safe_read32(SB_MDSTAR)
    if mdstar == nil or mdstar == 0 then
        mdstar = safe_read32(0xA05F6C04)  -- P2 uncached
    end
    if mdstar == nil or mdstar == 0 then
        print("  DMA: MDSTAR=0 (not configured or can't read SB registers from Lua)")
        return nil
    end

    print(string.format("  DMA: MDSTAR=%s", hex(mdstar)))

    local descriptors = {}
    local addr = mdstar

    for i = 1, 12 do  -- safety limit
        local h1 = safe_read32(addr)
        local h2 = safe_read32(addr + 4)
        if h1 == nil or h2 == nil then
            print(string.format("  [%d] @%s — can't read", i, hex(addr)))
            break
        end

        local last     = band(brshift(h1, 31), 1)
        local bus_num  = band(brshift(h1, 16), 0x07)
        local maple_op = band(brshift(h1, 8), 0x07)
        local plen     = band(h1, 0xFF)
        local rxbuf    = band(h2, 0x1FFFFFE0)

        -- Read command data words
        local cmd_data = {}
        for w = 0, plen - 1 do
            local val = safe_read32(addr + 8 + (w * 4))
            cmd_data[w] = val or 0
        end

        -- Parse maple command header (first data word)
        local cmd_code = 0
        local cmd_size = 0
        local cmd_dest = 0
        local cmd_orig = 0
        if plen > 0 then
            cmd_code = band(cmd_data[0], 0xFF)
            cmd_dest = band(brshift(cmd_data[0], 8), 0xFF)
            cmd_orig = band(brshift(cmd_data[0], 16), 0xFF)
            cmd_size = band(brshift(cmd_data[0], 24), 0xFF)
        end

        local cmd_name = CMD_NAMES[cmd_code] or string.format("UNK_%d", cmd_code)

        local desc = {
            addr     = addr,
            h1       = h1,
            h2       = h2,
            last     = last,
            bus      = bus_num,
            op       = maple_op,
            plen     = plen,
            rxbuf    = rxbuf,
            cmd_code = cmd_code,
            cmd_name = cmd_name,
            cmd_size = cmd_size,
            cmd_dest = cmd_dest,
            cmd_orig = cmd_orig,
            cmd_data = cmd_data,
        }
        table.insert(descriptors, desc)

        -- Print descriptor
        print(string.format("  [%d] @%s LAST=%d BUS=%d OP=%d PLEN=%d",
            i, hex(addr), last, bus_num, maple_op, plen))
        print(string.format("      CMD=%d(%s) SIZE=%d DEST=0x%02X ORIG=0x%02X RXBUF=%s",
            cmd_code, cmd_name, cmd_size, cmd_dest, cmd_orig, hex(rxbuf)))

        -- Dump raw data words
        if VERBOSE then
            for w = 0, plen - 1 do
                print(string.format("      data[%d] = %s", w, hex(cmd_data[w])))
            end
        end

        -- Also dump the RESPONSE in the receive buffer (from last DMA cycle)
        if cmd_code == 9 and rxbuf ~= 0 then
            local resp_status = safe_read32(rxbuf)
            local resp_w1 = safe_read32(rxbuf + 4)
            local resp_w2 = safe_read32(rxbuf + 8)
            local resp_w3 = safe_read32(rxbuf + 12)
            print(string.format("      RESP: [%s] [%s] [%s] [%s]",
                hex(resp_status), hex(resp_w1), hex(resp_w2), hex(resp_w3)))
        end

        if last == 1 then break end
        addr = addr + (2 + plen) * 4
    end

    return descriptors
end

-- ============================================================
-- Telemetry Injection into DMA Chain
--
-- Extends CMD9 for port 0 (P1 controller) with game state data.
-- The DC sends the extra words to the controller.
-- On real hardware, our ISR reads them from rxDmaBuf.
-- In Flycast, the virtual controller ignores them (harmless).
--
-- WARNING: This writes to the DMA chain buffer in RAM.
-- If the buffer has no room after the CMD9 descriptor,
-- this will corrupt the next descriptor. Check the dump first!
-- ============================================================
local function inject_telemetry(descriptors, state)
    if not INJECT_TELEMETRY then return end
    if not descriptors then return end

    for _, desc in ipairs(descriptors) do
        -- Find CMD9 (GET_CONDITION) targeting main controller (dest=0x20)
        if desc.cmd_code == 9 and desc.cmd_dest == 0x20 then

            -- Check if there's room: we need PLEN to go from 2 to 6.
            -- If next descriptor is right after (2+2)*4=16 bytes, we'd overwrite it.
            -- Only inject if this is the LAST descriptor, or if we've verified spacing.
            if desc.last ~= 1 then
                print("  INJECT: CMD9 is NOT last in chain — SKIPPING (would corrupt next desc)")
                print("  INJECT: To test safely, need to relocate the DMA chain first.")
                return false
            end

            -- Pack game state into 4 telemetry words:
            --   W0: [P1c1_hp | P1c2_hp | P1c3_hp | timer  ]
            --   W1: [P2c1_hp | P2c2_hp | P2c3_hp | 0      ]
            --   W2: [P1_meter| P2_meter| in_match | stage  ]
            --   W3: [P1_id   | P2_id   | 0        | 0      ]
            local w0 = bor(bor(bor(
                blshift(state.p1.hp[1], 24),
                blshift(state.p1.hp[2], 16)),
                blshift(state.p1.hp[3], 8)),
                state.timer)

            local w1 = bor(bor(
                blshift(state.p2.hp[1], 24),
                blshift(state.p2.hp[2], 16)),
                blshift(state.p2.hp[3], 8))

            local w2 = bor(bor(bor(
                blshift(state.p1.meter, 24),
                blshift(state.p2.meter, 16)),
                blshift(state.in_match, 8)),
                state.stage)

            local w3 = bor(
                blshift(state.p1.id, 24),
                blshift(state.p2.id, 16))

            -- Modify command header: change numWords from 1 to 5
            local new_hdr = bor(band(desc.cmd_data[0], 0x00FFFFFF), blshift(5, 24))
            flycast.memory.write32(desc.addr + 8, new_hdr)

            -- Write 4 telemetry words after the function code (data[1])
            flycast.memory.write32(desc.addr + 16, w0)
            flycast.memory.write32(desc.addr + 20, w1)
            flycast.memory.write32(desc.addr + 24, w2)
            flycast.memory.write32(desc.addr + 28, w3)

            -- Update PLEN in header_1 from 2 to 6
            local new_h1 = bor(band(desc.h1, 0xFFFFFF00), 6)
            flycast.memory.write32(desc.addr, new_h1)

            print(string.format("  INJECTED CMD9 @%s: PLEN=6 SIZE=5", hex(desc.addr)))
            print(string.format("    W0=%s W1=%s W2=%s W3=%s", hex(w0), hex(w1), hex(w2), hex(w3)))
            return true
        end
    end

    print("  INJECT: No CMD9 for dest=0x20 found in chain")
    return false
end

-- ============================================================
-- Overlay (optional — shows game state on screen)
-- ============================================================
local function draw_overlay(state)
    if state.in_match ~= 1 then return end

    flycast.ui.beginWindow("GP-RETRO", 10, 10, 300, 120)
    flycast.ui.text(string.format("Timer: %d   Stage: %d", state.timer, state.stage))
    flycast.ui.text(string.format("P1: %d/%d/%d  Meter: %d",
        state.p1.hp[1], state.p1.hp[2], state.p1.hp[3], state.p1.meter))
    flycast.ui.text(string.format("P2: %d/%d/%d  Meter: %d",
        state.p2.hp[1], state.p2.hp[2], state.p2.hp[3], state.p2.meter))

    -- Health bars
    flycast.ui.text("P1 HP:")
    flycast.ui.bargraph(state.p1.hp[1] / 0x90)
    flycast.ui.text("P2 HP:")
    flycast.ui.bargraph(state.p2.hp[1] / 0x90)

    flycast.ui.endWindow()
end

-- ============================================================
-- Callbacks
-- ============================================================
frame_count = 0
local last_in_match = 0

flycast_callbacks = {}

flycast_callbacks.start = function()
    print("========================================")
    print("  GP-RETRO MvC2 Probe v0.1")
    print("  Game state + Maple DMA analysis")
    print("========================================")
    print("  Log interval:  " .. LOG_INTERVAL .. " frames")
    print("  DMA dump:      " .. (DUMP_DMA and "ON" or "OFF"))
    print("  Injection:     " .. (INJECT_TELEMETRY and "ON" or "OFF"))
    print("  Verbose:       " .. (VERBOSE and "ON" or "OFF"))
    print("========================================")
end

flycast_callbacks.vblank = function()
    frame_count = frame_count + 1
    local state = read_game_state()

    -- Detect match transitions
    if state.in_match ~= last_in_match then
        if state.in_match == 1 then
            print(string.format("\n>>> MATCH START [F%06d] Stage:%d <<<", frame_count, state.stage))
        else
            print(string.format("\n<<< MATCH END [F%06d] <<<", frame_count))
        end
        last_in_match = state.in_match
    end

    -- Periodic logging
    if frame_count % LOG_INTERVAL == 0 then
        log_state(state)
        if DUMP_DMA then
            walk_dma_chain()
        end
    end

    -- Telemetry injection (every frame when enabled)
    if INJECT_TELEMETRY then
        local descs = walk_dma_chain()  -- need fresh chain walk each frame
        inject_telemetry(descs, state)
    end
end

flycast_callbacks.overlay = function()
    local state = read_game_state()
    draw_overlay(state)
end
