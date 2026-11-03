-- reader: title ダブルキャスト
-- reader: reads The lines under the film (with 台詞設定 at 文字+音声 or 文字のみ), and the choices
-- reader: version SCPS-10053 (boot program AC46E621)
-- reader: touch press
-- reader: buttons up down circle
--
-- Double Cast (Yarudora Series Vol. 1; Sony / Production I.G, 1998), the Japanese discs: disc 1
-- SCPS-10053, disc 2 SCPS-10054, one boot program (SCPS-10054.lua is this file but for its
-- version line). The game's script keeps its text in Shift-JIS with its hiragana as single
-- bytes, and draws it into a texture of each of its windows, a glyph a frame, through one
-- routine that takes the glyph's Shift-JIS code (hiragana already made full-width) and its
-- place in that texture; the window's sprites show the texture at the window's place on the
-- screen. The profile hooks that routine (the window is the caller's s3 there) and the two
-- that clear a window's texture, and reports a window's text once no glyph has come for a few
-- frames: a line the game stops in the middle of (it waits for ○) comes in two pieces. Speakers
-- the picture does not show stand on a line of their own (電話の声), and are read with the line.
--
-- The lines under the film are only shown when the game's own setting asks for them (各種設定,
-- 表示, 台詞設定: 文字+音声 or 文字のみ; it starts at 音声のみ, voices alone, with no text; the
-- setting is kept on the memory card). A choice is a window of its own, its answers in an order
-- the game shuffles, each a line of its texture shown as a bar of its own (or a line of a small
-- window, as はい / いいえ); the window keeps the answer its cursor is on (reader.cursor), so that
-- a tap on an answer picks it. Up and down move that cursor, ○ takes it (the buttons line
-- above is a note of that). Text that was on screen when a savestate is loaded is not read
-- (it is in the picture only); the next line is. See docs/reader/games/double-cast.md.

local BUILD = "AC46E621"
if reader.build_id ~= BUILD then
    error("this profile is for the boot program " .. BUILD .. ", the disc has " .. tostring(reader.build_id))
end

local DRAW_GLYPH = 0x80029884 -- DrawGlyph(sjis a0, ..., x a3, y [sp+16]); the window is s3
local CLEAR = 0x800291B8      -- ClearWindowText(window a0): the whole texture
local CLEAR_LINES = 0x800290D8 -- ClearWindowLines(window a0): before a new line, and when a line goes
local WINDOWS = 0x8008B280    -- the windows: 0x1EC bytes each
local SIZE, COUNT = 0x1EC, 16
local STYLES = 0x80080FA0     -- the windows' styles: 30 bytes each; +2 == 8: a row a bar
-- Fields of a window.
local W_STATE = 0             -- s8: 1 while it is shown
local W_X, W_Y = 8, 10        -- s16: its place on the screen
local W_STYLE = 16            -- s16: its style, -1 none
local W_ROW_STEP = 22         -- s16: how far apart its bars are on the screen
local W_CURSOR = 24           -- s16: the answer the cursor is on, -1 none
local W_TEXT_W = 274          -- s16: its texture's width
local W_ROW_H = 280           -- s16: a line's height in the texture
local W_TEXT_X, W_TEXT_Y = 300, 302 -- s16: where its text begins, from its place
local CELL_W, CELL_H = 12, 14 -- a glyph's cell
local QUIET = 6               -- frames without a new glyph: the text is complete

local function s16(v)
    if not v then return 0 end
    return v >= 0x8000 and v - 0x10000 or v
end

local function decode(code)
    if code < 0x100 then return text.sjis(string.char(code)) end
    return text.sjis(string.char(code >> 8, code & 0xFF))
end

-- The screen is 240 lines high to the game; the picture shown may be cut to fewer (224).
local function top_cut()
    local _, h = reader.picture()
    if h and h < 240 then return (240 - h) // 2 end
    return 0
end

local pages = {}     -- window address -> { glyphs, said (how many reported), quiet (frames), windows }

local function name_of(w) return string.format("w%X", w) end

local function withdraw(w)
    local page = pages[w]
    if not page then return end
    for _, name in ipairs(page.windows) do reader.layout(name, nil) end
    if page.choice then reader.cursor(nil) end
    pages[w] = nil
end

local function withdraw_all()
    for w in pairs(pages) do withdraw(w) end
    pages = {}
end

local function row_mode(w)
    local style = s16(mem.u16(w + W_STYLE))
    if style < 0 then return false end
    return mem.u16(STYLES + style * 30 + 2) == 8
end

