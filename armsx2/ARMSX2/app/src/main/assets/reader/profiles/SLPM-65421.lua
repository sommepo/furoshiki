-- reader: no hooks
-- reader: title Ever17 -the out of infinity- Premium Edition
-- reader: reads The story's text with speakers, each message as it has come up in full, and choices
-- reader: version SLPM-65421, disc version 1.01
-- reader: touch press
-- reader: buttons start select up down circle cross
-- Touch play's buttons (found in the harness, 2026-10-16): START at the title (○ does nothing
-- there), up and down for the title's and the menu's lists, ○ to choose (a tap on the picture
-- is ○ too, but a button is sure), × back out; SELECT the system menu
-- (save, load), △ the message history, □ skip.
--
-- Ever17 -the out of infinity- Premium Edition (KID, PS2, 2003), SLPM-65421. Program C577D399.
--
-- Script text is Shift-JIS with %-commands. When a message starts, the game copies the text
-- without commands to a buffer and fills a table with one entry per character: offset from
-- the window's left edge, glyph columns, visibility. Rows are not numbered: a row starts
-- where a character's offset does not increase.

local BUILD = "C577D399"
if reader.build_id ~= BUILD then
    error("this profile is for Ever17 Premium Edition SLPM-65421 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

-- Addresses (the program loads at its link address).
local SCRIPT_AT = 0x00c10ea4   -- where the script is being carried out
local TEXT = 0x00c10eac        -- the message's text, commands taken out, ends with a zero
local WINDOW = 0x00c217dc      -- the text window in the picture: left, top, width, height
local COUNT = 0x00c21810       -- how many characters the message has
local ENTRIES = 0x00c21844     -- the table of characters
local ENTRY = 48               -- bytes to an entry: at +16 the place from the window's left
                               -- (signed), at +24 and +25 the glyph's first and last column
                               -- from there (bytes), at +40 how visible it is (128: fully)
local SHOWN = 128
local ANSWERS = 0x00c2b648     -- how many answers the choice on the screen has (0: none)
local ANSWER_TEXT = 0x00c2b650 -- the answers' texts, each in a buffer of its own
local ANSWER_SIZE = 64
local ANSWER_CURSOR = 0x00c2b572 -- the answer the cursor is on, from 0; 0xff until the d-pad
                               -- first puts it on one (○ picks nothing then). A byte
local FONT_COLUMNS = 0x0096b0b8 -- pointer to the font's table: two bytes for each character,
                               -- its glyph's first and last column, in the order of
                               -- Shift-JIS from 0x8140 on (188 to a lead byte)
local MIDDLE_X, MIDDLE_Y = 320, 180 -- the answers stand around this point of the picture,
                               -- each row centred (measured in pictures of the game)
local ROW = 26                 -- from one row's top to the next, in the picture's lines
local HEIGHT = 24              -- a character's height
local TEXT_MAX = 1024
local OPEN, CLOSE = "「", "」"
local NAME_MAX = 12            -- characters of a speaker's name at most

-- Frames a message must stay complete before it is reported; skipped text is not read.
local SHOWN_FRAMES = 4

local function is_lead(b)
    return (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)
end

-- The characters of a text as the game counts them: { text, bytes }.
local function characters(raw)
    local out, at = {}, 1
    while at <= #raw do
        local b = raw:byte(at)
        if is_lead(b) and at < #raw then
            out[#out + 1] = text.sjis(raw:sub(at, at + 1))
            at = at + 2
        else
            out[#out + 1] = string.char(b)
            at = at + 1
        end
    end
    return out
end

local shown_raw      -- the text of the message that was reported, as bytes
local shown_at       -- where the script was when it was
local reported = 0   -- how many characters of it were reported
local parts = 0
local complete_since
local windows = {}

local function withdraw()
    for name in pairs(windows) do reader.layout(name, nil) end
    windows = {}
end

local function reset()
    withdraw()
    shown_raw, shown_at, reported, parts, complete_since = nil, nil, 0, 0, nil
end

-- A choice's answers as one event, one answer per line. The game keeps no character table
-- for them: each row is laid out like a message row (glyph columns plus four empty ones)
-- and centred.
local answers_shown
local function choice()
    local number = mem.u32(ANSWERS)
    if not number or number == 0 or number > 8 then
        if answers_shown then
            reader.layout("choice", nil)
            answers_shown = nil
        end
        return
    end
    local raw = mem.read(ANSWER_TEXT, number * ANSWER_SIZE)
    if not raw or raw == answers_shown then return end
    local columns = mem.u32(FONT_COLUMNS)
    local b, cells = reader.builder(), {}
    for row = 1, number do
        local chars_raw = raw:sub((row - 1) * ANSWER_SIZE + 1, row * ANSWER_SIZE):match("^[^%z]*")
        if #chars_raw == 0 then return end -- not all there yet
        local row_cells, x, at = {}, 0, 1
        if row > 1 then
            b:add("\n")
            cells[#cells + 1] = false
        end
        while at <= #chars_raw do
            local lead = chars_raw:byte(at)
            local from, to = 0, 11
            if is_lead(lead) and at < #chars_raw then
                local trail = chars_raw:byte(at + 1)
                b:add(text.sjis(chars_raw:sub(at, at + 1)))
                local index = ((lead >= 0xe0 and lead - 0xc1 or lead - 0x81)) * 188 + trail - 0x40 -
                              (trail > 0x7f and 1 or 0)
                local pair = columns and mem.read(columns + index * 2, 2)
                if pair and #pair == 2 and pair:byte(2) > pair:byte(1) then
                    from, to = pair:byte(1), pair:byte(2)
                else
                    from, to = 1, 22
                end
                at = at + 2
            else
                b:add(string.char(lead))
                at = at + 1
            end
            row_cells[#row_cells + 1] = { x - 2, to - from + 4 }
            x = x + to - from + 4
        end
        local left = MIDDLE_X - (x - 4) / 2
        local top = MIDDLE_Y - number * ROW / 2 + (row - 1) * ROW
        for _, cell in ipairs(row_cells) do
            cells[#cells + 1] = { left + cell[1], top, cell[2], HEIGHT }
        end
    end
    answers_shown = raw
    reader.emit{ text = b:text(), channel = "choice", window = "choice",
                 id = "choice:" .. text.hash(raw) }
    reader.layout("choice", { space = { 640, 448 }, glyphs = cells })
end

-- Touch play: which answer the cursor is on while a choice is up, -1 before the d-pad has put
-- it on one; nothing once the choice has gone. An older app has no reader.cursor.
local cursor_said
local function report_cursor()
    local at
    local number = mem.u32(ANSWERS) or 0
    if answers_shown and number >= 1 and number <= 8 then
        at = mem.u8(ANSWER_CURSOR) or 0xff
        if at >= number then at = -1 end
    end
    if at == cursor_said then return end
    cursor_said = at
    if reader.cursor then reader.cursor(at) end
end

reader.on_frame(function(frame)
    choice()
    report_cursor()
    local count = mem.u32(COUNT)
    if not count or count == 0 or count > 400 then
        if shown_raw then reset() end
        complete_since = nil
        return
    end
    local buffer = mem.read(TEXT, TEXT_MAX)
    if not buffer then return end
    local raw = buffer:match("^[^%z]*")
    local at = mem.u32(SCRIPT_AT)
    if raw == shown_raw and at == shown_at then return end

    local chars = characters(raw)
    if #chars ~= count then
        complete_since = nil
        return
    end
    local table_raw = mem.read(ENTRIES, count * ENTRY)
    if not table_raw or #table_raw < count * ENTRY then return end
    -- Complete when the second-to-last character is fully visible (the last entry carries the
    -- wait mark and has no visibility).
    local settled = count < 2 and 0 or (count - 2) * ENTRY
    if count >= 2 and table_raw:byte(settled + 41) ~= SHOWN then
        complete_since = nil
        return
    end
    complete_since = complete_since or frame
    if frame - complete_since < SHOWN_FRAMES then return end
    complete_since = nil

    -- A message continued after a wait starts with the earlier text: only the new part is
    -- reported.
    local first = 1
    if shown_raw and #raw > #shown_raw and raw:sub(1, #shown_raw) == shown_raw and reported < count then
        first = reported + 1
    else
        withdraw()
        parts = 0
    end

    -- The speaker's name precedes 「 in the same text, with 」 at the end. The game draws the
    -- name in its own box above the text, so rows are counted after it.
    local speaker, said = nil, 1
    if chars[count] == CLOSE then
        for index = 2, math.min(count - 1, NAME_MAX + 1) do
            if chars[index] == OPEN then
                speaker = table.concat(chars, "", 1, index - 1)
                said = index
                break
            elseif chars[index] == "\n" then
                break
            end
        end
    end
    if first < said then first = said end

    local left, top = mem.u32(WINDOW) or 34, mem.u32(WINDOW + 4) or 344
    local cells, row, before = {}, 0, nil
    for index = said, count do
        local base = (index - 1) * ENTRY
        local x = string.unpack("<i4", table_raw, base + 17)
        local from, to = table_raw:byte(base + 25), table_raw:byte(base + 26)
        if before and x <= before then row = row + 1 end
        before = x
        if index >= first then
            -- Two columns are left empty on either side of a glyph.
            cells[#cells + 1] = { left + x + from - 2, top + row * ROW, math.max(to - from, 0) + 4, HEIGHT }
        end
    end

    local b = reader.builder()
    for index = first, count do b:add(chars[index]) end
    local whole = b:text()
    if first > said then speaker = nil end -- said already, with the message's first part
    parts = parts + 1
    shown_raw, shown_at, reported = raw, at, count
    local window = "message" .. parts
    reader.emit{ text = whole, speaker = speaker, window = window,
                 id = text.hash(raw) .. ":" .. tostring(at) .. ":" .. parts }
    reader.layout(window, { space = { 640, 448 }, glyphs = cells })
    windows[window] = true
end)
