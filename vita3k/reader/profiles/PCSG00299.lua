-- reader: no hooks
-- reader: api 2
-- reader: title 俺たちに翼はない (Oretachi ni Tsubasa wa Nai)
-- reader: reads Dialogue and speakers
-- reader: version PCSG00299, 1.00
-- 俺たちに翼はない (Navel / 5pb. / MAGES., PS Vita, 2014), PCSG00299 v1.00,
-- eboot.bin module NID D5E58A74.
--
-- Everything is read once a frame from the data segment (PT_LOAD[1] at 0x8115E000 in Vita3K,
-- or reader.module("data") on a real Vita); no hooks are installed.
--
-- Task vector at TASK_VEC (0x8153C6E0: {begin, end, cap}) contains an entry whose buffer
-- std::vector<uint8_t> has size 0x854 (normally at offset +0x0F6C from begin).
-- Message struct at that buffer pointer:
--   +0x000  u32 line1_bytes (byte length of first wrapped line)
--   +0x004  u32 state (4 while active)
--   +0x008  u32 speaker_id (index into SPEAKER_TABLE, 0..361; 361 is blank "　")
--   +0x010  char[] Shift-JIS dialogue text with literal "\n" line breaks and leading "　"
--
-- Speaker table at SPEAKER_TABLE (0x814E9094, stride 0x50, 362 entries, Shift-JIS).
-- Suppression flags:
--   MENU_OPEN    (0x81539A1C): u32 non-zero while the in-game System Menu is open
--   MENU_OVERLAY (0x8153EAC8): u32 non-zero while a menu overlay is active
--
-- Not read: pre-rendered opening movie, prologue poem overlays, system menus, config.

