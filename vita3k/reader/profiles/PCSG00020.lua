-- reader: api 2
-- reader: title 極限脱出ADV 善人シボウデス (Zero Escape: Virtue's Last Reward)
-- reader: reads Dialogue and speakers
-- reader: version PCSG00020, 1.00
-- 極限脱出ADV 善人シボウデス (Chunsoft, PS Vita, 2012), PCSG00020 v1.00,
-- eboot.bin module NID F98BA0BA.
-- Adapted from LunaHook (vita3k_1.cpp, 0x810DE344 / r6 / UTF-8).
--
-- Dialogue hook at reader.base + 0xDE345 (Thumb):
--   regs.r6 points to the UTF-8 dialogue string (with inline tags such as <N>, <K>, <P>, <F:B>).
-- Active speaker struct in static data at SPEAKER_PTR (0x823D88F8):
--   -0x10 (0x823D88E8): u8 visible flag (non-zero while the dialogue box is visible)
--   +0x00 (0x823D88F8): u32 pointer to UTF-8 speaker name string (e.g. "シグマ", "少女")
--
-- Not read: escape-room 3D object labels, flowchart nodes, files/memos, system menus.

local BUILD = "F98BA0BA"
if reader.build_id ~= BUILD then
    error("this profile is for 極限脱出ADV 善人シボウデス, PCSG00020 1.00 (" .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local BOX_VIS     = reader.base + 0x13D88E8 -- 0x823D88E8
local SPEAKER_PTR = reader.base + 0x13D88F8 -- 0x823D88F8

-- Dialogue box glyph grid in 960x544 Vita coordinates (measured from live frames;
-- the game's box rect at 0x823D8994 is x=75, y=403, w=806).
local ORIGIN_X = 97
local ORIGIN_Y = 423
local CELL_W   = 28
local CELL_H   = 26
local ROW_STEP = 34
local MAX_COLS = 28

local function usable(addr)
    return addr ~= nil and addr >= 0x80000000 and addr < 0xC0000000
end

local function read_speaker()
    local ptr = mem.u32(SPEAKER_PTR)
    if not usable(ptr) then return nil end
    local s = mem.cstr(ptr, 64) or ""
    s = s:gsub("^[\x20\t]+", ""):gsub("[\x20\t]+$", ""):gsub("^\227\128\128+", ""):gsub("\227\128\128+$", "")
    if #s == 0 then return nil end
    return s
end

-- Parse UTF-8 dialogue text, converting <N> / \n into row advances and stripping
-- inline formatting tags (<K>, <P>, <F:...>, etc.), while producing per-codepoint cells.
local function parse_dialogue(raw)
    -- Normalize <N> / <n> to \n, strip all other <...> tags
    local s = raw:gsub("<[Nn]>", "\n"):gsub("<[^>]+>", "")
    -- Strip leading/trailing whitespace and leading fullwidth space
    s = s:gsub("^[\r\n\x20\t]+", ""):gsub("[\r\n\x20\t]+$", ""):gsub("^\227\128\128+", "")

    local chars = {}
    local cells = {}
    local x, y = ORIGIN_X, ORIGIN_Y
    local col = 0
    local i = 1
    local n = #s

    while i <= n do
        local b = s:byte(i)
        if b == 0x0A then
            x = ORIGIN_X
            y = y + ROW_STEP
            col = 0
            i = i + 1
        elseif b == 0x0D then
            i = i + 1
        else
            local len = 1
            if b >= 0xF0 then len = 4
            elseif b >= 0xE0 then len = 3
            elseif b >= 0xC0 then len = 2
            end
            if i + len - 1 > n then break end
            local ch = s:sub(i, i + len - 1)
            local w = (len == 1) and (CELL_W * 0.5) or CELL_W
            if col >= MAX_COLS then
                x = ORIGIN_X
                y = y + ROW_STEP
                col = 0
            end
            chars[#chars + 1] = ch
            cells[#cells + 1] = { x, y, w, CELL_H }
            x = x + w
            col = col + ((len == 1) and 0.5 or 1)
            i = i + len
        end
    end

    return table.concat(chars), cells
end

local last_key = nil
local layout_up = false

reader.hook(reader.base + 0xDE345, function(regs)
    local ptr = regs.r6
    if not usable(ptr) then return end
    local raw = mem.cstr(ptr, 1024)
    if not raw or #raw == 0 then return end

    local line, cells = parse_dialogue(raw)
    if #line == 0 then return end

    local speaker = read_speaker()
    local key = (speaker or "") .. "\0" .. line
    if key == last_key then return end
    last_key = key

    reader.emit{ text = line, speaker = speaker, window = "message" }
    reader.layout("message", { space = { 960, 544 }, glyphs = cells })
    layout_up = true
end)

reader.on_frame(function()
    if layout_up and (mem.u8(BOX_VIS) or 0) == 0 then
        reader.layout("message", nil)
        layout_up = false
    end
end)
