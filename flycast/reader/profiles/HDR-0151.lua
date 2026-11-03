-- reader: no hooks
-- reader: title セガガガ (Segagaga)
-- reader: reads The message window: what is said in the story's conversations
-- reader: version HDR-0151 (Japan)
-- Segagaga (Sega, Dreamcast, 2001). docs/reader/games/segagaga.md has what is read, what is
-- not, and how it was checked.
--
-- A conversation is one Shift-JIS text with commands in it that begin with an at sign:
-- @n a new row, @p and @k wait for the button and clear the window, and others with a number after them (@v a voice, @c and @f who
-- speaks and with which face, @w, @m, @e). The game reads it through a stream that keeps a
-- pointer to the text, how far it has read, and where the run of text it is showing began.

local BUILD = "042E25DC"
if reader.build_id ~= BUILD then
    error("this profile is for Segagaga HDR-0151 (" .. BUILD .. "), the disc is " .. tostring(reader.build_id))
end

local STREAM = 0x8CAC5E54     -- the message's stream: pointer, read position, start of the run
local AT = 0x40               -- "@"

-- The message window's grid, in the 640x480 picture.
local LEFT, TOP, CELL, PITCH, HEIGHT = 42, 344, 24, 30, 26

local shown = false    -- the message window has a layout
local last_base, last_pos, still = nil, nil, 0

local function withdraw()
    if shown then
        reader.layout("message", nil)
        shown = false
    end
end

-- The bytes of a run without their commands: its characters (one or two bytes each) and a
-- "\n" where a new row begins.
local function plain(raw)
    local out, i = {}, 1
    while i <= #raw do
        local b = raw:byte(i)
        if b == AT then
            local c = raw:sub(i + 1, i + 1)
            i = i + 2
            while raw:byte(i) and raw:byte(i) >= 0x30 and raw:byte(i) <= 0x39 do i = i + 1 end
            if c == "n" then out[#out + 1] = "\n" end
        elseif (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc) then
            out[#out + 1] = raw:sub(i, i + 1)
            i = i + 2
        else
            out[#out + 1] = raw:sub(i, i)
            i = i + 1
        end
    end
    return out
end

reader.on_frame(function(frame)
    local base = mem.u32(STREAM)
    local pos = mem.u32(STREAM + 4)
    if not base or base < 0x8C010000 or base >= 0x8D000000 or not pos or pos > 0x8000 then
        withdraw()
        last_base, last_pos = nil, nil
        return
    end
    -- The reading stands still while the game waits for the button; when it moves on, what
    -- was in the window is gone.
    if pos ~= last_pos or base ~= last_base then
        if last_pos ~= nil then withdraw() end
        last_base, last_pos, still = base, pos, 0
        return
    end
    still = still + 1
    if still ~= 4 then return end
    local raw = mem.read(base, pos + 2)
    if not raw or #raw < pos + 2 then return end
    local waiting = raw:sub(pos + 1, pos + 2)
    if waiting ~= "@p" and waiting ~= "@k" then return end
    -- What the window holds: the run since the wait before this one (either kind clears it).
    local begin, at = 0, 1
    while true do
        local found = raw:find("@[pk]", at)
        if not found or found > pos then break end
        begin = found + 1
        at = found + 2
    end
    local row, x = 0, LEFT
    local chars, cells = {}, {}
    for _, c in ipairs(plain(raw:sub(begin + 1, pos))) do
        if c == "\n" then
            if #chars > 0 then
                row, x = row + 1, LEFT
                chars[#chars + 1] = c
                cells[#cells + 1] = false
            end
        else
            local w = #c == 2 and CELL or CELL / 2
            chars[#chars + 1] = c
            cells[#cells + 1] = { x, TOP + row * PITCH, w, HEIGHT }
            x = x + w
        end
    end
    while chars[#chars] == "\n" do
        chars[#chars], cells[#cells] = nil, nil
    end
    if #chars == 0 then return end
    local raw_text = table.concat(chars)
    reader.emit{ text = text.sjis(raw_text), speaker = "", window = "message",
                 id = text.hash(raw:sub(1, pos)) }
    reader.layout("message", { space = { 640, 480 }, box = { 24, 322, 596, 124 }, glyphs = cells })
    shown = true
end)

reader.on_state_loaded(function()
    withdraw()
    last_base, last_pos = nil, nil
end)