-- Where a glyph of window w's texture is on the screen.
local function place(w, g, rows)
    local x0 = s16(mem.u16(w + W_X)) + s16(mem.u16(w + W_TEXT_X))
    local y0 = s16(mem.u16(w + W_Y)) + s16(mem.u16(w + W_TEXT_Y)) - top_cut()
    local y = g.y
    if rows then
        local h = s16(mem.u16(w + W_ROW_H))
        if h > 0 then y = (g.y // h) * s16(mem.u16(w + W_ROW_STEP)) + g.y % h end
    end
    return { x0 + g.x, y0 + y - 1, g.w or CELL_W, CELL_H }
end

local function row_height(w)
    local h = s16(mem.u16(w + W_ROW_H))
    return h > 0 and h or 16
end

-- The glyphs from..to as text and cells. A line that ends well short of the window's right
-- edge ends there (a new line in the text); one that runs to the edge (within the game's wrap
-- before punctuation) goes on. Each line of a choice is an answer.
local function piece(w, page, from, to, choice, rows)
    local chars, cells = {}, {}
    local list = page.glyphs
    local width = s16(mem.u16(w + W_TEXT_W))
    local h = row_height(w)
    local prev = nil      -- the last glyph kept
    local leading = true  -- still at a line's beginning (its spaces are no text)
    for i = from, to do
        local g = list[i]
        local before = list[i - 1]
        if not before or g.y ~= before.y then leading = true end
        local space = g.code == 0x8140 or g.code == 0x20
        if not (leading and space) then
            leading = false
            if prev and g.y > prev.y then
                if choice then
                    if g.y // h ~= prev.y // h then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
                elseif prev.x + (prev.w or CELL_W) <= width - 3 * CELL_W then
                    chars[#chars + 1] = "\n"; cells[#cells + 1] = false
                end
            end
            chars[#chars + 1] = decode(g.code)
            cells[#cells + 1] = place(w, g, rows)
            prev = g
        end
    end
    return table.concat(chars), cells
end

-- A choice: a window whose lines are shown as bars, or one with a cursor.
local function is_choice(w)
    return row_mode(w) or s16(mem.u16(w + W_CURSOR)) >= 0
end

local function emit(w, page)
    local list = page.glyphs
    if page.said >= #list then return end
    if is_choice(w) then
        -- All its answers at once, again whenever they change.
        table.sort(list, function(a, b) return a.y < b.y or (a.y == b.y and a.x < b.x) end)
        local body, cells = piece(w, page, 1, #list, true, row_mode(w))
        page.said = #list
        if body == "" then return end
        local h = row_height(w)
        page.answers = 0
        local seen = {}
        for _, g in ipairs(list) do
            if not seen[g.y // h] then seen[g.y // h] = true; page.answers = page.answers + 1 end
        end
        local name = name_of(w) .. "c"
        page.windows = { name }
        page.choice = true
        reader.emit{ text = body, window = name, channel = "choice", id = "choice:" .. text.hash(body) }
        reader.layout(name, { space = { reader.picture() }, glyphs = cells })
        return
    end
    local body, cells = piece(w, page, page.said + 1, #list, false, false)
    page.said = #list
    if body == "" then return end
    local name = name_of(w) .. "p" .. (#page.windows + 1)
    page.windows[#page.windows + 1] = name
    reader.emit{ text = body, window = name, id = text.hash(body) }
    reader.layout(name, { space = { reader.picture() }, glyphs = cells })
end

reader.hook(CLEAR, function(cpu)
    withdraw(cpu.a0)
end)

reader.hook(CLEAR_LINES, function(cpu)
    withdraw(cpu.a0)
end)

reader.hook(DRAW_GLYPH, function(cpu)
    local w = cpu.s3
    if w < WINDOWS or w >= WINDOWS + SIZE * COUNT or (w - WINDOWS) % SIZE ~= 0 then return end
    local code = cpu.a0 & 0xFFFF
    local x, y = s16(cpu.a3 & 0xFFFF), s16((mem.u32(cpu.sp + 16) or 0) & 0xFFFF)
    local page = pages[w]
    local last = page and page.glyphs[#page.glyphs]
    -- Back at or before the last glyph's place: the window was written anew (the game does not
    -- always clear it first).
    if last and (y < last.y or (y == last.y and x <= last.x)) then
        withdraw(w)
        page, last = nil, nil
    end
    if not page then
        page = { glyphs = {}, said = 0, quiet = 0, windows = {} }
        pages[w] = page
    end
    if last and last.y == y and x > last.x then last.w = math.min(x - last.x, CELL_W) end
    page.glyphs[#page.glyphs + 1] = { code = code, x = x, y = y }
    page.quiet = 0
end)

reader.on_frame(function()
    for w, page in pairs(pages) do
        if (mem.u8(w + W_STATE) or 0) ~= 1 then
            withdraw(w)
        else
            page.quiet = page.quiet + 1
            if page.quiet == QUIET then emit(w, page) end
            if page.choice then
                local at = s16(mem.u16(w + W_CURSOR))
                reader.cursor(at >= 0 and at < page.answers and at or -1)
            end
        end
    end
end)

reader.on_state_loaded(function()
    withdraw_all()
end)
