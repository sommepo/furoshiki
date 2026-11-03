-- reader: no hooks
-- reader: api 2
-- reader: title CLANNAD
-- reader: reads The story's text with speakers, each message as it comes up, and choices; finished routes can be marked
-- reader: version SLPM-66302, disc version 1.01
-- reader: touch press
-- reader: buttons start square up down circle cross
-- Touch play's buttons (found in the harness, 2026-10-16): START at the title (○ does nothing
-- there), up and down for the title's and the menu's lists, ○ to choose (a tap on the picture
-- is ○ too, but a button is sure), × back out; □ the menu (save, load,
-- config), △ nothing, SELECT the backlog, START auto.
--
-- CLANNAD (Interchannel, PS2, 2005), SLPM-66302. Program 7800DC84. HuneX's engine.
--
-- The script is Shift-JIS text commands; a message's text goes into one buffer as the script
-- has it, which the game draws as it types it out: a speaker as 【name】 in front (drawn as the
-- name, without the brackets), `＊Ａ` and the like for the names the player gives (a table of
-- eight bytes a name), `^` for a line break. When a message goes on after a wait the buffer
-- grows. Characters are drawn at a fixed pitch, 23 to a row (measured in pictures).
-- A choice: its answers are buffers of 100 bytes, how many there are a number beside them, and a
-- bit of the script's state says that one is waiting. The answers stand in the message window's
-- rows. (A choice of the other kind, SEB, has digits for answers that pick texts from a table
-- through story variables, as the game's code at 0x14B344 does.)
--
-- Routes: the game keeps what was finished in its global variables G (16 bits each), which it
-- writes to the memory card as its system data. `reader.progress` lets them be set by hand,
-- for routes read elsewhere; the game keeps them once its system data is saved.

local BUILD = "7800DC84"
if reader.build_id ~= BUILD then
    error("this profile is for CLANNAD SLPM-66302 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

-- Addresses (the program loads at its link address).
local MESSAGE = 0x00376f38      -- the message, Shift-JIS, as the script has it
local MESSAGE_SIZE = 440
local NAMES = 0x00376cf8        -- ＊Ａ, ＊Ｂ...: eight bytes each
local WINDOW_ON = 0x001cfe14    -- 1 while the message window is shown
local SCRIPT_STATE = 0x003775b8 -- bit 0x10 while a choice waits
local ANSWER_COUNT = 0x003775b0 -- how many answers the choice has (16 bits)
local ANSWERS = 0x00377290      -- the answers, 100 bytes each
local ANSWER_SIZE = 100
local ANSWER_MAX = 8
local ANSWER_CURSOR = 0x0060cdda -- the answer the cursor is on, from 0; 0xffff until the d-pad
                                -- first puts it on one (○ picks nothing then). 16 bits, in an
                                -- object of the choice's window: the same place in two runs
                                -- of the game (a new game, a save state of another day)
local SEB_TEXTS = 0x003778b8    -- pointers to the texts a SEB answer picks
local F = 0x00371ab8            -- the story's variables F (16 bits each)
local G = 0x00372f08            -- the global variables G (16 bits each), the routes

-- Where the text stands in the picture (640 by 448).
local LEFT, TOP = 34, 334
local PITCH, ROW_PITCH, HEIGHT = 23.6, 28.2, 26
local PER_ROW = 23
-- These do not begin a row: they hang at the end of the row before (not measured).
local HANG = {}
for _, c in ipairs{ "、", "。", "」", "』", "）", "！", "？", "…", "ー", "っ", "ゃ", "ゅ", "ょ" } do
    HANG[c] = true
end

-- Frames a message must stay the same before it is reported.
local SHOWN_FRAMES = 4

local function is_lead(b)
    return (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)
end

local FULL_A = 0x8260 -- Ａ

-- A name the player gives (＊Ａ is the family name, ＊Ｂ the given name).
local function name(index)
    local raw = mem.read(NAMES + index * 8, 8)
    return raw and raw:match("^[^%z]*") or ""
end

-- The characters of a text as the game draws them: names put in, `^` a line break ("\n").
local function characters(raw)
    local out, at = {}, 1
    while at <= #raw do
        local b = raw:byte(at)
        if is_lead(b) and at < #raw then
            local code = b * 256 + raw:byte(at + 1)
            local nxt = at + 2 < #raw and raw:byte(at + 2) * 256 + raw:byte(at + 3) or 0
            if code == 0x8196 and nxt >= FULL_A and nxt < FULL_A + 26 then -- ＊ and a letter
                for _, c in ipairs(characters(name(nxt - FULL_A))) do out[#out + 1] = c end
                at = at + 4
            else
                out[#out + 1] = text.sjis(raw:sub(at, at + 1))
                at = at + 2
            end
        elseif b == 0x5e then
            out[#out + 1] = "\n"
            at = at + 1
        else
            out[#out + 1] = string.char(b)
            at = at + 1
        end
    end
    return out
end

-- Brackets that open what is said: the rows after the first begin under the character after
-- the bracket (seen with 朋也「, 2026-10-23: the second row three characters in).
local OPENS = { ["「"] = true, ["『"] = true, ["（"] = true }

-- Cells for drawn characters, from row `row` and column `col` on, as the game breaks rows;
-- later rows begin at column `indent` (0 when not given).
-- Returns the cells (false for a line break) and where the next character would go.
local function place(chars, row, col, indent)
    indent = indent or 0
    local cells = {}
    for _, c in ipairs(chars) do
        if c == "\n" then
            cells[#cells + 1] = false
            row, col = row + 1, indent
        else
            if col >= PER_ROW and not HANG[c] then row, col = row + 1, indent end
            cells[#cells + 1] = { LEFT + col * PITCH, TOP + row * ROW_PITCH, PITCH, HEIGHT }
            col = col + 1
        end
    end
    return cells, row, col
end

-- A message: its speaker, the characters of what is said and the characters drawn before them.
local function parse(raw)
    local speaker, body = raw:match("^\x81\x79(.-)\x81\x7a(.*)$") -- 【...】
    if not speaker then return nil, characters(raw), {} end
    local shown = characters(speaker)
    return table.concat(shown), characters(body), shown
end

local reported = nil     -- the raw message reported last
local pending, since     -- the raw message seen, and since which frame
local windows = {}
local answers_shown
local answers_up = 0 -- how many answers the choice waiting has, 0 while none waits

-- Touch play: which answer the cursor is on while a choice waits, -1 before the d-pad has put
-- it on one; nothing once it has gone. An older app has no reader.cursor.
local cursor_said
local function report_cursor()
    local at
    if answers_up > 0 then
        at = mem.u16(ANSWER_CURSOR) or 0xffff
        if at >= answers_up then at = -1 end
    end
    if at == cursor_said then return end
    cursor_said = at
    if reader.cursor then reader.cursor(at) end
end

local function withdraw()
    for window in pairs(windows) do reader.layout(window, nil) end
    windows = {}
end

local function choosing()
    return ((mem.u32(SCRIPT_STATE) or 0) & 0x10) ~= 0
end

-- The text of a SEB answer: a digit picks a row of a table through a story variable.
local SEB = { [1] = { 100, 3 }, [2] = { 101, 6 }, [3] = { 103, 9 }, [4] = { 104, 12 } }
local function seb(digit)
    local pick = SEB[digit]
    if not pick then return nil end
    local index = (mem.u16(F + pick[1] * 2) or 0) + pick[2]
    local pointer = mem.u32(SEB_TEXTS + index * 4)
    if not pointer or pointer == 0 then return nil end
    local raw = mem.read(pointer, 100)
    return raw and raw:match("^[^%z]*")
end

local function choice()
    local rows = {}
    if choosing() then
        local count = mem.u16(ANSWER_COUNT) or 0
        if count > ANSWER_MAX then count = 0 end
        for index = 1, count do
            local raw = mem.read(ANSWERS + (index - 1) * ANSWER_SIZE, ANSWER_SIZE)
            raw = raw and raw:match("^[^%z]*") or ""
            local digit = raw:match("^(%d)$")
            if digit then raw = seb(tonumber(digit)) or raw end
            rows[#rows + 1] = raw
        end
    end
    local key = table.concat(rows, "\n")
    answers_up = key ~= "" and #rows or 0
    if #rows < 1 or key == "" then
        if answers_shown then
            reader.layout("choice", nil)
            answers_shown = nil
        end
        return
    end
    if key == answers_shown then return end
    answers_shown = key
    local b, cells = reader.builder(), {}
    for index, raw in ipairs(rows) do
        if index > 1 then
            b:add("\n")
            cells[#cells + 1] = false
        end
        local chars = characters(raw)
        local placed = place(chars, index - 1, 0)
        for at, c in ipairs(chars) do
            b:add(c)
            cells[#cells + 1] = placed[at]
        end
    end
    -- The window shows the answers in place of the message.
    withdraw()
    reader.emit{ text = b:text(), channel = "choice", window = "choice", id = "choice:" .. text.hash(key) }
    reader.layout("choice", { space = { 640, 448 }, glyphs = cells })
end

reader.on_state_loaded(function()
    reported, pending, since, answers_shown = nil, nil, nil, nil
    windows = {}
end)

reader.on_frame(function(frame)
    choice()
    report_cursor()
    if answers_shown then return end
    if (mem.u32(WINDOW_ON) or 0) == 0 then
        withdraw()
        return
    end
    local raw = mem.read(MESSAGE, MESSAGE_SIZE)
    raw = raw and raw:match("^[^%z]*") or ""
    if raw == "" or raw == reported then
        pending = nil
        return
    end
    if raw ~= pending then
        pending, since = raw, frame
        return
    end
    if frame - since < SHOWN_FRAMES then return end
    pending = nil

    -- The message went on after a wait: only what is new.
    local grown = reported and #raw > #reported and raw:sub(1, #reported) == reported
    local speaker, said, before = parse(raw)
    local skip = 0
    if grown then
        local _, old, _ = parse(reported)
        skip = #old
    else
        withdraw()
    end
    reported = raw
    -- Where the drawn text puts each character.
    local all = {}
    for _, c in ipairs(before) do all[#all + 1] = c end
    for _, c in ipairs(said) do all[#all + 1] = c end
    local indent = OPENS[said[1]] and #before + 1 or 0
    local placed = place(all, 0, 0, indent)
    local b, cells = reader.builder(), {}
    for at = skip + 1, #said do
        local c = said[at]
        b:add(c)
        cells[#cells + 1] = placed[#before + at]
    end
    local whole = b:text()
    if whole:match("^%s*$") then return end
    local window = "message" .. (grown and ("+" .. skip) or "")
    reader.emit{ text = whole, speaker = speaker, window = window,
                 id = text.hash(raw) .. ":" .. skip }
    reader.layout(window, { space = { 640, 448 }, glyphs = cells })
    windows[window] = true
end)

-- ---- Routes finished elsewhere ----
-- The global cells each ending sets, from the script's own assignments (VN Library's CLANNAD
-- notes have where each is set). G0 counts the lights of the cells marked so; G73 is the
-- title screen's stage, which the game raises at its title from the cells (0x144860): 2 opens
-- AFTER STORY (every School Life route but Ryou's, and both extra lights), 3 the true ending's.

local ROUTES = {
    { id = "nagisa", group = "School Life", label = "古河 渚 (Nagisa)", cells = { 1 } },
    { id = "fuko", group = "School Life", label = "伊吹 風子 (Fuko)", cells = { 2 } },
    { id = "tomoyo", group = "School Life", label = "坂上 智代 (Tomoyo)", cells = { 3 } },
    { id = "misae-light", group = "School Life", label = "美佐枝さんの光 (Misae's light, in Tomoyo's route)", cells = { 31 } },
    { id = "kyou", group = "School Life", label = "藤林 杏 (Kyou)", cells = { 4 } },
    { id = "kotomi", group = "School Life", label = "一ノ瀬 ことみ (Kotomi)", cells = { 5 } },
    { id = "yukine", group = "School Life", label = "宮沢 有紀寧 (Yukine)", cells = { 6, 30 } },
    { id = "sunohara", group = "School Life", label = "春原兄妹 (Sunohara and Mei)", cells = { 9 } },
    { id = "kappei", group = "School Life", label = "柊 勝平 (Kappei)", cells = { 12 } },
    { id = "misae", group = "School Life", label = "相楽 美佐枝 (Misae)", cells = { 13 } },
    { id = "koumura", group = "School Life", label = "幸村 俊夫 (Koumura)", cells = { 15 } },
    { id = "akio", group = "After Story", label = "秋生の光 (Akio's light)", cells = { 7 } },
    { id = "sanae", group = "After Story", label = "早苗の光 (Sanae's light)", cells = { 8 } },
    { id = "kouko", group = "After Story", label = "公子の光 (Kouko's light)", cells = { 10 } },
    { id = "yoshino", group = "After Story", label = "芳野の光 (Yoshino's light)", cells = { 11 } },
    { id = "naoyuki", group = "After Story", label = "直幸の光 (Naoyuki's light)", cells = { 14 } },
    { id = "fuko-light", group = "After Story", label = "風子の光 (Fuko's light given back)", cells = { 33 } },
    { id = "after-story", group = "After Story", label = "AFTER STORY の終わり (its first ending)", cells = { 35 } },
    { id = "true-end", group = "After Story", label = "AFTER STORY 真のエンディング (true ending)", cells = { 45 } },
}
-- Cells that are a light when set (counted in G0).
local LIGHT = {}
for _, cell in ipairs{ 3, 4, 5, 7, 8, 9, 10, 11, 12, 14, 15, 30, 31, 33 } do LIGHT[cell] = true end
local STAGE_CELLS = { 1, 2, 3, 4, 5, 6, 9, 12, 13, 15 }

local function g(cell) return mem.u16(G + cell * 2) or 0 end
local function set_g(cell, value) mem.w16(G + cell * 2, value & 0xffff) end
local function find(id)
    for _, route in ipairs(ROUTES) do
        if route.id == id then return route end
    end
end

-- The title's stage from the cells, as the game works it out; raised only, unless a mark was
-- taken back.
local function title_stage(lowered)
    local any, all = false, true
    for _, cell in ipairs(STAGE_CELLS) do
        if g(cell) == 1 then any = true else all = false end
    end
    local stage = 0
    if any then stage = 1 end
    if all and g(30) == 1 and g(31) == 1 then stage = 2 end
    if g(45) == 1 then stage = 3 end
    local now = g(73)
    if not lowered and now > stage and (now < 3 or g(45) == 1) then stage = now end
    set_g(73, stage)
end

reader.progress{
    title = "CLANNAD: routes",
    note = "The game keeps these with its system data: save the system data from the game's " ..
           "menu (or when it asks, after an ending) to keep them. AFTER STORY shows on the " ..
           "title screen once all School Life routes but Ryou's and both extra lights are " ..
           "marked, the next time the game goes to its title. Ryou's route leaves no mark of " ..
           "its own in the game and is not needed.",
    items = ROUTES,
    done = function(id)
        local route = find(id)
        if not route then return false end
        for _, cell in ipairs(route.cells) do
            if g(cell) ~= 1 then return false end
        end
        return true
    end,
    mark = function(id, on)
        local route = find(id)
        if not route then error("no such route: " .. tostring(id)) end
        if on then
            -- Fuko's light given back repays the one used earlier (G32): marked alone, it
            -- would make a light out of nothing.
            if id == "fuko-light" and g(33) ~= 1 and g(32) ~= 1 then
                set_g(32, 1)
                set_g(0, g(0) - 1)
            end
            for _, cell in ipairs(route.cells) do
                if g(cell) ~= 1 then
                    if LIGHT[cell] then set_g(0, g(0) + 1) end
                    set_g(cell, 1)
                end
            end
        else
            for _, cell in ipairs(route.cells) do
                if g(cell) == 1 then
                    if LIGHT[cell] then set_g(0, math.max(g(0) - 1, 0)) end
                    set_g(cell, 0)
                end
            end
        end
        title_stage(not on)
    end,
}
