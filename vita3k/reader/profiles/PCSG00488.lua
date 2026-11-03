-- reader: no hooks
-- reader: title 英雄伝説 空の軌跡 FC Evolution (The Legend of Heroes: Trails in the Sky, Vita)
-- reader: reads The talk windows of the story and of people in the field, with speakers and furigana
-- reader: version PCSG00488, 1.00
-- 英雄伝説 空の軌跡 FC Evolution (Chara-Ani / Nihon Falcom, PS Vita, 2015), PCSG00488 v1.00,
-- eboot.bin's module VS2010_Hachi_Vita_Shipping, NID 5EE81DAC.
--
-- The remake keeps the PSP game's talk windows (ppsspp/docs/reader/games/sora-no-kiseki-fc.md):
-- each talk in Shift-JIS with Falcom's codes in a buffer of its own: #nnnF a face, #nP where
-- the window stands, #nS the size of the letters, 漢字#nRかな# a reading, and the bytes 0x01 (a
-- new row), 0x02 (the wait for the button), 0x03 (the page is cleared). New here: every page
-- begins with codes for the voice and its timing (#6907J, #0010000003V, #2B, #102Z), which are
-- passed over as any code is. Nothing is hooked: the window is found once a frame.
--
-- The window object (vtable VTABLE, in the heap; a talk may move it from one slot to another):
--   +0x038 its drawing record (+0xD6 the window's x, +0xD8 its y, signed, both in the PSP's
--          480x272 units, which this game doubles for the 960x544 screen),
--   +0x880 how far the text has been typed (one past the 0x02 while the page waits),
--   +0x884 where the page began,
--   +0x87C where the talk's text begins in the scenario. Just before it stand two bytes of who
--          speaks, and between them and the text, ending with a zero, a name of the scenario's
--          own where it gives one ("男性の声", for a voice the player is not to know yet),
--   +0x8C0 the talk: +0x0C the speaker's character object (its name, Shift-JIS, at +0xE0),
--          +0x14 flags (0x40: the face stands on the right), its text from +0x234.
-- The slots are a list in the game object, whose address the data segment holds at GAME.
-- The big portraits have a window of the same kind and an empty talk: their text is in a buffer of
-- its own, and the name in the box stands RECORD_NAME from the drawing record.
local BUILD = "5EE81DAC"
if reader.build_id ~= BUILD then
    error("this profile is for Sora no Kiseki FC Evolution, PCSG00488 1.00 (" .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end
-- The data segment: beside the program in Vita3K, wherever the system put it on a real Vita.
local data = reader.module("data")
local GAME = data and data.base + 0xCEC0 or reader.base + 0x199EC0
local VTABLE = reader.base + 0x18ACA4
local SLOTS, SLOT_COUNT = 0x3D14, 12
local W_RECORD, W_SOURCE, W_TYPED, W_PAGE, W_TALK = 0x38, 0x87C, 0x880, 0x884, 0x8C0
local TALK_SPEAKER, TALK_FLAGS, TALK_TEXT, SPEAKER_NAME = 0x0C, 0x14, 0x234, 0xE0
local FLAG_FACE_RIGHT = 0x40
local RECORD_X, RECORD_Y, RECORD_NAME = 0xD6, 0xD8, -0x84

-- Where the first row's first character stands, from the window's point (measured on the
-- screen, 960x544): the face takes the left of the window, or of its right.
local TEXT_X_FACE_LEFT, TEXT_X_FACE_RIGHT, TEXT_X_NO_FACE = 118, -50, 34
local TEXT_Y, TEXT_Y_NOTICE = 89, 58
-- A character of size n (#nS, 2 at first), rows 40 pixels apart at size 2.
local DEFAULT_SIZE, CELL_2, ROW_2 = 2, 27, 40
local function size_px(n) return (6 + 4 * n) * CELL_2 / 14 end

-- A page is read once it has waited for the button for this many frames.
local SHOWN_FRAMES = 3

-- Vita addresses a game's own data can be at.
local function usable(address)
    return address ~= nil and address >= 0x81000000 and address < 0xA0000000
end

local function is_lead(c)
    return (c >= 0x81 and c <= 0x9f) or (c >= 0xe0 and c <= 0xfc)
end

-- A page's text as tokens, as the PSP game's profile has them: { char, width (cells: 2 full,
-- 1 half), size }, { row = true } for 0x01, { ruby = reading, cells = n } for 漢字#nRかな#.
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
        elseif c == 0x07 then -- a colour: one byte follows (Falcom's format)
            i = i + 2
        elseif c == 0x1f then -- an item's name: two bytes follow (not read, as on the PSP)
            i = i + 3
        elseif c < 0x20 then
            i = i + 1
        elseif is_lead(c) and i < #raw then
            out[#out + 1] = { char = text.sjis(raw:sub(i, i + 1)), width = 2, size = size }
            i = i + 2
        else
            out[#out + 1] = { char = text.sjis(raw:sub(i, i)), width = 1, size = size }
            i = i + 1
        end
    end
    return out
end

-- Spaces at a row's end pad the voice's timing: they are nothing to read.
local function trimmed(list)
    for i = #list, 1, -1 do
        if list[i].char == "　" and (not list[i + 1] or list[i + 1].row) then table.remove(list, i) end
    end
    return list
end

-- The page's text, readings and a cell for every character, from the first row's first one.
local function compose(list, origin_x, origin_y)
    local b, cells, placed = reader.builder(), {}, {}
    local x, y, row_px = origin_x, origin_y, 0
    local pending_break = false
    for _, t in ipairs(list) do
        if t.row then
            if #placed > 0 then pending_break = true end
            y = y + (row_px > 0 and row_px or CELL_2) * ROW_2 / CELL_2
            x, row_px = origin_x, 0
        elseif t.ruby then
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

-- The window that is showing a talk, or nil.
local function open_window()
    local game = mem.u32(GAME)
    if not usable(game) then return nil end
    for slot = 0, SLOT_COUNT - 1 do
        local w = mem.u32(game + SLOTS + slot * 4)
        if usable(w) and mem.u32(w) == VTABLE then
            local typed, page = mem.u32(w + W_TYPED), mem.u32(w + W_PAGE)
            if usable(typed) and usable(page) and typed > page and typed - page <= 4096 then
                return w, typed, page
            end
        end
    end
end

local shown_key, stood = nil, 0
local function withdraw()
    reader.layout("talk", nil)
end

reader.on_frame(function()
    local w, typed, page = open_window()
    if not w then
        if shown_key then withdraw() end
        shown_key, stood = nil, 0
        return
    end
    if mem.u8(typed - 1) ~= 0x02 then
        -- The next page is being typed: the same text typed again is read again.
        if shown_key then withdraw() end
        shown_key, stood = nil, 0
        return
    end
    local key = string.format("%x:%x", page, typed)
    if key == shown_key then return end
    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0
    shown_key = key

    local raw = mem.read(page, typed - page) or ""
    local record = mem.u32(w + W_RECORD)
    local x, y = 0, 0
    if usable(record) then
        x = mem.u16(record + RECORD_X) or 0
        y = mem.u16(record + RECORD_Y) or 0
        if y >= 0x8000 then y = y - 0x10000 end -- a window above the screen's top
    end
    local talk = w + W_TALK
    -- A face is set once in a talk, on its first page. A big portrait is no face in the window.
    local in_talk = typed - talk - TALK_TEXT
    local portrait = in_talk <= 0 or in_talk > 4096
    local so_far = not portrait and mem.read(talk + TALK_TEXT, in_talk) or ""
    local offset = TEXT_X_NO_FACE
    if so_far:find("#%d+F") then
        local flags = mem.u32(talk + TALK_FLAGS) or 0
        offset = flags & FLAG_FACE_RIGHT ~= 0 and TEXT_X_FACE_RIGHT or TEXT_X_FACE_LEFT
    end
    -- The scenario's own name for this talk, if it gives one, before the character's.
    local source = mem.u32(w + W_SOURCE)
    local before = usable(source) and mem.read(source - 48, 48) or ""
    local name = before:match("%z%z..([^%z]+)%z$")
    if not name and portrait then
        name = usable(record) and mem.cstr(record + RECORD_NAME, 32) or nil
    elseif not name then
        local who = mem.u32(talk + TALK_SPEAKER)
        name = usable(who) and mem.cstr(who + SPEAKER_NAME, 32) or nil
    end
    local speaker = name and #name > 0 and text.sjis(name) or nil
    -- A notice ("500 Mira received") is a window of this kind with no name above its text.
    local top = portrait and not speaker and TEXT_Y_NOTICE or TEXT_Y
    local line, ruby, cells = compose(trimmed(tokens(raw)), x * 2 + offset, y * 2 + top)
    if #cells == 0 then return end
    reader.emit{ text = line, ruby = ruby, speaker = speaker, window = "talk" }
    reader.layout("talk", { space = { 960, 544 }, glyphs = cells })
end)
