-- reader: no hooks
-- reader: title グリザイアの果実 -LE FRUIT DE LA GRISAIA-, started from disc 1
-- reader: reads The message window with speakers, and choices
-- reader: touch press
-- reader: version ULJM-06232
-- グリザイアの果実 -LE FRUIT DE LA GRISAIA- (Prototype/Frontwing, PSP, 2013), disc 1, ULJM-06232.
-- EBOOT.BIN CAB4E2B4. Disc 2 is ULJM06233.lua.
--
-- The two discs are one program built twice: this file and its twin are the same from this
-- line on. Disc 1 installs the game's pictures and part of its voices onto the memory stick
-- and asks for disc 2; disc 2 has the other voices and the films, and asks for disc 1 when it
-- is started first. The story is played with disc 2 in the drive, by the program of whichever
-- disc was started (in PPSSPP: start disc 1, and swap to disc 2 when it asks). The game's ID
-- stays the one of the disc that was started, so either file may be the one that reads.
--
-- The game keeps the line it is showing as Shift-JIS at a fixed place, the hiragana written
-- with the single bytes of half-width katakana (ｱ is あ), the speaker in front as $t, the
-- name and @. It draws the line in rows of 368 pixels (23 full-width characters, up to three
-- rows), so the place of every character follows from the text. Nothing is hooked:
-- everything is read once a frame from objects of the game's that stay at one place.
--
-- line: the line on screen, NUL-ended.
-- window: the game's message window (an object of its class cObj_MessageWin). +8 is 1 while
--   the window is in use (0 for a frame between two lines); +0x3c points to the line's place
--   in the scene's script (0 until the first line of a new game); +0x1f8 is 0x30 while the
--   line is being typed out, 0 once it is.
-- replay: 1 while the game shows an older line again (L or ↑ step back through the story;
--   ○ and ↓ step forward to where the story was).
-- overlays: the game's other windows that come over the line (the menu of △, the message
--   log of □, the settings, the quick load, the yes/no questions): +8 is 1 while one is up.
-- select: the choice's window (cObj_SelectWin), +8 is 1 while it is up.
local BUILDS = {
    -- The two discs' programs are one program: the same code and the same places.
    ["CAB4E2B4"] = "disc 1 (ULJM-06232)",
    ["B234BF0B"] = "disc 2 (ULJM-06233)",
}
if not BUILDS[reader.build_id] then
    error("this profile is for グリザイアの果実 (PSP), ULJM-06232 and ULJM-06233 (EBOOT " ..
          "CAB4E2B4 and B234BF0B), the game is " .. tostring(reader.build_id))
end
-- The executable is not relocatable (its module begins at 0x08804018): this is zero.
local base = reader.base - 0x08804018
local LINE, LINE_SIZE = base + 0x08dcc290, 1024
local WINDOW = base + 0x08de4850
local ALIVE, PLACE, TYPING = 0x08, 0x3c, 0x1f8
local REPLAY = base + 0x08e179dc
local SELECT = base + 0x08de72d8
-- The choice window: +0x668 how many answers, +0x680 the one the cursor is on (from 0),
-- +0x9b1 the answers' text as the script has it, one after another with $d between them.
local SELECT_COUNT, SELECT_CURSOR, SELECT_TEXT = 0x668, 0x680, 0x9b1
local OVERLAYS = {
    base + 0x08df9340, -- cObj_SystemMenu (△)
    base + 0x08de41e8, -- cObj_BackLogWin (□)
    base + 0x08df66f8, -- cObj_ConfigWin
    base + 0x08df8e80, -- cObj_QuickLoad
    base + 0x08dccad8, -- cObj_AskWin
    base + 0x08dcd4b8, -- cObj_AskWin
}

-- The line has to stand, typed out, for this many frames: what a skip runs through is not read.
local SHOWN_FRAMES = 4
-- The window has to be out of use for this many frames before its line goes (between two lines
-- it is for one frame).
local IDLE_FRAMES = 8

