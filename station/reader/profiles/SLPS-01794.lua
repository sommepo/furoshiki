-- reader: title かまいたちの夜 特別篇
-- reader: reads The story's text over the pictures, paragraph by paragraph, and its choices
-- reader: version SLPS-01794 (boot program AD2BBA84)
-- reader: audio never
-- reader: touch press
--
-- Kamaitachi no Yoru: Tokubetsu-hen (Chunsoft, 1998), the Japanese disc. The text is drawn
-- straight over the scene, a glyph at a time, by one routine of the game's that takes the
-- glyph's place and its Shift-JIS code; it leaves the glyph's width where the next call can
-- read it. A flag is up while the game waits for a press. A page grows paragraph by
-- paragraph until the game clears it and begins again at the top. A choice is drawn as the
-- page's last lines, each answer in a colour of its own (its number), while the game's choice
-- flag is up. The game draws its pointer beside the answer it is on, every frame, through the
-- same routine: its place says which answer that is (reader.cursor), so that a tap on an
-- answer picks it. The welcome page's question (お客様のお名前は…: そのまま はじめる / 名前を
-- 変える) is such a choice too. A word of the game's says which of its routines runs each turn:
-- when it leaves the story's for the title's and its menus' (also when the attract demo ends),
-- the page is withdrawn. See docs/reader/games/kamaitachi.md.

local BUILD = "AD2BBA84"
if reader.build_id ~= BUILD then
    error("this profile is for the boot program " .. BUILD .. ", the disc has " .. tostring(reader.build_id))
end

