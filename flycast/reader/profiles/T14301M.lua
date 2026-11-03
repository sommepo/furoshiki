-- reader: no hooks
-- reader: title 北へ。White Illumination (Kita e. White Illumination)
-- reader: reads The message window: what is said and told in the story
-- reader: version T14301M (Japan)
-- reader: touch press
-- reader: buttons up down left right a b
-- Kita e. White Illumination (Hudson, Dreamcast, 1999). docs/reader/games/kita-e.md has what
-- is read, what is not, and how it was checked.
--
-- The game runs on Windows CE: the program sees its memory at addresses of its own, which
-- the system maps page by page onto the console's memory. This profile follows the system's
-- page tables to read by the program's addresses. A scene's script is a table of messages,
-- each a number of rows and a pointer to the first of them (Shift-JIS strings one after the
-- other); an object the game calls "MessageOut" has the number of the message it shows.

local BUILD = "A3C799F2"
if reader.build_id ~= BUILD then
    error("this profile is for Kita e. White Illumination T14301M (" .. BUILD .. "), the disc is " .. tostring(reader.build_id))
end

-- Windows CE: the table of the processes' address spaces and the game's place in it.
local SECTIONS, SLOT = 0x8C1618A0, 7
-- The program's own addresses.
local OUT_ENTRY = 0x00032D0C   -- pointer to the named object "MessageOut"
local SCRIPT = 0x00040548      -- pointer to the scene's script
local FAMILY, GIVEN = 0x00033770, 0x0003377C   -- the hero's name as it was entered

-- The message window's grid, in the 640x480 picture.
local LEFT, TOP, CELL, PITCH, HEIGHT = 20, 370, 24, 27, 26

local function kernel(p) return p and p >= 0x8C000000 and p < 0x8D000000 end

-- Where a page of the program's is in the console's memory, or nil.
local function page(va)
    local section = mem.u32(SECTIONS + SLOT * 4)
    if not kernel(section) then return nil end
    local block = mem.u32(section + ((va >> 16) & 0x1FF) * 4)
    if not kernel(block) then return nil end
    local entry = mem.u32(block + 0x0C + ((va >> 12) & 0xF) * 4)
    if not entry or entry & 0x100 == 0 then return nil end
    return 0x8C000000 | (entry & 0x00FFF000)
end

local function vread(va, size)
    if not va or va < 0x10000 or va >= 0x02000000 then return nil end
    local parts = {}
    while size > 0 do
        local base = page(va)
        if not base then return nil end
        local take = math.min(size, 0x1000 - (va & 0xFFF))
        local piece = mem.read(base + (va & 0xFFF), take)
        if not piece or #piece < take then return nil end
        parts[#parts + 1] = piece
        va, size = va + take, size - take
    end
    return table.concat(parts)
end

local function v32(va)
    local raw = vread(va, 4)
    return raw and string.unpack("<I4", raw)
end

local function vstr(va, limit)
    local raw = vread(va, limit)
    return raw and raw:match("^[^%z]*")
end

local shown = false
local last = nil

local function withdraw()
    if shown then
        reader.layout("message", nil)
        shown = false
    end
end

-- The characters of a row, one or two bytes each, without its commands: a command is a
-- letter or a number between two at signs (@S@, @P@, @0002@) and takes no room.
local function each(raw)
    local out, i = {}, 1
    while i <= #raw do
        local b = raw:byte(i)
        if b == 0x40 then
            local close = raw:match("^@[%w_]*()@", i)
            if close then
                i = close + 1
                goto continue
            end
        end
        do
            local n = ((b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)) and 2 or 1
            out[#out + 1] = raw:sub(i, i + n - 1)
            i = i + n
        end
        ::continue::
    end
    return out
end

local PLACEHOLDER = {}   -- by its Shift-JIS bytes: which parts of the name
PLACEHOLDER["\x8e\xe5\x90\x6c\x8c\xf6\x96\xbc\x91\x4f"] = { FAMILY, GIVEN }   -- 主人公名前
PLACEHOLDER["\x8e\xe5\x90\x6c\x8c\xf6"] = { FAMILY }                           -- 主人公
PLACEHOLDER["\x96\xbc\x91\x4f"] = { GIVEN }                                    -- 名前

local function named(raw)
    return (raw:gsub("&([^&]+)&", function(key)
        local parts = PLACEHOLDER[key]
        if not parts then return nil end
        local out = {}
        for _, at in ipairs(parts) do out[#out + 1] = vstr(at, 12) or "" end
        return table.concat(out)
    end))
end

reader.on_frame(function(frame)
    local entry = v32(OUT_ENTRY)
    local out = entry and vread(entry + 0x14, 11) == "MessageOut\0" and v32(entry + 8)
    local state = out and vread(out + 8, 12)
    if not state then withdraw(); last = nil; return end
    -- The second byte: 0 while no message is up, 1 while one is typed, 2 while it waits.
    local up, index = string.unpack("<xBxxI4", state)
    if up ~= 1 and up ~= 2 then withdraw(); return end
    local script = v32(SCRIPT)
    local head = script and vread(script + 4, 16)
    if not head then withdraw(); return end
    local count, _, _, lines = string.unpack("<I4I4I4I4", head)
    if index >= count then withdraw(); return end
    local key = script .. ":" .. index
    if key == last then return end
    local item = vread(lines + index * 8, 8)
    if not item then return end
    local rows, at = string.unpack("<I4I4", item)
    if rows < 1 or rows > 4 then return end
    local chars, cells = {}, {}
    for row = 0, rows - 1 do
        local raw = vstr(at, 128)
        if not raw then return end
        at = at + #raw + 1
        if row > 0 then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
        local x = LEFT
        for _, c in ipairs(each(named(raw))) do
            local wide = #c == 2 and CELL or CELL / 2
            chars[#chars + 1] = c
            cells[#cells + 1] = { x, TOP + row * PITCH, wide, HEIGHT }
            x = x + wide
        end
    end
    last = key
    if #chars == 0 then return end
    reader.emit{ text = text.sjis(table.concat(chars)), speaker = "", window = "message", id = key }
    reader.layout("message", { space = { 640, 480 }, box = { 18, 366, 604, 84 }, glyphs = cells })
    shown = true
end)

reader.on_state_loaded(function()
    withdraw(); last = nil
end)