-- Where the game draws, in pixels of the 480x272 screen (from the vertices it draws with): the
-- first cell, the step from one cell to the next and from one row to the next, a cell's height,
-- how many full cells a row holds.
local LEFT, TOP, STEP, PITCH, HEIGHT, COLUMNS = 98, 201, 16, 24, 17, 23
local ROW_WIDTH = COLUMNS * STEP
local IDEOGRAPHIC_SPACE = "\x81\x40"
-- The widths of characters of one byte that were measured; the others are taken as half a cell.
local NARROW = { ["!"] = 4.5 }
local AREA = { 90, 196, 382, 74 }
-- The answers of a choice: centred at this x, their rows this far apart around this y (the
-- first row's top, measured with two answers at 94 and 150), a character this wide.
local ANSWER_CENTRE, ANSWER_MIDDLE, ANSWER_PITCH, ANSWER_STEP = 240, 122, 56, 18
-- Marks that may stand after a full row, as a 24th character, instead of beginning the next one
-- (seen: 、 。 」; the others are the usual ones and were not met).
local HANGING = { ["、"] = true, ["。"] = true, ["」"] = true, ["』"] = true, ["）"] = true,
                  ["！"] = true, ["？"] = true, ["，"] = true, ["．"] = true }

-- The single bytes of half-width katakana stand for hiragana (and five marks) from 0xa1 on.
local SINGLE = {
    "。", "「", "」", "、", "・", "を", "ぁ", "ぃ", "ぅ", "ぇ", "ぉ", "ゃ", "ゅ", "ょ", "っ",
    "ー", "あ", "い", "う", "え", "お", "か", "き", "く", "け", "こ", "さ", "し", "す", "せ",
    "そ", "た", "ち", "つ", "て", "と", "な", "に", "ぬ", "ね", "の", "は", "ひ", "ふ", "へ",
    "ほ", "ま", "み", "む", "め", "も", "や", "ゆ", "よ", "ら", "り", "る", "れ", "ろ", "わ",
    "ん", "゛", "゜",
}

local function is_lead(c)
    return (c >= 0x81 and c <= 0x9f) or (c >= 0xe0 and c <= 0xfc)
end

-- The line (or an answer) as { speaker = name or nil, name_width = the pixels the name takes,
-- chars = { { UTF-8, width in pixels }, ... }, breaks = { [n] = true } } (a break after the
-- nth character). At the size `normal` (16 pixels in the window, 18 in a choice) most
-- characters take a full cell; the full-width space takes half of one, and characters of one
-- byte are narrower still (! is 4.5 pixels in the window: the game's !! fills about half a
-- cell). $S and three digits changes the size for what follows (a shout in large letters, a
-- whisper in small ones), $S alone puts it back.
local warned = {}
local function parse(raw, normal)
    local chars, breaks, speaker = {}, {}, nil
    local size = normal
    local i = 1
    local function char_at(at)
        local c = raw:byte(at)
        local scale = size / STEP
        if c >= 0xa1 and c <= 0xdf then return SINGLE[c - 0xa0], 1, size end
        if is_lead(c) and at < #raw then
            local pair = raw:sub(at, at + 1)
            return text.sjis(pair), 2, pair == IDEOGRAPHIC_SPACE and size / 2 or size
        end
        if c >= 0x20 and c < 0x7f then
            local ch = string.char(c)
            return ch, 1, (NARROW[ch] or STEP / 2) * scale
        end
        return nil, 1, 0
    end
    -- $t, the name, @: who speaks. The game draws the name in front of the line, in the line's
    -- cells.
    local name_width = 0
    if raw:sub(1, 2) == "$t" then
        local stop = raw:find("@", 3, true)
        if stop then
            local name = {}
            local at = 3
            while at < stop do
                local c, bytes, width = char_at(at)
                if c then name[#name + 1] = c end
                name_width = name_width + width
                at = at + bytes
            end
            speaker = table.concat(name)
            i = stop + 1
        end
    end
    while i <= #raw do
        local c = raw:byte(i)
        if c == 0x24 and i < #raw then -- $ and a letter: a command
            local command = raw:sub(i + 1, i + 1)
            if command == "d" then
                breaks[#chars] = true
            elseif command == "S" then
                local digits = raw:match("^%d+", i + 2) or ""
                size = tonumber(digits) or normal
                i = i + #digits
            elseif not warned[command] then
                warned[command] = true
                reader.warn("an unknown command $" .. command .. " in the line")
            end
            i = i + 2
        else
            local ch, bytes, width = char_at(i)
            if ch then chars[#chars + 1] = { ch, width } end
            i = i + bytes
        end
    end
    return { speaker = speaker, name_width = name_width, chars = chars, breaks = breaks }
end

-- The line's text and a cell for every character (false for a line break of the writer's).
local function compose(line)
    local b, cells = reader.builder(), {}
    local x, row = line.name_width, 0
    for n, c in ipairs(line.chars) do
        local ch, width = c[1], c[2]
        if x + width > ROW_WIDTH and not (HANGING[ch] and x <= ROW_WIDTH) then
            x, row = 0, row + 1
        end
        b:add(ch)
        cells[#cells + 1] = { LEFT + x, TOP + row * PITCH, width, HEIGHT }
        x = x + width
        if line.breaks[n] and n < #line.chars then
            b:add("\n")
            cells[#cells + 1] = false
            x, row = 0, row + 1
        end
    end
    return b:text(), cells
end

local shown_raw, shown_at  -- the line on screen that was looked at, and where in the story
local read_raw, read_at    -- the line that was reported last
local layout       -- its layout while it is on screen
local hidden = false
local stood, idle = 0, 0

local function withdraw()
    if layout and not hidden then reader.layout("message", nil) end
    layout = nil
end

local function overlaid()
    for _, at in ipairs(OVERLAYS) do
        if mem.u8(at + ALIVE) ~= 0 then return true end
    end
    return false
end

reader.on_frame(function()
    if mem.u8(WINDOW + ALIVE) ~= 1 then
        stood = 0
        idle = idle + 1
        if idle >= IDLE_FRAMES then
            withdraw()
            shown_raw, shown_at = nil, nil
        end
        return
    end
    idle = 0
    -- Under the menu, the log and the other windows nothing can be pointed at; the line comes
    -- back as it was, without being read again.
    local covered = overlaid()
    if covered ~= hidden then
        hidden = covered
        if layout then reader.layout("message", (not covered) and layout or nil) end
    end
    if covered then
        stood = 0
        return
    end
    local raw = mem.cstr(LINE, LINE_SIZE) or ""
    local at = mem.u32(WINDOW + PLACE)
    if raw == shown_raw and at == shown_at then return end
    if layout then withdraw() end
    -- A window that has no place in the script yet (for some frames as a new game begins)
    -- still has an older text of the game's in the line.
    if mem.u32(WINDOW + TYPING) ~= 0 or raw == "" or (at or 0) == 0 then
        stood = 0
        return
    end
    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0
    shown_raw, shown_at = raw, at
    -- An older line shown again (the game steps back through the story) is not read; the line
    -- the story stood at, shown again when it steps forward to it, is not read twice.
    if mem.u8(REPLAY) ~= 0 then return end
    local line = parse(raw, STEP)
    if #line.chars == 0 then return end
    local text_, cells = compose(line)
    if raw ~= read_raw or at ~= read_at then
        read_raw, read_at = raw, at
        reader.emit{ text = text_, speaker = line.speaker, window = "message" }
    end
    layout = { space = { 480, 272 }, box = AREA, glyphs = cells }
    reader.layout("message", layout)
end)

-- The choice: one event, an answer to a line, while the game's choice window is up.
local choice_read     -- the answers that were reported (they stay so under the menu)
local choice_layout   -- their layout while it is on screen
local choice_stood = 0

local function hide_choice()
    choice_stood = 0
    if reader.cursor then reader.cursor(nil) end
    if choice_layout then
        reader.layout("choice", nil)
        choice_layout = nil
    end
end

local function read_choice(count)
    local raw = mem.cstr(SELECT + SELECT_TEXT, 512) or ""
    local answers, at = {}, 1
    while true do
        local stop = raw:find("$d", at, true)
        answers[#answers + 1] = raw:sub(at, (stop or #raw + 1) - 1)
        if not stop then break end
        at = stop + 2
    end
    if #answers ~= count then return nil end
    local b, cells = reader.builder(), {}
    local top = ANSWER_MIDDLE - (count - 1) * ANSWER_PITCH / 2
    for n, answer in ipairs(answers) do
        -- An answer's characters are larger than the window's (18 pixels), unless it names
        -- a size ($S016); each answer is centred on the screen.
        local line = parse(answer, ANSWER_STEP)
        if #line.chars == 0 then return nil end
        local width = 0
        for _, c in ipairs(line.chars) do width = width + c[2] end
        local x = ANSWER_CENTRE - width / 2
        if n > 1 then
            b:add("\n")
            cells[#cells + 1] = false
        end
        for _, c in ipairs(line.chars) do
            b:add(c[1])
            cells[#cells + 1] = { x, top + (n - 1) * ANSWER_PITCH, c[2], ANSWER_STEP + 1 }
            x = x + c[2]
        end
    end
    return b:text(), cells
end

reader.on_frame(function()
    local count = mem.u32(SELECT + SELECT_COUNT) or 0
    if mem.u8(SELECT + ALIVE) ~= 1 or count < 1 or count > 8 then
        hide_choice()
        choice_read = nil
        return
    end
    -- Under the menu the answers cannot be pointed at; they come back without being read again.
    if overlaid() then
        hide_choice()
        return
    end
    if choice_layout then
        -- Which answer the cursor is on: what a tap on an answer is turned into presses with.
        local at = mem.u32(SELECT + SELECT_CURSOR)
        if at and at < count and reader.cursor then reader.cursor(at) end
        return
    end
    choice_stood = choice_stood + 1
    if choice_stood < SHOWN_FRAMES then return end
    local answers, cells = read_choice(count)
    if not answers then return end
    if answers ~= choice_read then
        choice_read = answers
        reader.emit{ text = answers, channel = "choice", window = "choice" }
    end
    choice_layout = { space = { 480, 272 }, glyphs = cells }
    reader.layout("choice", choice_layout)
end)

reader.on_state_loaded(function()
    hide_choice()
    choice_read = nil
    withdraw()
    shown_raw, shown_at, read_raw, read_at = nil, nil, nil, nil
    hidden = false
    stood, idle = 0, 0
end)
