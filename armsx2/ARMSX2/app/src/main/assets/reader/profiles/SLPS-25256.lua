-- reader: no hooks
-- reader: title Never7 -the end of infinity-
-- reader: reads The story's text with speakers, each message as it has come up in full, and choices
-- reader: version SLPS-25256, disc version 1.01
-- reader: touch press
-- reader: buttons start select up down circle cross
-- Never7 -the end of infinity- (KID, PS2, 2003), SLPS-25256. Program 9D260CAF.
--
-- Touch play's buttons (found in the harness, 2026-10-16): START at the title (○ does nothing
-- there) and for the auto mode, SELECT the system menu (save, load, options), up and down for
-- the title's and that menu's lists, ○ to choose (a tap on the picture is ○ too, but a button
-- is sure), × back out (in the story it hides the window). △ (the
-- message history) and □ (skip) are left out: the reader has its own backlog.
--
-- The game keeps every row it has shown, as wrapped, in a list; a row is added once typed
-- out. When the list is full (999 rows) the count stays and the rows shift up one place per
-- new row. A speaker's name is a row of its own starting with a space.
-- A variable gives the window row of the last list row, so the rows before it are the ones
-- above. Characters are drawn at a fixed pitch, brackets and commas at half of it (measured).
-- A choice's answers are in their own buffers and drawn the same way, each row centred.

