-- reader: title 学校であった怖い話S
-- reader: reads The story's text, written down the page, paragraph by paragraph, and its choices
-- reader: version SLPS-00404 (boot program 8A7E3290)
-- reader: no hooks
-- reader: touch press
-- reader: buttons left right circle
--
-- Gakkou de Atta Kowai Hanashi S (Banpresto, 1996), the Japanese disc. The game lays a whole
-- page out at once in a buffer of its own, one 16-bit Shift-JIS code per character (the
-- player's own words, which differ for a boy and a girl, already chosen), with codes of its
-- own for where a column wraps and where a paragraph ends; a counter says how much of the page
-- is shown. The page is written top to bottom in columns from the right. The profile reads
-- that once a frame and reports each paragraph when the counter stops at its end.
--
-- A choice's question is a page of its own in a second buffer, and its answers stand in the
-- next columns to the left, numbered ("１・知ってる"). The game draws them one at a time
-- through the page buffer, so only the last is left there (which says how many there are);
-- their words are read from the script, where the game's pointer then stands at them.
--
-- While a choice is up the profile says which answer the game's cursor is on (reader.cursor),
-- so that a tap on an answer picks it. Left and right move that cursor, the first answer at the
-- right; a reported cursor counts from the left, so the choice's text lists the answers from
-- the left too (the last first). The buttons line above is a note of what picks an answer with
-- the controller. See docs/reader/games/gakkou-de-atta-kowai-hanashi.md.

local BUILD = "8A7E3290"
if reader.build_id ~= BUILD then
    error("this profile is for the boot program " .. BUILD .. ", the disc has " .. tostring(reader.build_id))
end

local PAGE = 0x80061858   -- u16[]: the page's codes
local QUESTION = 0x80061058 -- u16[]: a choice's question, laid out the same way
local SHOWN = 0x80060D18  -- u16: how many of them are shown
local SCRIPT = 0x80060CB8 -- u32: where the game is in the scene's script (at a choice, its answers)
local CURSOR = 0x80060DE8 -- u8: the answer the game's cursor is on (0 the first, at the right)
local ANSWER_PAD = 0xFF04 -- what an answer's column is filled out with
local WRAP = 0xFF03       -- the column is full: the text goes on in the next
local PARAGRAPH = 0x0D0A  -- a paragraph ends, and a new column begins
local PARAGRAPH_AT_WRAP = 0xFF02 -- a paragraph ends where its column did
local PAGE_END = 0xFF01
local LONGEST = 600       -- codes a page may have

-- Where a character of the page is drawn, in the 320-wide picture: columns 30 pixels apart
-- from the right, characters 15 pixels apart from the top.
local COLUMN_X, COLUMN_STEP, CELL_W = 288, 30, 16
local ROW_Y, ROW_STEP = 9, 15

-- The codes of a page, to its end. A choice's question ends with a zero instead (`open`).
local function read_page(at, open)
    local raw = mem.read(at or PAGE, LONGEST * 2)
    if not raw or #raw < 2 then return nil end
    local codes = {}
    for i = 1, #raw - 1, 2 do
        local code = string.unpack("<I2", raw, i)
        if code == 0 then return open and #codes > 0 and codes or nil end
        codes[#codes + 1] = code
        if code == PAGE_END then return codes end
    end
    return nil
end

local page_key = nil
local said = 0       -- codes of the page already reported
local windows = 0
local last_shown = -1
local still = 0      -- frames the counter has not moved

local function withdraw_all()
    for n = 1, windows do reader.layout("page" .. n, nil) end
    windows = 0
end

-- The characters of codes from..to (1-based, inclusive) with their cells.
local function piece(codes, from, to)
    local column, row = 0, 0
    local chars, cells = {}, {}
    for i = 1, to do
        local code = codes[i]
        if code == WRAP or code == PARAGRAPH then
            column, row = column + 1, 0
        elseif code == PARAGRAPH_AT_WRAP or code == PAGE_END then
            -- nothing drawn
        else
            if i >= from then
                chars[#chars + 1] = text.sjis(string.char(code >> 8, code & 0xFF))
                cells[#cells + 1] = { COLUMN_X - column * COLUMN_STEP, ROW_Y + row * ROW_STEP, CELL_W, ROW_STEP }
            end
            row = row + 1
        end
    end
    return table.concat(chars), cells
end

-- A choice is up: the page buffer holds the last answer drawn ("２・…", filled out with
-- ANSWER_PAD). How many answers there are, or nothing.
local function answer_count(codes)
    if not codes or #codes < 3 or codes[2] ~= 0x8145 then return nil end
    local n = codes[1] - 0x824F -- "１" is 0x8250
    if n < 2 or n > 9 then return nil end
    for _, code in ipairs(codes) do
        if code == ANSWER_PAD then return n end
    end
    return nil
end

local choice_key = nil
local choice_count = 0

local function emit_choice(count)
    -- The question, once, as a paragraph.
    local question = read_page(QUESTION, true)
    if not question then return end
    local key = table.concat(question, ",", 1, math.min(#question, 24))
    -- How many columns the question takes: the answers stand in the ones after.
    local last = #question
    while last > 0 and (question[last] == PARAGRAPH or question[last] == PAGE_END or
                        question[last] == PARAGRAPH_AT_WRAP) do
        last = last - 1
    end
    local columns = 1
    for i = 1, last do
        if question[i] == WRAP or question[i] == PARAGRAPH then columns = columns + 1 end
    end
    -- The answers, from the script, where the game's pointer stands at the zero that ends the
    -- question: for each, a zero, three bytes of the game's own, the words.
    local at = mem.u32(SCRIPT)
    if not at then return end
    local answers = {}
    for n = 1, count do
        local words = mem.cstr(at + 4, 64)
        if not words or words == "" then return end
        answers[n] = text.sjis(words)
        at = at + 4 + #words
    end
    local body = table.concat(answers, "\n")
    if key .. body == choice_key then return end
    choice_key = key .. body
    withdraw_all()
    page_key, said = nil, 0
    local qtext, qcells = piece(question, 1, #question)
    local width, height = reader.picture()
    local space = { width or 320, height or 224 }
    if qtext ~= "" then
        windows = windows + 1
        reader.emit{ text = qtext, window = "page" .. windows, id = text.hash(qtext) }
        reader.layout("page" .. windows, { space = space, glyphs = qcells })
    end
    -- Each answer in a column of its own, its number and a dot first, as the game shows it;
    -- from the left, as the cursor is reported.
    local chars, cells = {}, {}
    for n = count, 1, -1 do
        local words = answers[n]
        if n < count then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
        local x = COLUMN_X - (columns + n - 1) * COLUMN_STEP
        local row = 0
        for _, ch in utf8.codes(utf8.char(0xFF10 + n) .. "・" .. words) do
            chars[#chars + 1] = utf8.char(ch)
            cells[#cells + 1] = { x, ROW_Y + row * ROW_STEP, CELL_W, ROW_STEP }
            row = row + 1
        end
    end
    local choice = table.concat(chars)
    reader.emit{ text = choice, window = "choice", channel = "choice", id = "choice:" .. text.hash(choice) }
    reader.layout("choice", { space = space, glyphs = cells })
    choice_count = count
end

reader.on_frame(function()
    local codes = read_page()
    -- An answer drawn through the page buffer ends with a zero, as a question does.
    local count = answer_count(codes or read_page(PAGE, true))
    if count then
        emit_choice(count)
        if choice_key then
            local at = mem.u8(CURSOR)
            reader.cursor(at and at < choice_count and choice_count - 1 - at or -1, "horizontal")
        end
        return
    end
    if choice_key then
        reader.layout("choice", nil)
        choice_key = nil
        reader.cursor(nil)
    end
    if not codes then
        if page_key then withdraw_all(); page_key = nil end
        return
    end
    -- A new page: its first codes differ, or the counter went back.
    local key = table.concat(codes, ",", 1, math.min(#codes, 24))
    local shown = mem.u16(SHOWN) or 0
    if key ~= page_key or shown < said then
        withdraw_all()
        page_key, said, last_shown, still = key, 0, -1, 0
    end
    if shown ~= last_shown then
        last_shown, still = shown, 0
        return
    end
    still = still + 1
    -- The counter has stopped at the end of a paragraph or of the page: the game waits.
    if still ~= 2 or shown <= said or shown > #codes then return end
    local last = codes[shown]
    if last ~= PARAGRAPH and last ~= PARAGRAPH_AT_WRAP and last ~= PAGE_END then return end
    local body, cells = piece(codes, said + 1, shown)
    said = shown
    if body == "" then return end
    windows = windows + 1
    local width, height = reader.picture()
    reader.emit{ text = body, window = "page" .. windows, id = text.hash(body) }
    reader.layout("page" .. windows, { space = { width or 320, height or 224 }, glyphs = cells })
end)

reader.on_state_loaded(function()
    withdraw_all()
    if choice_key then reader.layout("choice", nil) end
    choice_key = nil
    reader.cursor(nil)
    page_key, said, last_shown, still = nil, 0, -1, 0
end)
