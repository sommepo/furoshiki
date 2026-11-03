-- reader: no hooks
-- reader: title レンタヒーロー No.1 (Rent a Hero No.1)
-- reader: reads The message window: narration and what people say
-- reader: version HDR-0074 (Japan)
-- Rent a Hero No.1 (Sega, Dreamcast, 2000), Japanese version. docs/reader/games/
-- rent-a-hero.md has what is read, what is not, and how it was checked.
--
-- A message is a row of 16-bit codes. Below 0x8000 a code is a character, counted through
-- the JIS X 0208 table with its empty places left out (ten codes come before the table's
-- first character, and the row of box-drawing characters is not there); 0x8001 is a new
-- row, 0x8000 the end, 0x9000 and up stand for a name the player typed, and 0x4D00 to
-- 0x4DFF are half-width Latin characters. The game keeps
-- pointers to the message it is showing and to where it has got to in it.

local BUILD = "4CE8F65A"
if reader.build_id ~= BUILD then
    error("this profile is for Rent a Hero No.1 HDR-0074 (" .. BUILD .. "), the disc is " .. tostring(reader.build_id))
end

local WINDOW = 0x8C1E29C8   -- not zero while the message window is up
local START = 0x8C1E29DC    -- pointer to the message's first code
local READ = 0x8C1E29E0     -- pointer to where the game has got to (the end code when it is all shown)
local NAMES = 0x8C1BEFBC    -- the names the codes from 0x9000 stand for, 18 bytes each
local END, ROW, NAME = 0x8000, 0x8001, 0x9000

-- Where the window's text is drawn, in the 640x480 picture (measured on the opening's
-- narration: 24 across and down for every character).
local LEFT, TOP, CELL, PITCH, HEIGHT = 70, 350, 24, 24, 24

-- The game's character codes: every character of the JIS table's rows 1 to 7 and 16 to 84,
-- in the table's order, counted from 10.
local glyphs = {}
do
    local code = 10
    local function row(r)
        for cell = 1, 94 do
            local c = text.eucjp(string.char(0xA0 + r, 0xA0 + cell))
            -- (an empty place comes back beginning with the replacement character)
            if c ~= "" and c:sub(1, 3) ~= "\239\191\189" then
                glyphs[code] = c
                code = code + 1
            end
        end
    end
    for r = 1, 7 do row(r) end
    for r = 16, 84 do row(r) end
end

local shown = nil

local function withdraw()
    if shown then
        reader.layout("message", nil)
        shown = nil
    end
end

local function codes_at(address, limit)
    local raw = mem.read(address, limit * 2)
    local out = {}
    if not raw then return out end
    for i = 1, #raw - 1, 2 do
        local v = raw:byte(i) | (raw:byte(i + 1) << 8)
        out[#out + 1] = v
        if v == END then break end
    end
    return out
end

local still, last = 0, nil

reader.on_frame(function(frame)
    local open = mem.u32(WINDOW)
    local start, read = mem.u32(START), mem.u32(READ)
    if not open or open == 0 or not start or not read or start < 0x8C010000 or start >= 0x8D000000
        or read < start or read - start > 2048 then
        withdraw()
        last = nil
        return
    end
    -- All of it is shown when the game has got to the end code.
    if mem.u16(read) ~= END then
        last = nil
        return
    end
    local key = start .. ":" .. read
    if key ~= last then
        last, still = key, 0
        return
    end
    still = still + 1
    if still ~= 3 or shown == key then return end
    withdraw()
    local chars, cells = {}, {}
    local row, x = 0, LEFT
    local function put(c)
        chars[#chars + 1] = c
        cells[#cells + 1] = { x, TOP + row * PITCH, CELL, HEIGHT }
        x = x + CELL
    end
    for _, v in ipairs(codes_at(start, (read - start) / 2 + 1)) do
        if v == END then
            break
        elseif v == ROW then
            row, x = row + 1, LEFT
            chars[#chars + 1] = "\n"
            cells[#cells + 1] = false
        elseif v >= NAME and v < NAME + 16 then
            for _, n in ipairs(codes_at(NAMES + (v - NAME) * 18, 9)) do
                if n ~= END and glyphs[n] then put(glyphs[n]) end
            end
        elseif v >= 0x4D00 and v <= 0x4DFF then
            -- A half-width character: the Latin letters and digits, 0x7D above their ASCII
            -- codes (seen: capitals; 0xA7 is the space between words).
            local c = (v & 0xFF) - 0x7D
            chars[#chars + 1] = (v == 0x4DA7 or c < 0x21 or c > 0x7E) and " " or string.char(c)
            cells[#cells + 1] = { x, TOP + row * PITCH, CELL / 2, HEIGHT }
            x = x + CELL / 2
        elseif v < END then
            put(glyphs[v] or "?")
        end
    end
    while chars[#chars] == "\n" do
        chars[#chars], cells[#cells] = nil, nil
    end
    if #chars == 0 then return end
    reader.emit{ text = table.concat(chars), speaker = "", window = "message", id = text.hash(key) }
    reader.layout("message", { space = { 640, 480 }, glyphs = cells })
    shown = key
end)

reader.on_state_loaded(function()
    withdraw()
    last = nil
end)