local BUILD = "9D260CAF"
if reader.build_id ~= BUILD then
    error("this profile is for Never7 SLPS-25256 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

-- Addresses (the program loads at its link address).
local ROWS = 0x00599b34        -- the rows shown so far, Shift-JIS, each in a buffer of its own
local ROW_SIZE = 100
local ROW_COUNT = 0x00398280   -- how many rows there are
local SETTLED = 0x00397f78     -- 1 when the message has been typed out, 0 while it is
local WINDOW_ROW = 0x00397f7c  -- the row of the text window the last row stands in (from 0)
local ANSWER_TEXT = 0x005cbe57 -- a choice's answers, each in a buffer of its own
local ANSWER_SIZE = 66
local ANSWER_MAX = 6
local ANSWER_COUNT = 0x00398630 -- how many answers the choice has (kept afterwards)
local CHOICE_BOX = 0x00397f4c  -- not 0 while a choice's box is up
local ANSWER_CURSOR = 0x0053d896 -- the answer the cursor is on, from 1; 0 until the d-pad is
                               -- first pressed (○ picks nothing then). 16 bits; kept afterwards
local MIDDLE_X, MIDDLE_Y = 320, 196 -- the answers stand around this point of the picture

-- Where the text stands in the picture (640 by 448): in the box at the bottom, or on the page
-- that fills the screen (some scenes' narration). The game keeps the rows' pitch, 28 in the box
-- and 29 on the page, which is what tells the two apart. The page's top was measured.
local LEFT = 24
local PITCH, HEIGHT = 24, 26
local ROW_PITCH_NOW = 0x003a08d0 -- the text rows' pitch, a 16-bit number
local BOX = { top = 319, pitch = 28, rows = 4 }
local PAGE = { top = 81, pitch = 29, rows = 12 }
local ROW_PITCH = BOX.pitch      -- the choice's answers stand in rows as the box's
local WIDTH = 576              -- a row is this wide at most
local HALF = {}                -- characters half as wide as the others
for _, c in ipairs{ "、", "。", "（", "）", "「", "」", "『", "』" } do HALF[c] = true end
-- A row ending in one of these ends a sentence; any other row continues in the next.
local ENDS = {}
for _, c in ipairs{ "。", "」", "』", "）", "！", "？", "…", "―", "♪", "～" } do ENDS[c] = true end

-- The list's rows are broken for the box. The page draws at most this many characters to a
-- row, whatever their width, and gives what is left to the start of the next row; a character
-- that may not begin a row takes the one before it along (seen: 「コーヒ」 drawn 「コー」 and
-- 「ヒー…」, 「行く』」 drawn 「行」 and 「く』と…」). Needs proper testing beyond one scene.
local PAGE_CHARACTERS = 24
local NO_START = {}
for _, c in ipairs{ "、", "。", "」", "』", "）", "！", "？", "…", "‥", "ー", "～", "ぁ", "ぃ", "ぅ", "ぇ",
                    "ぉ", "っ", "ゃ", "ゅ", "ょ", "ァ", "ィ", "ゥ", "ェ", "ォ", "ッ", "ャ", "ュ", "ョ" } do
    NO_START[c] = true
end

-- Frames a message must stay complete before it is reported; skipped text is not read.
local SHOWN_FRAMES = 4

local function is_lead(b)
    return (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)
end

-- A row's characters and how wide each is.
local function characters(raw)
    local out, at = {}, 1
    while at <= #raw do
        local b = raw:byte(at)
        if is_lead(b) and at < #raw then
            local c = text.sjis(raw:sub(at, at + 1))
            out[#out + 1] = { c, HALF[c] and PITCH / 2 or PITCH }
            at = at + 2
        else
            out[#out + 1] = { string.char(b), PITCH / 2 }
            at = at + 1
        end
    end
    return out
end

local function row(index)
    local raw = mem.read(ROWS + index * ROW_SIZE, ROW_SIZE)
    return raw and raw:match("^[^%z]*") or ""
end

local function blank(raw)
    return raw == "" or raw == " "
end

-- The characters the window draws in rows `first` to `last` of the list, and how many of each
-- row's characters came over from the row before: in the box the list's rows as they are, on
-- the page broken again (PAGE_CHARACTERS above) from the page's first row on.
local function drawn(first, last, page)
    local out, carried, carry = {}, {}, {}
    for index = math.max(first, 0), last do
        local chars = characters(row(index))
        if page then
            local all = carry
            for _, each in ipairs(chars) do all[#all + 1] = each end
            carried[index], carry = #carry, {}
            if #all > PAGE_CHARACTERS then
                local cut = PAGE_CHARACTERS
                if NO_START[all[cut + 1][1]] then cut = cut - 1 end
                for k = cut + 1, #all do carry[#carry + 1] = all[k] end
                for k = #all, cut + 1, -1 do all[k] = nil end
            end
            chars = all
        else
            carried[index] = 0
        end
        out[index] = chars
    end
    -- What the last row gives away is drawn on a row of its own below it (never seen).
    if #carry > 0 then out[last + 1], carried[last + 1] = carry, #carry end
    return out, carried
end

local reported = nil  -- how many rows had been there when text was last reported
local tail            -- the rows before it (not the last, which may grow), to find them again
local TAIL = 3        -- after the full list shifts
local last_row, last_size -- the last row reported, and how many bytes it had then
local settled_since
local windows = {}
local answers_shown
local answers_up = 0 -- how many answers the choice on the screen has, 0 while there is none

-- Touch play: which answer the cursor is on while a choice is up, -1 before the d-pad has put
-- it on one; nothing once the choice has gone. An older app has no reader.cursor.
local cursor_said
local function report_cursor()
    local at
    if answers_up > 0 then
        at = (mem.u16(ANSWER_CURSOR) or 0) - 1
        if at < 0 or at >= answers_up then at = -1 end
    end
    if at == cursor_said then return end
    cursor_said = at
    if reader.cursor then reader.cursor(at) end
end

-- `n` rows that end just before row `to`.
local function rows_before(to, n)
    local out = {}
    for index = to - n, to - 1 do out[#out + 1] = index >= 0 and row(index) or "" end
    return out
end

local function same(a, b)
    for index = 1, #a do
        if a[index] ~= b[index] then return false end
    end
    return true
end

local function withdraw()
    for name in pairs(windows) do reader.layout(name, nil) end
    windows = {}
end

-- A choice is up while the answers' box is.
local function choosing()
    return (mem.u32(CHOICE_BOX) or 0) ~= 0
end

-- A choice's answers as one event, one answer per line.
local function choice()
    local raw = mem.read(ANSWER_TEXT, ANSWER_SIZE * ANSWER_MAX)
    if not raw then return end
    local rows = {}
    -- The buffers keep old answers (also from earlier, longer choices): the box says whether a
    -- choice is up, the count how many answers it has.
    local wanted = choosing() and math.min(mem.u32(ANSWER_COUNT) or 0, ANSWER_MAX) or 0
    for index = 1, wanted do
        local one = raw:sub((index - 1) * ANSWER_SIZE + 1, index * ANSWER_SIZE):match("^[^%z]*")
        if one == "" then break end
        rows[#rows + 1] = one
    end
    local key = table.concat(rows, "\n")
    if #rows < 2 then
        answers_up = 0
        if answers_shown then
            reader.layout("choice", nil)
            answers_shown = nil
        end
        return
    end
    answers_up = #rows
    if key == answers_shown then return end
    answers_shown = key
    local b, cells = reader.builder(), {}
    for index, one in ipairs(rows) do
        if index > 1 then
            b:add("\n")
            cells[#cells + 1] = false
        end
        local chars = characters(one)
        local width = 0
        for _, each in ipairs(chars) do width = width + each[2] end
        local x = MIDDLE_X - width / 2
        local y = MIDDLE_Y - #rows * ROW_PITCH / 2 + (index - 1) * ROW_PITCH
        for _, each in ipairs(chars) do
            b:add(each[1])
            cells[#cells + 1] = { x, y, each[2], HEIGHT }
            x = x + each[2]
        end
    end
    reader.emit{ text = b:text(), channel = "choice", window = "choice", id = "choice:" .. text.hash(key) }
    reader.layout("choice", { space = { 640, 448 }, glyphs = cells })
end

reader.on_frame(function(frame)
    choice()
    report_cursor()
    local count = mem.u32(ROW_COUNT)
    if not count or count > 100000 then return end
    if reported == nil or count < reported then
        -- First frame, new game or loaded game: the existing rows are not new.
        reported, last_row, last_size, tail = count, nil, nil, rows_before(count - 1, TAIL)
        withdraw()
        return
    end
    -- The full list shifted: find the rows reported last.
    if tail and not same(rows_before(reported - 1, TAIL), tail) then
        local moved
        for by = 1, math.min(reported - 1 - TAIL, 200) do
            if same(rows_before(reported - 1 - by, TAIL), tail) then
                moved = by
                break
            end
        end
        if not moved then
            -- Not found (a loaded game with as many rows): the existing rows are not new.
            reported, last_row, last_size, tail = count, nil, nil, rows_before(count - 1, TAIL)
            withdraw()
            return
        end
        reported = reported - moved
        if last_row then last_row = last_row - moved end
    end
    -- While a choice is up, the message under it counts as typed out (the flag is not set then).
    if mem.u32(SETTLED) ~= 1 and not choosing() then
        settled_since = nil
        return
    end
    -- The last row with text (the game sometimes adds an empty one).
    local last = count - 1
    while last >= 0 and last >= count - 3 and blank(row(last)) do last = last - 1 end
    if last < 0 or blank(row(last)) then return end
    local grown = last == last_row and #row(last) > (last_size or 0)
    if count == reported and not grown then
        settled_since = nil
        return
    end
    settled_since = settled_since or frame
    if frame - settled_since < SHOWN_FRAMES then return end
    settled_since = nil

    -- New rows, or the last row continued after a mid-row pause.
    local from, skip = reported, 0
    if grown then
        from, skip = last, last_size
    elseif last_row and from <= last_row then
        from = last_row + 1
    end
    if from > last then
        reported = count
        tail = rows_before(count - 1, TAIL)
        return
    end
    local area = (mem.u16(ROW_PITCH_NOW) or 0) == PAGE.pitch and PAGE or BOX
    local at_row = mem.u16(WINDOW_ROW) or 0
    if at_row >= area.rows then at_row = 0 end
    -- Only rows the window shows: rows passed while skipping or going to the next choice are
    -- not read.
    if last - from >= area.rows then
        from, skip, grown = last - math.min(at_row, area.rows - 1), 0, false
    end
    if at_row - (last - from) <= 0 and not grown then
        withdraw() -- the window was cleared for these rows
    end

    local b, cells = reader.builder(), {}
    local speaker, ended, any
    -- On the page every row from its first one on counts; rows before it are not on the screen.
    local page_first = last - at_row
    local rows_drawn, carried = drawn(area == PAGE and page_first or math.min(from, page_first),
                                      last, area == PAGE)
    for index = from, last + 1 do
        local raw = row(index)
        local name = index <= last and raw:match("^ +(.-) *$")
        local chars = rows_drawn[index]
        if name and name ~= "" and index < last then
            speaker = speaker or text.sjis(name)
        elseif chars and #chars > 0 and not (index <= last and blank(raw)) then
            -- After a mid-row pause, the characters reported before are passed over.
            local first = 1
            if index == from and skip > 0 then
                first = carried[index] + #characters(raw:sub(1, skip)) + 1
            end
            local x = LEFT
            for k = 1, first - 1 do x = x + (chars[k] and chars[k][2] or 0) end
            local y = area.top + (at_row - (last - index)) * area.pitch
            for k = first, #chars do
                if not any then
                    any = true
                elseif k == first and ended then
                    b:add("\n")
                    cells[#cells + 1] = false
                end
                b:add(chars[k][1])
                cells[#cells + 1] = { x, y, chars[k][2], HEIGHT }
                x = x + chars[k][2]
            end
            ended = ENDS[chars[#chars][1]] or false
        end
    end
    reported, last_row, last_size = count, last, #row(last)
    tail = rows_before(count - 1, TAIL)
    local whole = b:text()
    if whole == "" then return end
    local window = "message" .. from .. (grown and "+" or "")
    reader.emit{ text = whole, speaker = speaker, window = window,
                 id = tostring(from) .. ":" .. skip .. ":" .. text.hash(whole) }
    reader.layout(window, { space = { 640, 448 }, glyphs = cells })
    windows[window] = true
end)
