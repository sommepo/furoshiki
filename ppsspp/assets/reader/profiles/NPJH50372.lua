-- reader: no hooks
-- reader: title ダンガンロンパ 希望の学園と絶望の高校生 (Danganronpa)
-- reader: reads The message box with speakers: the story, investigation talk, the tutorial
-- reader: touch press
-- reader: buttons up down left right l r circle cross triangle start
-- reader: version NPJH-50372
-- ダンガンロンパ 希望の学園と絶望の高校生 (Spike, PSP, 2010), the PlayStation Store
-- release NPJH-50372. EBOOT.BIN DCE18276.
--
-- The game keeps the message the box shows as UTF-16 at a fixed place, row by row as the
-- box shows them (the script's own line breaks), whole from the moment it begins to be
-- typed out, with the number of the character whose name plate the box shows. A flag says
-- when the message is typed out and waits for the button. The plate is a picture: the
-- names below are the plates' as they read. Where each character stands follows from the
-- game's font, whose widths are read from the game's memory. Nothing is hooked.
--
-- message: from +4, five rows of 54 UTF-16 units each (0x6c bytes), each ended by a zero;
--   +2 (two bytes) the speaker's number; from +0x220, a byte per character, its colour.
-- waiting: a byte, 1 once the message is typed out and waits for the button (0 again as
--   soon as the next one begins); the byte before it counts how far the typing has got.
-- system: a byte, 1 while the game's SYSTEM page (the help, START) is over everything.
-- pad: the buttons the game sees held; × held runs through the text.
-- font: the game's message font (an "SpFt"): +0x20 a pointer to its glyphs (16 bytes
--   each: u16 character, u16 x, y, width, height...), +0x24 a pointer to a u16 per UTF-16
--   unit, the glyph's number (0xffff for none).
local BUILD = "DCE18276"
if reader.build_id ~= BUILD then
    error("this profile is for Danganronpa NPJH-50372 (EBOOT " .. BUILD .. "), the game is " ..
          tostring(reader.build_id))
end
local base = reader.base - 0x08804000 -- the executable is not relocatable: this is zero
local MESSAGE = base + 0x08c9a260
local SPEAKER = 2
local ROWS, ROW_FROM, ROW_SIZE = 5, 4, 0x6c
local WAITING = base + 0x08c9a591
local SYSTEM = base + 0x08993fb6
local PAD = base + 0x08990538
local PAD_CROSS = 0x4000
local FONT = base + 0x08a1de6c
local FONT_MAGIC = "tFpS"
local FONT_GLYPHS, FONT_MAP, GLYPH = 0x20, 0x24, 16

-- The message has to stand typed out for this many frames, × not held: what a skip runs
-- through (× held stops on each message for six frames or more) is not read.
local SHOWN_FRAMES = 8

-- The name plates, by the speaker's number. 23 is a plate that says ナレーション
-- (narration) and is read as no speaker; 26 to 29 show the plate of 25 and 31 and above
-- none (31 is the tutorial's voice).
local NAMES = {
    [0] = "ナエギ　マコト", "イシマル　キヨタカ", "トガミ　ビャクヤ", "オオワダ　モンド",
    "クワタ　レオン", "ヤマダ　ヒフミ", "ハガクレ　ヤスヒロ", "マイゾノ　サヤカ",
    "キリギリ　キョウコ", "アサヒナ　アオイ", "フカワ　トウコ", "オオガミ　サクラ", "セレス",
    "エノシマ　ジュンコ", "フジサキ　チヒロ", "モノクマ", "エノシマ　ジュンコ", "アルターエゴ",
    "ジェノサイダー　ショウ", "ガクエンチョウ", "ナエギ・ハハ", "ナエギ・チチ", "ナエギ・イモウト",
    nil, "イシマル・オオワダ", "オオワダ　ダイア", "オオワダ　ダイア", "オオワダ　ダイア",
    "オオワダ　ダイア", "オオワダ　ダイア", "？？？",
}

-- Where the box's text is drawn, in pixels of the 480x272 screen (measured in
-- screenshots): the first character's left edge, the first row's top, the step between
-- rows and a character's height. A character is as wide as the font says (16 for a
-- full-width one, 8 for “ ”).
local LEFT, TOP, PITCH, HEIGHT = 17, 208, 24, 20
local AREA = { 8, 180, 464, 88 }

