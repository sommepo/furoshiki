-- reader: no hooks
-- reader: title ひぐらしのなく頃に祭 カケラ遊び (Higurashi no Naku Koro ni Matsuri: Kakera Asobi)
-- reader: reads The story's text with speakers and furigana, part by part as it comes up
-- reader: version SLPM-66913, disc version 1.01
-- reader: touch press
-- reader: buttons up down circle cross triangle
-- ひぐらしのなく頃に祭 カケラ遊び (Alchemist, PS2, 2007), SLPM-66913. Program C08A36F0.
--
-- The whole script is in memory. A message is a string: speaker name, the letter r, then text
-- with one-letter commands (k waits for the button, r starts a row, v names a voice, b gives
-- the reading of the word between < and >, ! escapes an ASCII character). Kana and
-- punctuation are single bytes indexing a 64-character table in the program; the rest is
-- Shift-JIS.
--
-- When a message starts, the game lays it out once: one entry per character with its box in
-- the text window and the time it appears (ms from the message start). A clock runs while
-- characters appear and stops at each k.

local BUILD = "C08A36F0"
if reader.build_id ~= BUILD then
    error("this profile is for ひぐらしのなく頃に祭 カケラ遊び SLPM-66913 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

-- Addresses (the program loads at its link address).
local KANA_TABLE = 0x00245598    -- 64 Shift-JIS characters, for the bytes 0xA0 to 0xDF
-- The message state is part of the scene object, which the game allocates: its address
-- varies between runs (by 16 bytes once autosave is switched on), so it is read from the
-- program's pointer to it and the fields are offsets into it.
local SCENE = 0x0025b264         -- pointer to the scene object
local MESSAGE_TEXT = 0xcd4       -- pointer to the string of the message being shown, in the script
local BOX_LEFT = 0x1be4          -- the text box in the picture: left, top, right, bottom
local BOX_TOP = 0x1be8
local GLYPHS = 0x1c08            -- pointer to the list of characters
local GLYPHS_END = 0x1c0c        -- pointer to the list's end
local CLOCK = 0x1c7c             -- milliseconds of the message shown so far
local ENTRY = 52                 -- bytes to an entry: at +4 left, top, right, bottom (floats,
                                 -- in the box), at +44 the moment it comes up, at +48 how
                                 -- far the next character is (a float)
local SMALL = 15                 -- an entry lower than this is a character of a reading: those
                                 -- stand in the list after their word, and are not the text's

-- Frames a part must stay complete before it is reported; skipped text is not read.
local SHOWN_FRAMES = 4

local kana -- the table's characters as UTF-8, by byte

-- A u32 field of the scene object; nil while there is none.
local function field(offset)
    local scene = mem.u32(SCENE)
    if not scene or scene < 0x00100000 or scene >= 0x02000000 then return nil end
    return mem.u32(scene + offset)
end
local function load_kana()
    local raw = mem.read(KANA_TABLE, 128)
    if not raw or #raw < 128 then return false end
    kana = {}
    for i = 0, 63 do
        local lo, hi = raw:byte(i * 2 + 1, i * 2 + 2)
        kana[0xa0 + i] = text.sjis(string.char(hi, lo))
    end
    return true
end

local WITH_ARGUMENT = {}
for c in ("vbfozscaw$"):gmatch(".") do WITH_ARGUMENT[c:byte()] = true end

local function is_lead(b)
    return (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)
end

-- One character of the string at `at` and the next position; nil for an ASCII command byte.
local function character(raw, at)
    local b = raw:byte(at)
    if b >= 0xa0 and b <= 0xdf then
        return kana[b], at + 1
    elseif is_lead(b) and at < #raw then
        return text.sjis(raw:sub(at, at + 1)), at + 2
    end
    return nil, at
end

-- Parses a message into the speaker and the parts between waits: each a list of pieces
-- { text = one character, or "\n" with newline = true } and ruby groups { first, last, reading }.
local function parse(raw)
    local at, speaker = 1, {}
    -- The speaker's name stands before the first r.
    while at <= #raw do
        local b = raw:byte(at)
        if b == 0x72 then
            at = at + 1
            break
        elseif b == 0x21 then -- !
            speaker[#speaker + 1] = raw:sub(at + 1, at + 1)
            at = at + 2
        else
            local c, after = character(raw, at)
            if c then speaker[#speaker + 1] = c end
            at = c and after or at + 1
        end
    end
    local parts, pieces, ruby = {}, {}, {}
    local reading, word
    local function flush()
        if #pieces > 0 then
            parts[#parts + 1] = { pieces = pieces, ruby = ruby }
        end
        pieces, ruby = {}, {}
    end
    while at <= #raw do
        local b = raw:byte(at)
        if b == 0 then break end
        local c, after = character(raw, at)
        if c then
            pieces[#pieces + 1] = { text = c }
            at = after
        elseif b == 0x21 then -- !: the next byte is itself
            if at < #raw then pieces[#pieces + 1] = { text = raw:sub(at + 1, at + 1) } end
            at = at + 2
        elseif b == 0x72 then -- r: a new row
            pieces[#pieces + 1] = { text = "\n", newline = true }
            at = at + 1
        elseif b == 0x6b then -- k: wait for the button
            flush()
            at = at + 1
        elseif WITH_ARGUMENT[b] then
            local stop = raw:find(".", at + 1, true) or #raw
            if b == 0x62 then -- b: the reading of the word that follows
                local out, i = {}, at + 1
                while i < stop do
                    local rc, ri = character(raw, i)
                    if rc then
                        out[#out + 1] = rc
                        i = ri
                    else
                        if raw:byte(i) ~= 0x21 then out[#out + 1] = raw:sub(i, i) end
                        i = i + 1
                    end
                end
                reading = table.concat(out)
            end
            at = stop + 1
        elseif b == 0x3c then -- <
            word = #pieces + 1
            at = at + 1
        elseif b == 0x3e then -- >
            if word and reading and #pieces >= word then
                ruby[#ruby + 1] = { word, #pieces, reading }
            end
            word, reading = nil, nil
            at = at + 1
        else
            at = at + 1 -- the other letters change how the text comes up, not what it says
        end
    end
    flush()
    return table.concat(speaker), parts
end

-- The message being shown.
local current       -- its pointer
local speaker, parts
local raw_text
local reported = 0  -- how many of its parts were reported
local complete_since
local shown = {}    -- the windows that have a layout

local function withdraw()
    for name in pairs(shown) do reader.layout(name, nil) end
    shown = {}
end

-- Entries of the text's characters `first` to `last` (from 0 over the message), each
-- { left, top, width, height, moment }. nil when the list's length does not match the message
-- (the list belongs to another message).
local function entries(first, last, total)
    local list, stop = field(GLYPHS), field(GLYPHS_END)
    if not list or not stop or list == 0 or stop <= list then return nil end
    local size = stop - list
    if size > 400 * ENTRY or size % ENTRY ~= 0 then return nil end
    local raw = mem.read(list, size)
    if not raw or #raw < size then return nil end
    local out, index = {}, 0
    for i = 0, size // ENTRY - 1 do
        local x0, y0, x1, y1 = string.unpack("<ffff", raw, i * ENTRY + 5)
        local moment = string.unpack("<I4", raw, i * ENTRY + 45)
        local advance = string.unpack("<f", raw, i * ENTRY + 49)
        if not (x0 >= 0 and x0 < 640 and y0 >= 0 and y0 < 448 and x1 > x0 and y1 > y0) then
            return nil
        end
        if y1 - y0 >= SMALL then
            if index >= first and index <= last then
                local width = (advance > 0 and advance <= x1 - x0) and advance or x1 - x0
                out[#out + 1] = { x0, y0, width, y1 - y0, moment }
            end
            index = index + 1
        end
    end
    if index ~= total then return nil end
    return out
end

reader.on_frame(function(frame)
    if not kana and not load_kana() then return end
    local pointer = field(MESSAGE_TEXT)
    if pointer ~= current then
        current = pointer
        withdraw()
        reported, complete_since, parts, speaker, raw_text = 0, nil, nil, nil, nil
        if not pointer or pointer < 0x00100000 or pointer >= 0x02000000 then return end
        -- The string's length is two bytes before it, the message command seven bytes before.
        if mem.u8(pointer - 7) ~= 0x86 then return end
        local length = mem.u16(pointer - 2)
        if not length or length == 0 or length > 4096 then return end
        raw_text = mem.read(pointer, length)
        if not raw_text then return end
        speaker, parts = parse(raw_text)
    end
    if not parts or reported >= #parts then return end

    -- Where the next part's characters start in the list (a new row has no entry).
    local first = 0
    for i = 1, reported do
        for _, piece in ipairs(parts[i].pieces) do
            if not piece.newline then first = first + 1 end
        end
    end
    local part = parts[reported + 1]
    local count = 0
    for _, piece in ipairs(part.pieces) do
        if not piece.newline then count = count + 1 end
    end
    if count == 0 then
        reported = reported + 1
        return
    end
    local total = 0
    for _, each in ipairs(parts) do
        for _, piece in ipairs(each.pieces) do
            if not piece.newline then total = total + 1 end
        end
    end
    local list = entries(first, first + count - 1, total)
    local clock = field(CLOCK)
    if not list or not clock then return end
    if clock < list[#list][5] then
        complete_since = nil
        return
    end
    complete_since = complete_since or frame
    if frame - complete_since < SHOWN_FRAMES then return end

    -- The part is complete: report its text, readings and a cell per character.
    local left, top = field(BOX_LEFT) or 45, field(BOX_TOP) or 292
    local b = reader.builder()
    local cells, ruby = {}, {}
    local starts = {} -- where each piece begins in the text
    local entry = 0
    for index, piece in ipairs(part.pieces) do
        starts[index] = b:length()
        b:add(piece.text)
        if piece.newline then
            cells[#cells + 1] = false
        else
            entry = entry + 1
            local e = list[entry]
            cells[#cells + 1] = { left + e[1], top + e[2], e[3], e[4] }
        end
    end
    for _, group in ipairs(part.ruby) do
        local from = starts[group[1]]
        local to = (starts[group[2] + 1] or b:length())
        ruby[#ruby + 1] = { from, to - from, group[3] }
    end
    reported = reported + 1
    complete_since = nil
    local window = "message" .. reported
    reader.emit{ text = b:text(), speaker = speaker, ruby = ruby, window = window,
                 id = text.hash(raw_text) .. ":" .. reported }
    reader.layout(window, { space = { 640, 448 }, glyphs = cells })
    shown[window] = true
end)