local DRAW_GLYPH = 0x800380EC -- void DrawGlyph(int x, int y, int sjis): a0, a1, a2
local LAST_WIDTH = 0x800792DA -- u8: the width of the glyph drawn last
local COLOUR     = 0x800792DC -- u8: the colour of the glyph being drawn (1.. an answer's)
local WAITING    = 0x8007943A -- u8: 1 while the game waits for a press
local CHOOSING   = 0x80079295 -- u8: 1 while a choice is up
local PROGRAM    = 0x8007942C -- u32: the routine the game runs each turn
local STORY      = 0x801855D0 -- its value while the story runs (also the welcome page, the demo)
local CURSOR_A, CURSOR_B = 0x8483, 0x8484 -- the blinking marks that ask for a press: not text
local POINTER    = 0x848B     -- the pointer beside the answer of a choice the cursor is on
local HEIGHT = 18             -- a line's height in the picture (the font is 16 high)
local RIGHT = 312             -- where a full line ends

local page = {}       -- every glyph on the page now: {x, y, code, colour, w, at (the frame)}
local said = 0        -- how many of them were emitted already
local windows = {}    -- the layouts reported for the page's paragraphs
local last_waiting = 0
local in_story = nil
local choice_key = nil
local choice_rows = {} -- the line each answer of the choice begins on (its glyphs' y)
local pointer_y = nil  -- where the game drew its pointer last, while the choice is up
local quiet = 0       -- frames since a glyph was last drawn
local frame = 0
local LEAD = 30       -- frames the story's first glyphs may come before its routine runs

local function decode(code)
    if code < 0x100 then return text.sjis(string.char(code)) end
    return text.sjis(string.char(code >> 8, code & 0xFF))
end

local function withdraw_all()
    for _, w in ipairs(windows) do reader.layout(w, nil) end
    windows = {}
    if choice_key then reader.layout("choice", nil) end
    choice_key = nil
    choice_rows, pointer_y = {}, nil
    reader.cursor(nil)
end

local function clear_page()
    withdraw_all()
    page, said = {}, 0
end

reader.hook(DRAW_GLYPH, function(cpu)
    local code = cpu.a2 & 0xFFFF
    if code == CURSOR_A or code == CURSOR_B then return end
    local x, y = cpu.a0 & 0xFFFF, cpu.a1 & 0xFFFF
    -- The pointer is no text, and drawn beside an answer it is no new page either.
    if code == POINTER then
        pointer_y = y
        return
    end
    local last = page[#page]
    if last then
        -- The width of the glyph before this one is where the routine left it.
        if not last.w then last.w = mem.u8(LAST_WIDTH) end
        -- Back above the last line: the game cleared the page and began a new one.
        if y + HEIGHT / 2 < last.y then clear_page() end
    end
    -- A glyph drawn again where it already is (a menu redrawn every frame) is not new.
    for i = #page, math.max(1, #page - 64), -1 do
        local g = page[i]
        if g.x == x and g.y == y and g.code == code then return end
    end
    page[#page + 1] = { x = x, y = y, code = code, colour = mem.u8(COLOUR), at = frame }
    quiet = 0
end)

-- The text and cells of glyphs from..to; a line that ends short of the right edge ends its
-- sentence there (a new line in the text), one that runs to the edge (within a few glyphs:
-- the game wraps before punctuation) goes on.
local function piece(from, to, keep)
    local chars, cells = {}, {}
    for i = from, to do
        local g = page[i]
        if keep == nil or keep(g) then
            local prev = chars[1] and page[i - 1]
            if prev and g.y > prev.y + HEIGHT / 2 and prev.x + (prev.w or 14) < RIGHT - 40 then
                chars[#chars + 1] = "\n"; cells[#cells + 1] = false
            end
            chars[#chars + 1] = decode(g.code)
            cells[#cells + 1] = { g.x, g.y, g.w or 14, HEIGHT }
        end
    end
    return table.concat(chars), cells
end

local function emit_paragraph()
    local last = page[#page]
    if not last or said >= #page then return end
    if not last.w then last.w = mem.u8(LAST_WIDTH) end
    -- While a choice is up its answers (in their colours) are not part of the paragraph.
    local choosing = (mem.u8(CHOOSING) or 0) == 1
    local body, cells = piece(said + 1, #page, function(g)
        return not (choosing and g.colour >= 1 and g.colour <= 7)
    end)
    said = #page
    if body == "" then return end
    local window = "page" .. (#windows + 1)
    windows[#windows + 1] = window
    reader.emit{ text = body, window = window, id = text.hash(body) }
    reader.layout(window, { space = { 320, 240 }, glyphs = cells })
end

local function emit_choice()
    local answers = {}
    for _, g in ipairs(page) do
        if g.colour >= 1 and g.colour <= 7 then
            answers[g.colour] = answers[g.colour] or {}
            table.insert(answers[g.colour], g)
        end
    end
    local chars, cells, key, rows = {}, {}, {}, {}
    for n = 1, 7 do
        local list = answers[n]
        if not list then break end
        rows[n] = list[1].y
        if n > 1 then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
        for i, g in ipairs(list) do
            if not g.w then g.w = (list[i + 1] and list[i + 1].x - g.x) or 14 end
            chars[#chars + 1] = decode(g.code)
            cells[#cells + 1] = { g.x, g.y, g.w, HEIGHT }
            key[#key + 1] = string.format("%d:%d:%X", g.x, g.y, g.code)
        end
    end
    if #chars == 0 then return end
    key = table.concat(key, ",")
    if key == choice_key then return end
    choice_key = key
    choice_rows = rows
    local body = table.concat(chars)
    reader.emit{ text = body, window = "choice", channel = "choice", id = "choice:" .. text.hash(body) }
    reader.layout("choice", { space = { 320, 240 }, glyphs = cells })
end

-- Which answer the pointer is beside (from 0), or -1 while it is beside none.
local function pointed()
    if not pointer_y then return -1 end
    for n, y in ipairs(choice_rows) do
        if math.abs(pointer_y - y) < HEIGHT / 2 then return n - 1 end
    end
    return -1
end

reader.on_frame(function()
    quiet = quiet + 1
    frame = frame + 1
    -- The story ended (the game went to its title or its menus): the page is gone.
    local story = mem.u32(PROGRAM) == STORY
    if not story and in_story then clear_page() end
    -- The story begins: what the menus drew before is no text (the save book's 未使用), but the
    -- story's first glyphs may come a few frames before its routine does.
    if story and in_story == false then
        local recent = {}
        for _, g in ipairs(page) do
            if g.at and g.at >= frame - LEAD then recent[#recent + 1] = g end
        end
        clear_page()
        page = recent
    end
    in_story = story
    local waiting = mem.u8(WAITING) or 0
    -- The game waits for a press: what it drew since the last wait is a paragraph.
    if waiting == 1 and last_waiting ~= 1 then emit_paragraph() end
    last_waiting = waiting
    -- A choice: once its answers are drawn and the drawing has stopped.
    if (mem.u8(CHOOSING) or 0) == 1 then
        if quiet > 2 then
            emit_paragraph()
            emit_choice()
        end
        if choice_key then reader.cursor(pointed()) end
    elseif choice_key then
        reader.layout("choice", nil)
        choice_key = nil
        choice_rows, pointer_y = {}, nil
        reader.cursor(nil)
    end
end)

reader.on_state_loaded(function()
    clear_page()
    last_waiting = mem.u8(WAITING) or 0
    in_story = mem.u32(PROGRAM) == STORY
end)