-- Every row break is the script's own (the game does not wrap). A row that ends like this
-- ends a sentence: the break after it is kept; a break in the middle of a sentence is left
-- out, so that a word or sentence that runs on is one.
local SENTENCE_END = {}
for _, c in ipairs{ "。", "！", "？", "」", "』", "）", "…", "‥", "!", "?", "♪", "”" } do
    SENTENCE_END[c] = true
end

local font_glyphs, font_map -- the font's tables, once found
local widths = {}            -- the font's width of a character, by UTF-16 unit

local function width_of(unit)
    local known = widths[unit]
    if known then return known end
    if not font_glyphs then
        if mem.read(FONT, 4) ~= FONT_MAGIC then return 16 end
        font_glyphs, font_map = mem.u32(FONT + FONT_GLYPHS), mem.u32(FONT + FONT_MAP)
        if not font_glyphs or not font_map then
            font_glyphs, font_map = nil, nil
            return 16
        end
    end
    local glyph = mem.u16(font_map + unit * 2)
    local w = glyph and glyph ~= 0xffff and mem.u16(font_glyphs + glyph * GLYPH + 6) or nil
    if not w or w == 0 or w > 32 then w = 16 end
    widths[unit] = w
    return w
end

-- A UTF-16 unit as UTF-8 (the game's font has no characters outside the first plane).
local function char_of(unit)
    if unit >= 0xd800 and unit < 0xe000 then return "\u{fffd}" end
    return utf8.char(unit)
end

-- The rows of the message as lists of UTF-16 units.
local function read_rows(raw)
    local rows = {}
    for r = 0, ROWS - 1 do
        local units = {}
        local at = ROW_FROM + r * ROW_SIZE
        for i = at + 1, at + ROW_SIZE - 1, 2 do
            local unit = string.unpack("<I2", raw, i)
            if unit == 0 then break end
            units[#units + 1] = unit
        end
        rows[#rows + 1] = units
    end
    -- Rows after the last that has text are not shown.
    while #rows > 0 and #rows[#rows] == 0 do rows[#rows] = nil end
    return rows
end

-- The message as text and a cell for every character (false for a kept line break).
local function compose(rows)
    local b, cells = reader.builder(), {}
    local last -- the last character of the text so far
    for r, units in ipairs(rows) do
        local x = LEFT
        local top = TOP + (r - 1) * PITCH
        for n, unit in ipairs(units) do
            local w = width_of(unit)
            local c = char_of(unit)
            if n == 1 and r > 1 then
                if SENTENCE_END[last] then
                    b:add("\n")
                    cells[#cells + 1] = false
                end
            end
            -- A row's leading full-width space is an indent (a letter's rows), not text.
            if not (n == 1 and c == "　" and r > 1) then
                b:add(c)
                cells[#cells + 1] = { x, top, w, HEIGHT }
                last = c
            end
            x = x + w
        end
    end
    return b:text(), cells
end

local shown        -- the layout of the message that was read, while it is on screen
local reported     -- the message that was read (its rows' bytes), until the next begins
local hidden = false
local stood = 0

local function withdraw()
    if shown and not hidden then reader.layout("message", nil) end
    shown, reported = nil, nil
end

reader.on_frame(function()
    -- Under the SYSTEM page nothing can be pointed at; the cells come back afterwards.
    local up = mem.u8(SYSTEM) == 1
    if up ~= hidden then
        hidden = up
        if shown then reader.layout("message", (not up) and shown or nil) end
    end
    if mem.u8(WAITING) ~= 1 then
        -- The next message has begun (or the box is gone): the last one goes.
        stood = 0
        if reported then withdraw() end
        return
    end
    if hidden then return end
    local raw = mem.read(MESSAGE, ROW_FROM + ROWS * ROW_SIZE)
    if not raw or #raw < ROW_FROM + ROWS * ROW_SIZE then return end
    local body = raw:sub(ROW_FROM + 1)
    if reported then
        if body == reported then return end
        withdraw()
    end
    if ((mem.u32(PAD) or 0) & PAD_CROSS) ~= 0 then
        stood = 0
        return
    end
    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0
    local rows = read_rows(raw)
    if #rows == 0 then return end
    local text, cells = compose(rows)
    reported = body
    if text == "" then return end
    local speaker = NAMES[string.unpack("<I2", raw, SPEAKER + 1)]
    reader.emit{ text = text, speaker = speaker, window = "message" }
    shown = { space = { 480, 272 }, box = AREA, glyphs = cells }
    reader.layout("message", shown)
end)

-- The choice ("外に出ますか？" and its はい／いいえ): the script's state, an object of the
-- game's heap that a pointer at a fixed place leads to, keeps it. The question is the
-- message in the box, read above; the answers are rows like the box's (UTF-16, 0x6c bytes
-- apart). state: 1 to 3 while the answers come up, 4 while they wait for the player, 7 and 8
-- while the cursor moves, 5 and 6 once one was picked, 0 when there is none.
local SCRIPT = base + 0x08995434
local CHOICE_COUNT = 0x776c   -- a byte: how many answers
local CHOICE_STATE = 0x7774
local CHOICE_CURSOR = 0x7778  -- the answer the cursor is on, from 0 (the first is lit at first)
local CHOICE_ANSWERS = 0x7958
local CHOICE_UP = { [4] = true, [7] = true, [8] = true }

-- Where the answers are drawn (measured with two answers): each on a bar of its own, 25
-- apart from y 93; an answer's text begins at x 226, the lit one's at 186 once its bar has
-- slid out (it takes some ten frames). A character is as wide as the font says. Where three
-- or more answers stand was not seen.
local ANSWER_LEFT, ANSWER_LIT_LEFT = 226, 186
local ANSWER_TOP, ANSWER_PITCH, ANSWER_HEIGHT = 93, 25, 17

local function script_state()
    local at = mem.u32(SCRIPT)
    if not at or at < 0x08800000 or at >= 0x0a000000 then return nil end
    return at
end

local function read_answers(state, count)
    local answers = {}
    for k = 0, count - 1 do
        local raw = mem.read(state + CHOICE_ANSWERS + k * ROW_SIZE, ROW_SIZE)
        if not raw or #raw < ROW_SIZE then return nil end
        local units = {}
        for i = 1, ROW_SIZE - 1, 2 do
            local unit = string.unpack("<I2", raw, i)
            if unit == 0 then break end
            units[#units + 1] = unit
        end
        if #units == 0 then return nil end
        answers[#answers + 1] = units
    end
    return answers
end

local function answer_cells(answers, cursor)
    local cells = {}
    for k, units in ipairs(answers) do
        if k > 1 then cells[#cells + 1] = false end
        local x = (k - 1 == cursor) and ANSWER_LIT_LEFT or ANSWER_LEFT
        local top = ANSWER_TOP + (k - 1) * ANSWER_PITCH
        for _, unit in ipairs(units) do
            local w = width_of(unit)
            cells[#cells + 1] = { x, top, w, ANSWER_HEIGHT }
            x = x + w
        end
    end
    return cells
end

local choice        -- the answers that were read, while they are up
local choice_cursor -- the cursor their cells were laid out for
local choice_stood = 0

local function close_choice()
    choice_stood = 0
    if reader.cursor then reader.cursor(nil) end
    if choice then
        reader.layout("choice", nil)
        choice, choice_cursor = nil, nil
    end
end

reader.on_frame(function()
    local state = script_state()
    local up = state and CHOICE_UP[mem.u32(state + CHOICE_STATE) or 0]
    if not up or mem.u8(SYSTEM) == 1 then
        close_choice()
        return
    end
    local cursor = mem.u32(state + CHOICE_CURSOR) or 0
    if choice then
        if cursor ~= choice_cursor and cursor < #choice then
            choice_cursor = cursor
            reader.layout("choice", { space = { 480, 272 }, glyphs = answer_cells(choice, cursor) })
            if reader.cursor then reader.cursor(cursor) end
        end
        return
    end
    choice_stood = choice_stood + 1
    if choice_stood < SHOWN_FRAMES then return end
    local count = mem.u8(state + CHOICE_COUNT) or 0
    if count < 1 or count > 8 then return end
    local answers = read_answers(state, count)
    if not answers then return end
    local b = reader.builder()
    for k, units in ipairs(answers) do
        if k > 1 then b:add("\n") end
        for _, unit in ipairs(units) do b:add(char_of(unit)) end
    end
    reader.emit{ text = b:text(), channel = "choice", window = "choice" }
    choice, choice_cursor = answers, cursor
    reader.layout("choice", { space = { 480, 272 }, glyphs = answer_cells(answers, cursor) })
    if reader.cursor and cursor < count then reader.cursor(cursor) end
end)

reader.on_state_loaded(function()
    withdraw()
    close_choice()
    hidden, stood = false, 0
    widths, font_glyphs, font_map = {}, nil, nil
end)
