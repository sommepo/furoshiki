-- reader: no hooks
-- reader: title 英雄伝説 空の軌跡FC (The Legend of Heroes: Trails in the Sky, PSP)
-- reader: reads The talk windows of the story and of people in the field, with speakers and furigana, and choices
-- reader: version ULJM-05170, disc version 1.01
-- reader: audio never
-- 英雄伝説 空の軌跡ＦＣ (Nihon Falcom, PSP, 2006), ULJM-05170 v1.01. EBOOT.BIN 6C535B6A.
--
-- The game keeps each talk (the text of one window, all its pages) as plain Shift-JIS in a
-- buffer of its own, with Falcom's codes still in it: #nnnF a face, #nP where the window
-- stands, #nS the size of the letters, #nW the typing speed, 漢字#nRかな# a reading over
-- the n half-width cells before it; the bytes 0x01 (a new row), 0x02 (the wait for the
-- button) and 0x03 (the page is cleared). A window object (class CMessageWindow) points
-- into that buffer: where the page it shows began and how far it has been typed. While a
-- page waits for the button, the byte before "how far" is that 0x02. Nothing is hooked: the
-- windows are found once a frame through the game's list of them.
--
-- The window object (vtable VTABLE, in the executable; the objects live in the heap and
-- change places between scenes):
--   +0x38 the window's drawing record (WINDOW_RECORD below holds its place on screen),
--   +0x78 how far the text has been typed (one past the 0x02 while the page waits),
--   +0x7c where the page began (at its 0x03, or at the talk's first byte),
--   +0x80 the talk's buffer: its text begins at +0x40. Before it: +0x10 the window's #nP,
--         +0x14 the speaker's character object (its name, Shift-JIS, at +0xe0), +0x28 flags
--         (0x8000 while the window closes, 0x40 the face on the right, 0x20000 the speaker's
--         name given by the script, which is then at +0x440: "男性の声" for a voice off screen).
local BUILD = "6C535B6A"
if reader.build_id ~= BUILD then
    error("this profile is for Sora no Kiseki FC, ULJM-05170 (EBOOT " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end
local base = reader.base - 0x08804000 -- the executable is not relocatable: this is zero

local VTABLE = base + 0x08a111e0
-- The game's list of open talk windows: pointers, a few slots (two are used, one window
-- opening while the last closes).
local WINDOWS = base + 0x08b87820
local WINDOW_SLOTS = 12
local MW_RECORD, MW_TYPED, MW_PAGE, MW_BUFFER = 0x38, 0x78, 0x7c, 0x80
local BUFFER_TEXT, BUFFER_SPEAKER, BUFFER_FLAGS = 0x40, 0x14, 0x28
local SPEAKER_NAME, BUFFER_NAME = 0xe0, 0x440
local FLAG_CLOSING, FLAG_FACE_RIGHT, FLAG_NAMED = 0x8000, 0x40, 0x20000
-- In the drawing record: a byte, 4 while the window is up (0x0c while it closes), then the
-- point the window is placed from, x and y in pixels of the 480x272 screen (two bytes each).
local WINDOW_RECORD = 0x28c8
local RECORD_CLOSING = 0x0c

-- Where the first row's first character stands, from that point (measured in the game's
-- drawing lists): 22 to the right without a face, 42 more with the face on the left, 42
-- less with it on the right; 37 down (the speaker's name is 24 above the first row).
local TEXT_X, FACE_SHIFT, TEXT_Y = 22, 42, 37
-- A character of size n (#nS, 2 at first) is 6 + 4n pixels wide and high (14 at first, 18
-- at #3S); rows are 8 pixels apart. Half-width characters take half of that.
local DEFAULT_SIZE, ROW_GAP = 2, 8
local function size_px(n) return 6 + 4 * n end

-- A page is read once it has waited for the button for this many frames.
local SHOWN_FRAMES = 3

-- The game's own symbols in Shift-JIS's NEC rows, as it draws them.
local SYMBOLS = { ["㈱"] = "♥" }

local function is_lead(c)
    return (c >= 0x81 and c <= 0x9f) or (c >= 0xe0 and c <= 0xfc)
end

-- A talk's text as tokens: { char, width (cells: 2 full, 1 half), size } for a character,
-- { row = true } for 0x01, { page = true } for 0x03, { ruby = reading, cells = n } for
-- 漢字#nRかな#. The size (#nS) is taken to carry on to the end of the talk (whether the game
-- sets it back at a new page was not seen).
local function tokens(raw)
    local out, i, size = {}, 1, DEFAULT_SIZE
    while i <= #raw do
        local c = raw:byte(i)
        if c == 0x23 then -- #
            local digits, code = raw:match("^#(%d*)(%a)", i)
            if not code then
                i = i + 1
            elseif code == "R" then
                local reading, after = raw:match("^#%d*R(.-)#()", i)
                if reading then
                    out[#out + 1] = { ruby = text.sjis(reading), cells = tonumber(digits) or 0 }
                    i = after
                else
                    i = i + 1 + #digits + 1
                end
            else
                if code == "S" then size = tonumber(digits) or DEFAULT_SIZE end
                i = i + 1 + #digits + 1
            end
        elseif c == 0x01 then
            out[#out + 1] = { row = true }
            i = i + 1
        elseif c == 0x03 then
            out[#out + 1] = { page = true }
            i = i + 1
        elseif c == 0x07 then -- a colour: one byte follows (Falcom's format; not met here)
            i = i + 2
        elseif c == 0x1f then -- an item's name: two bytes follow (not met here, not read)
            i = i + 3
        elseif c < 0x20 then
            i = i + 1
        elseif is_lead(c) and i < #raw then
            local ch = text.sjis(raw:sub(i, i + 1))
            out[#out + 1] = { char = SYMBOLS[ch] or ch, width = 2, size = size }
            i = i + 2
        else
            out[#out + 1] = { char = text.sjis(raw:sub(i, i)), width = 1, size = size }
            i = i + 1
        end
    end
    return out
end

-- The last page of a talk's tokens (what follows its last 0x03), laid out from the first
-- row's first character: its text, readings and a cell for every character.
local function compose(list, origin_x, origin_y)
    -- The page begins after the last page break.
    local first = 1
    for n = #list, 1, -1 do
        if list[n].page then first = n + 1; break end
    end
    local b, cells, placed = reader.builder(), {}, {}
    local x, y, row_px = origin_x, origin_y, 0
    local function new_row()
        y = y + (row_px > 0 and row_px or size_px(DEFAULT_SIZE)) + ROW_GAP
        x, row_px = origin_x, 0
    end
    local pending_break = false
    for n = first, #list do
        local t = list[n]
        if t.row then
            -- A row break the writer made: it stays in the text.
            if #placed > 0 then pending_break = true end
            new_row()
        elseif t.ruby then
            -- The reading belongs to the characters before it that fill t.cells half cells.
            local cells_left, count = t.cells, 0
            for k = #placed, 1, -1 do
                if cells_left <= 0 then break end
                cells_left = cells_left - placed[k].width
                count = count + 1
            end
            if count > 0 then
                local spans = b:ruby()
                spans[#spans + 1] = { b:length() - count, count, t.ruby }
            end
        elseif t.char then
            if pending_break then
                b:add("\n")
                cells[#cells + 1] = false
                pending_break = false
            end
            local px = size_px(t.size)
            local w = px * t.width / 2
            b:add(t.char)
            cells[#cells + 1] = { x, y, w, px }
            placed[#placed + 1] = t
            x = x + w
            if px > row_px then row_px = px end
        end
    end
    return b:text(), b:ruby(), cells
end

local shown = {}  -- by window object: { key, stood, window (layout name), layout }

local function withdraw(entry)
    if entry.layout then reader.layout(entry.window, nil) end
    entry.layout = nil
end

local function windows()
    local found = {}
    for slot = 0, WINDOW_SLOTS - 1 do
        local mw = mem.u32(WINDOWS + slot * 4)
        if mw and mw >= 0x08800000 and mw < 0x0a000000 and mem.u32(mw) == VTABLE then
            found[mw] = true
        end
    end
    return found
end

-- What a window shows: "waiting" with its page, "typing", or nil (closing, or not a talk).
local function read_window(mw)
    local typed, page, buffer = mem.u32(mw + MW_TYPED), mem.u32(mw + MW_PAGE), mem.u32(mw + MW_BUFFER)
    local record = mem.u32(mw + MW_RECORD)
    if not (typed and page and buffer and record) or buffer < 0x08800000 or
       typed <= page or page < buffer + BUFFER_TEXT or typed - page > 4096 then
        return nil
    end
    local flags = mem.u32(buffer + BUFFER_FLAGS) or 0
    if flags & FLAG_CLOSING ~= 0 or mem.u8(record + WINDOW_RECORD) == RECORD_CLOSING then
        return nil
    end
    if mem.u8(typed - 1) ~= 0x02 then return "typing" end
    return "waiting", typed, page, buffer, record, flags
end

reader.on_frame(function()
    local open = windows()
    for mw, entry in pairs(shown) do
        if not open[mw] then
            withdraw(entry)
            shown[mw] = nil
        end
    end
    for mw in pairs(open) do
        local entry = shown[mw]
        if not entry then
            entry = { stood = 0, window = string.format("talk%08x", mw) }
            shown[mw] = entry
        end
        local state, typed, page, buffer, record, flags = read_window(mw)
        if state ~= "waiting" then
            entry.stood = 0
            -- The page goes on (the next one is typed) or the window closes: its cells go.
            if entry.layout then withdraw(entry) end
            -- Typing begins every page: the same text typed again is read again.
            if state == "typing" then entry.key = nil end
        else
            local key = string.format("%x:%x:%x", buffer, page, typed)
            if key ~= entry.key then
                entry.stood = entry.stood + 1
                if entry.stood >= SHOWN_FRAMES then
                    entry.key, entry.stood = key, 0
                    withdraw(entry)
                    local raw = mem.read(buffer + BUFFER_TEXT, typed - buffer - BUFFER_TEXT) or ""
                    local list = tokens(raw)
                    local face = raw:find("#%d+F") ~= nil
                    local x = (mem.u16(record + WINDOW_RECORD + 2) or 0) + TEXT_X
                    if face then
                        x = x + (flags & FLAG_FACE_RIGHT ~= 0 and -FACE_SHIFT or FACE_SHIFT)
                    end
                    local y = (mem.u16(record + WINDOW_RECORD + 4) or 0) + TEXT_Y
                    local line, ruby, cells = compose(list, x, y)
                    if #cells > 0 then
                        local name
                        if flags & FLAG_NAMED ~= 0 then
                            name = mem.cstr(buffer + BUFFER_NAME, 64)
                        else
                            local who = mem.u32(buffer + BUFFER_SPEAKER)
                            if who and who >= 0x08800000 and who < 0x0a000000 then
                                name = mem.cstr(who + SPEAKER_NAME, 32)
                            end
                        end
                        local speaker = name and #name > 0 and text.sjis(name) or nil
                        reader.emit{ text = line, ruby = ruby, speaker = speaker, window = entry.window }
                        entry.layout = { space = { 480, 272 }, glyphs = cells }
                        reader.layout(entry.window, entry.layout)
                    end
                end
            end
        end
    end
end)

-- The choices (the script's menu: 休憩する／やめる, or a line to answer with): the game keeps
-- eight menu objects (class CED6Menu, VTABLE_MENU) side by side from MENUS; each points at its
-- drawing record (+0x38) as a talk window does. In it: from +0x8c the answers, Shift-JIS, 0x50
-- bytes apart; at +0x2898 the answer the cursor is on, from 0; at WINDOW_RECORD a byte that is
-- 6 or 7 while the menu is up (0 when it is not, with 0x08 set while it closes), then its x
-- and y and the number of answers (two bytes each).
local VTABLE_MENU = base + 0x08a07c48
local MENUS, MENU_SIZE, MENU_COUNT = base + 0x08c399b0, 0x40, 8
local MENU_ANSWERS, MENU_ANSWER, MENU_CURSOR = 0x8c, 0x50, 0x2898
local RECORD_GOING = 0x08
-- The first answer's first character stands 17 to the right of and 17 below the menu's
-- point; answers are 18 apart.
local ANSWER_X, ANSWER_Y, ANSWER_PITCH = 17, 17, 18

local choice -- { menu, key, layout } while one is reported
local choice_stood = 0

local function close_choice()
    if choice then
        reader.layout("choice", nil)
        if reader.cursor then reader.cursor(nil) end
        choice = nil
    end
end

local function open_menu()
    for k = 0, MENU_COUNT - 1 do
        local menu = MENUS + k * MENU_SIZE
        if mem.u32(menu) == VTABLE_MENU then
            local record = mem.u32(menu + MW_RECORD)
            if record and record >= 0x08800000 and record < 0x0a000000 then
                local state = mem.u8(record + WINDOW_RECORD) or 0
                local count = mem.u16(record + WINDOW_RECORD + 6) or 0
                if state ~= 0 and state & RECORD_GOING == 0 and count >= 1 and count <= 16 then
                    return menu, record, count
                end
            end
        end
    end
end

reader.on_frame(function()
    local menu, record, count = open_menu()
    if not menu then
        close_choice()
        choice_stood = 0
        return
    end
    local key = string.format("%x:%d", menu, count)
    if choice and choice.key == key then
        local at = mem.u32(record + MENU_CURSOR)
        if at and at < count and reader.cursor then reader.cursor(at) end
        return
    end
    close_choice()
    choice_stood = choice_stood + 1
    if choice_stood < SHOWN_FRAMES then return end
    choice_stood = 0
    local x0 = (mem.u16(record + WINDOW_RECORD + 2) or 0) + ANSWER_X
    local y0 = (mem.u16(record + WINDOW_RECORD + 4) or 0) + ANSWER_Y
    local b, cells = reader.builder(), {}
    for n = 0, count - 1 do
        local raw = mem.cstr(record + MENU_ANSWERS + n * MENU_ANSWER, MENU_ANSWER) or ""
        if n > 0 then
            b:add("\n")
            cells[#cells + 1] = false
        end
        local x = x0
        for _, t in ipairs(tokens(raw)) do
            if t.char then
                local px = size_px(t.size)
                local w = px * t.width / 2
                b:add(t.char)
                cells[#cells + 1] = { x, y0 + n * ANSWER_PITCH, w, px }
                x = x + w
            end
        end
    end
    if #cells == 0 then return end
    choice = { key = key, layout = { space = { 480, 272 }, glyphs = cells } }
    reader.emit{ text = b:text(), channel = "choice", window = "choice" }
    reader.layout("choice", choice.layout)
    local at = mem.u32(record + MENU_CURSOR)
    if at and at < count and reader.cursor then reader.cursor(at) end
end)

reader.on_state_loaded(function()
    for _, entry in pairs(shown) do withdraw(entry) end
    shown = {}
    close_choice()
    choice_stood = 0
end)