local BUILD = "D5E58A74"
if reader.build_id ~= BUILD then
    error("this profile is for 俺たちに翼はない, PCSG00299 1.00 (" .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local data = reader.module("data")
local DATA = data and data.base or (reader.base + 0x15E000)

local SPEAKER_TABLE  = DATA + 0x38B094 -- 0x814E9094 (362 entries, stride 0x50)
local SPEAKER_STRIDE = 0x50
local SPEAKER_COUNT  = 362

local MENU_OPEN      = DATA + 0x3DBA1C -- 0x81539A1C
local TASK_VEC       = DATA + 0x3DE6E0 -- 0x8153C6E0 ({begin, end, cap})
local MENU_OVERLAY   = DATA + 0x3E0AC8 -- 0x8153EAC8

local MSG_BUF_SIZE   = 0x854
local MSG_STATE      = 0x004
local MSG_SPEAKER_ID = 0x008
local MSG_TEXT       = 0x010

local SHOWN_FRAMES   = 3

-- Dialogue box glyph grid in 960x544 Vita coordinates (measured from live frames):
-- fullwidth cell 22x24, row advance 33, origin (113, 408).
local ORIGIN_X = 113
local ORIGIN_Y = 408
local CELL_W   = 22
local CELL_H   = 24
local ROW_STEP = 33

local function usable(addr)
    return addr ~= nil and addr >= 0x80000000 and addr < 0xA0000000
end

local function is_sjis_lead(c)
    return (c >= 0x81 and c <= 0x9F) or (c >= 0xE0 and c <= 0xFC)
end

-- Locate the message buffer pointer (size 0x854) inside TASK_VEC.
local cached_off = 0x0F6C
local function find_msg_buf()
    local vb = mem.u32(TASK_VEC)
    local ve = mem.u32(TASK_VEC + 4)
    if not usable(vb) or not usable(ve) or ve <= vb or (ve - vb) > 0x10000 then
        return nil
    end
    local function check_at(off)
        if off < 0 or vb + off + 12 > ve then return nil end
        local p  = mem.u32(vb + off)
        local p2 = mem.u32(vb + off + 4)
        local p3 = mem.u32(vb + off + 8)
        if usable(p) and p2 == p + MSG_BUF_SIZE and p3 == p2 then
            return p
        end
        return nil
    end
    local p = check_at(cached_off)
    if p then return p end
    for off = 0, (ve - vb) - 12, 4 do
        p = check_at(off)
        if p then
            cached_off = off
            return p
        end
    end
    return nil
end

-- Decode Shift-JIS dialogue text with literal "\n" line breaks into clean text and
-- per-codepoint screen cells. Leading fullwidth space ("　") indents the first line
-- on screen without being emitted as a leading space in the text.
local function parse_dialogue(raw)
    local chars = {}
    local cells = {}
    local x, y = ORIGIN_X, ORIGIN_Y
    local i = 1
    local at_start = true

    while i <= #raw do
        local c = raw:byte(i)
        if c == 0x0A then
            x = ORIGIN_X
            y = y + ROW_STEP
            i = i + 1
        elseif c == 0x5C and i < #raw and raw:byte(i + 1) == 0x6E then
            -- Literal "\n" wrap marker inserted by the game
            x = ORIGIN_X
            y = y + ROW_STEP
            i = i + 2
        elseif is_sjis_lead(c) and i < #raw then
            local c2 = raw:byte(i + 1)
            if at_start and c == 0x81 and c2 == 0x40 then
                -- Leading fullwidth indent space: advance x, do not emit
                x = x + CELL_W
                at_start = false
            else
                local ch = text.sjis(raw:sub(i, i + 1))
                if ch and #ch > 0 then
                    chars[#chars + 1] = ch
                    cells[#cells + 1] = { x, y, CELL_W, CELL_H }
                    at_start = false
                end
                x = x + CELL_W
            end
            i = i + 2
        elseif c >= 0x20 and c < 0x7F then
            local ch = string.char(c)
            if at_start and c == 0x20 then
                x = x + CELL_W * 0.5
            else
                chars[#chars + 1] = ch
                cells[#cells + 1] = { x, y, CELL_W * 0.5, CELL_H }
                at_start = false
                x = x + CELL_W * 0.5
            end
            i = i + 1
        else
            i = i + 1
        end
    end
    return table.concat(chars), cells
end

local function read_speaker(id)
    if id == nil or id >= SPEAKER_COUNT then return nil end
    local raw = mem.cstr(SPEAKER_TABLE + id * SPEAKER_STRIDE, 64) or ""
    if #raw == 0 then return nil end
    local s = text.sjis(raw)
    if not s or #s == 0 then return nil end
    -- Strip ASCII and fullwidth spaces to detect blank speaker slot (id 361 = "　")
    local trimmed = s:gsub("^[\x20\t]+", ""):gsub("[\x20\t]+$", ""):gsub("^\227\128\128+", ""):gsub("\227\128\128+$", "")
    if #trimmed == 0 then return nil end
    return trimmed
end

local shown_key = nil
local emitted_key = nil
local stood = 0

local function withdraw()
    if shown_key ~= nil then
        reader.layout("message", nil)
        shown_key = nil
    end
    stood = 0
end

reader.on_frame(function()
    if (mem.u32(MENU_OPEN) or 0) ~= 0 or (mem.u32(MENU_OVERLAY) or 0) ~= 0 then
        withdraw()
        return
    end

    local p = find_msg_buf()
    if not p then
        withdraw()
        return
    end

    local state = mem.u32(p + MSG_STATE) or 0
    if state == 0 then
        withdraw()
        return
    end

    local raw = mem.cstr(p + MSG_TEXT, 1024) or ""
    if #raw == 0 then
        withdraw()
        return
    end

    local spk_id = mem.u32(p + MSG_SPEAKER_ID)
    local speaker = read_speaker(spk_id)
    local key = (speaker or "") .. "\0" .. raw
    if key == shown_key then return end

    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0
    shown_key = key

    local line, cells = parse_dialogue(raw)
    if #line == 0 then return end

    if key ~= emitted_key then
        emitted_key = key
        reader.emit{ text = line, speaker = speaker, window = "message" }
    end
    reader.layout("message", { space = { 960, 544 }, glyphs = cells })
end)
