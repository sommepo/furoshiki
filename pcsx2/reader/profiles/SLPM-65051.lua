-- reader: no hooks
-- reader: title サイレントヒル2
-- reader: reads The text at the bottom of the picture: what is said in scenes and what is told when something is looked at
-- reader: version SLPM-65051
-- サイレントヒル2 (Konami, PS2, 2001), SLPM-65051. Program E36E16C9. Japanese text only.
--
-- A message is a list of 16-bit codes. Below 0xf000 a code is a font cell: 223 cells of the
-- game's own, then Shift-JIS (Windows' variant) in order without its gaps. 0xfffd starts a
-- new row, 0xffff ends the message.
--
-- Read: the message's address, how many characters are shown (0 while no text is up) and
-- the rows' left edge. Rows are 30 apart around a line near the bottom of the 640x448
-- picture; a character is 27 wide.

local BUILD = "E36E16C9"
if reader.build_id ~= BUILD then
    error("this profile is for Silent Hill 2 SLPM-65051 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local MESSAGE = 0x01151914   -- where the message begins
local SHOWN = 0x011513ec     -- u16: how many characters are on the screen
local LEFT = 0x01150a7c      -- u16: where the rows begin
local FIRST = 223            -- the font's place of the first Shift-JIS character
local NEW_ROW, PAUSE, END = 0xfffd, 0xfffe, 0xffff
local ADVANCE, PITCH, HEIGHT, MIDDLE = 27, 30, 26, 388
local UNITS_MAX = 400

-- Shift-JIS without its gaps, as runs of codes (first, last), up to the last kanji row.
local RUNS = {
    0x8140,0x81ac, 0x81b8,0x81bf, 0x81c8,0x81ce, 0x81da,0x81e8, 0x81f0,0x81f7, 0x81fc,0x81fc,
    0x824f,0x8258, 0x8260,0x8279, 0x8281,0x829a, 0x829f,0x82f1, 0x8340,0x8396, 0x839f,0x83b6,
    0x83bf,0x83d6, 0x8440,0x8460, 0x8470,0x8491, 0x849f,0x84be, 0x8740,0x875d, 0x875f,0x8775,
    0x877e,0x879c, 0x889f,0x88fc, 0x8940,0x89fc, 0x8a40,0x8afc, 0x8b40,0x8bfc, 0x8c40,0x8cfc,
    0x8d40,0x8dfc, 0x8e40,0x8efc, 0x8f40,0x8ffc, 0x9040,0x90fc, 0x9140,0x91fc, 0x9240,0x92fc,
    0x9340,0x93fc, 0x9440,0x94fc, 0x9540,0x95fc, 0x9640,0x96fc, 0x9740,0x97fc, 0x9840,0x9872,
    0x989f,0x98fc, 0x9940,0x99fc, 0x9a40,0x9afc, 0x9b40,0x9bfc, 0x9c40,0x9cfc, 0x9d40,0x9dfc,
    0x9e40,0x9efc, 0x9f40,0x9ffc, 0xe040,0xe0fc, 0xe140,0xe1fc, 0xe240,0xe2fc, 0xe340,0xe3fc,
    0xe440,0xe4fc, 0xe540,0xe5fc, 0xe640,0xe6fc, 0xe740,0xe7fc, 0xe840,0xe8fc, 0xe940,0xe9fc,
    0xea40,0xeaa4,
}
local CELLS = {}
do
    local n = 0
    for k = 1, #RUNS, 2 do
        for code = RUNS[k], RUNS[k + 1] do
            if code & 0xff ~= 0x7f then
                CELLS[n] = text.sjis(string.char(code >> 8, code & 0xff))
                n = n + 1
            end
        end
    end
end

-- The rows of the message at `address` as lists of characters, and its raw units.
local function read_message(address)
    local raw = mem.read(address, UNITS_MAX * 2)
    if not raw or #raw < UNITS_MAX * 2 then return nil end
    local rows, row, count = {}, {}, 0
    local used = 0
    for k = 0, UNITS_MAX - 1 do
        local unit = string.unpack("<I2", raw, k * 2 + 1)
        used = k + 1
        if unit == END then break end
        if unit == NEW_ROW then
            rows[#rows + 1] = row
            row = {}
        elseif unit < 0xf000 and unit >= FIRST then
            row[#row + 1] = CELLS[unit - FIRST] or "?"
            count = count + 1
        elseif unit < FIRST and count > 0 then
            -- The game's own cells below Shift-JIS are unknown: a placeholder is shown.
            row[#row + 1] = "?"
            count = count + 1
        end
    end
    rows[#rows + 1] = row
    return rows, count, raw:sub(1, used * 2)
end

local shown_key
local up = false

local function drop()
    if up then
        reader.layout("message", nil)
        up = false
    end
end

reader.on_state_loaded(function()
    drop()
    shown_key = nil
end)

reader.on_frame(function()
    local shown = mem.u16(SHOWN) or 0
    local address = mem.u32(MESSAGE) or 0
    if shown == 0 or address < 0x00100000 or address >= 0x02000000 then
        drop()
        shown_key = nil
        return
    end
    local rows, count, units = read_message(address)
    if not rows or count == 0 or shown < count then return end
    local key = address .. ":" .. units
    if key == shown_key then return end
    shown_key = key

    local left = mem.u16(LEFT) or 0
    local longest = 0
    for _, row in ipairs(rows) do longest = math.max(longest, #row) end
    if left == 0 or left > 640 then left = 320 - longest * ADVANCE / 2 end
    -- The rows form one text: the game wraps sentences anywhere.
    local b, cells = reader.builder(), {}
    local top = MIDDLE - #rows * PITCH / 2
    for index, row in ipairs(rows) do
        for k, ch in ipairs(row) do
            b:add(ch)
            cells[#cells + 1] = { left + (k - 1) * ADVANCE, top + (index - 1) * PITCH, ADVANCE, HEIGHT }
        end
    end
    local whole = b:text()
    if #whole == 0 then return end
    drop()
    reader.emit{ text = whole, window = "message",
                 id = text.hash(units) .. ":" .. string.format("%x", address) }
    reader.layout("message", { space = { 640, 448 }, glyphs = cells })
    up = true
end)
