-- reader: title My Merry May with be
-- reader: reads The story's text with speakers, in the message box and on the full-screen page, and choices
-- reader: touch press
-- reader: version ULJM-05603
-- My Merry May with be (KID/CYBERFRONT, PSP, 2009), ULJM-05603. EBOOT.BIN A0EDB17F.
--
-- The game keeps the text of the box (or of the full-screen page) as plain Shift-JIS at a
-- fixed place, the script's percent commands still in it, and draws it into a picture: there
-- is no list of characters. Where each one stands follows from the text and from the game's
-- cursor (the row and column the next character goes to), which this profile repeats.
-- Nothing is hooked.
--
-- shown: the text of the box or page, everything since it was last cleared: messages end in
--   %K (the wait for the button), then %N (a new row on the page), %P (the box is cleared)
--   or %O<n> (the page is cleared). A speaker is 【name】 in front of a message in the box.
-- waiting: a byte, 1 once the message is typed out and waits for the button.
-- page: a float, how opaque the full-screen page is: 1 while the text is on the page, 0 while
--   it is in the box.
-- active: two bytes, 1 while the text window is in use (0 in films and between scenes).
-- visible: two bytes, each 1 while the text can be seen: the first is 0 under the game's menu
--   (□) and its message log (△), the second while × hides the window.
-- clock: frames since the message began; it begins anew with each message.
-- row: the cursor's row, where the next character goes (from the top of the box or page,
--   from 0).
local BUILD = "A0EDB17F"
if reader.build_id ~= BUILD then
    error("this profile is for My Merry May with be, ULJM-05603 (EBOOT " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end
local base = reader.base - 0x08804000 -- the executable is not relocatable: this is zero
local SHOWN = base + 0x08a49b5c
local SHOWN_SIZE = 2048
local WAITING = base + 0x08a493c4
local PAGE_SHOWN = base + 0x08a492b4
local ACTIVE = base + 0x08a49b44
local VISIBLE = base + 0x08a492c4
local CLOCK = base + 0x08a4a93c
local ROW = base + 0x08e12f24

-- The choice: a window of its own at a fixed place. Its first byte is 1 while it is up, the
-- float after it its opacity; the answer the cursor is on is two bytes at +0x22 (from 0; the
-- first is lit from the start, and ○ picks it at once), how many answers there are two at
-- +0x24. The answers' texts are not there: they are in the scene's script, pointed to by the
-- game's task "select_call", which lives in the game's heap; the profile looks for it there
-- when a choice comes up.
local SELECT = base + 0x08a493d0
local SELECT_OPACITY, SELECT_CURSOR, SELECT_COUNT = 4, 0x22, 0x24
local TASKS_FROM, TASKS_SIZE = base + 0x08f80000, 0x20000
local TASK_NAME = "task select_call"
local TASK_ANSWERS, TASK_ANSWER = 0x20, 12 -- after the name: a pointer to each answer's text

-- The message has to stand for this many frames: what a skip runs through is not read.
local SHOWN_FRAMES = 4
-- The window has to be out of use for this many frames before its text goes.
local IDLE_FRAMES = 8

-- Where the game draws, in pixels of the 480x272 screen (measured in screenshots): the first
-- cell's left edge, a full-width character's step, how many fit in a row, the first row's
-- top, the step between rows and a character's height. The page has a larger step.
local BOX = { left = 28, step = 16, columns = 26, top = 200, pitch = 18, height = 16,
              area = { 20, 180, 440, 90 } }
local PAGE = { left = 32, step = 16.5, columns = 25, top = 31, pitch = 18.17, height = 17,
               area = { 20, 10, 440, 252 } }

-- The answers of a choice, measured with one of two answers: each row centred at this x, the
-- rows this far apart around this y (where a choice of other than two answers puts them was
-- not seen), a character's step.
local ANSWER_MIDDLE_X, ANSWER_MIDDLE_Y, ANSWER_PITCH, ANSWER_STEP = 239, 122, 19, 17

-- Characters that may stand after a full row instead of beginning the next one. The game
-- begins the next row as soon as a row is full (so a %N after it leaves an empty row), unless
-- one of these follows, which then stands at its end.
local HANGING = { ["、"] = true, ["。"] = true, ["」"] = true, ["』"] = true, ["）"] = true }

local function is_lead(c)
    return (c >= 0x81 and c <= 0x9f) or (c >= 0xe0 and c <= 0xfc)
end

-- The text as tokens: { char = UTF-8, width = 1 or 2 } for a character, { command = "K" }
-- (and N, P, O, LC...) for a percent command, its digits or colour taken out.
local function tokens(raw)
    local out, i = {}, 1
    while i <= #raw do
        local c = raw:byte(i)
        if c == 0x25 then -- %
            local name = raw:match("^%%(%a%u?)", i) -- %p (lowercase) ends a scene's last box
            if not name then
                i = i + 1
            else
                local length = 1 + #name
                local after = raw:sub(i + length)
                if name == "CG" then
                    length = length + #(after:match("^%x*") or "")
                else
                    length = length + #(after:match("^%d*") or "")
                end
                -- Two capitals that are not a command of two: one letter and the text.
                local known2 = { FS = true, FE = true, LC = true, CG = true, CE = true, XE = true }
                if #name == 2 and not known2[name] then
                    name = name:sub(1, 1)
                    length = 2 + #(raw:sub(i + 2):match("^%d*") or "")
                end
                out[#out + 1] = { command = name }
                i = i + length
            end
        elseif is_lead(c) and i < #raw then
            out[#out + 1] = { char = text.sjis(raw:sub(i, i + 1)), width = 2 }
            i = i + 2
        elseif c >= 0x20 then
            out[#out + 1] = { char = text.sjis(raw:sub(i, i)), width = 1 }
            i = i + 1
        else
            i = i + 1
        end
    end
    return out
end

-- The messages of the shown text, each laid out on rows from row 0, column 0 (the start of
-- the box or page): { speaker, chars = { {char, row, x (in cells), width} ... } }, and the
-- cursor's row at the last wait (the game's cursor stands there while it waits).
local function lay_out(list, layout)
    local messages, current = {}, nil
    local row, x = 0, 0
    local waited_row = 0 -- the cursor's row at the last wait: what follows is not done yet
    local function begin()
        if not current then
            current = { chars = {} }
            messages[#messages + 1] = current
        end
    end
    local i = 1
    while i <= #list do
        local t = list[i]
        if t.command == "N" then
            row, x = row + 1, 0
            -- A row break inside a message is the writer's: it stays in the text.
            if current and #current.chars > 0 then current.chars[#current.chars].breaks = true end
        elseif t.command == "K" then
            if current then current.done = true end
            current = nil
            waited_row = row
        elseif t.char then
            begin()
            -- 【name】 in front of a message is its speaker (the box shows it on its plate).
            if t.char == "【" and #current.chars == 0 and not current.speaker then
                local name, j = {}, i + 1
                while j <= #list and list[j].char and list[j].char ~= "】" do
                    name[#name + 1] = list[j].char
                    j = j + 1
                end
                if j <= #list and list[j].char == "】" then
                    current.speaker = table.concat(name)
                    i = j
                    goto continue
                end
            end
            local width = t.width / 2
            if x + width > layout.columns and not (HANGING[t.char] and x <= layout.columns) then
                row, x = row + 1, 0
            end
            current.chars[#current.chars + 1] = { t.char, row, x, width }
            x = x + width
            -- A full row ends at once, unless what comes next may hang after it.
            local following = list[i + 1]
            if x >= layout.columns and not (following and HANGING[following.char] and x == layout.columns) then
                row, x = row + 1, 0
            end
        end
        ::continue::
        i = i + 1
    end
    return messages, waited_row
end

-- A message as text and a cell for every character (false for a line break).
local function compose(message, layout, shift)
    local b, cells = reader.builder(), {}
    for n, c in ipairs(message.chars) do
        b:add(c[1])
        cells[#cells + 1] = { layout.left + c[3] * layout.step,
                              layout.top + (c[2] + shift) * layout.pitch,
                              c[4] * layout.step, layout.height }
        if c.breaks and n < #message.chars then
            b:add("\n")
            cells[#cells + 1] = false
        end
    end
    return b:text(), cells
end

local reported = 0   -- how many messages of the shown text were reported
local shown_raw      -- the shown text they were reported from
local windows = {}   -- the layouts of what was reported, by window
local hidden = false -- the text is under a menu or the log, or hidden: its layouts are down
local stood = 0
local idle = 0       -- frames the window has not been in use
local clock_before = 0
local boxed_before

local function withdraw()
    if not hidden then
        for name in pairs(windows) do reader.layout(name, nil) end
    end
    windows = {}
end

local function reset()
    withdraw()
    reported, shown_raw = 0, nil
end

reader.on_frame(function()
    local active = mem.u16(ACTIVE)
    -- Under a choice the window counts as not in use, but the message stays on screen; the
    -- window is let go a few frames before the choice comes up.
    if active ~= 1 then
        stood = 0
        if mem.u8(SELECT) == 1 then
            idle = 0
        else
            idle = idle + 1
            if idle >= IDLE_FRAMES and shown_raw then reset() end
        end
        return
    end
    idle = 0
    -- Under the menu or the log, or with the window hidden, nothing can be pointed at; the
    -- text comes back as it was, without being read again.
    local visible = mem.u16(VISIBLE) == 0x0101
    if visible == hidden then
        hidden = not visible
        for name, layout in pairs(windows) do reader.layout(name, visible and layout or nil) end
    end
    if hidden then return end
    local boxed = (mem.f32(PAGE_SHOWN) or 0) < 0.5
    if boxed ~= boxed_before then
        reset()
        boxed_before = boxed
    end
    local raw = mem.cstr(SHOWN, SHOWN_SIZE) or ""
    -- The box or page was cleared: what was reported from it goes. (A box can hold several
    -- messages, each after a wait, like a page.) The same text shown again in a new box is
    -- told by the clock, which begins anew with each message.
    local clock = mem.u32(CLOCK) or 0
    if shown_raw and (raw:sub(1, #shown_raw) ~= shown_raw or
                      (boxed and raw == shown_raw and clock < clock_before)) then
        reset()
    end
    clock_before = clock
    if mem.u8(WAITING) ~= 1 then
        stood = 0
        return
    end
    if raw == shown_raw then return end
    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0

    local layout = boxed and BOX or PAGE
    local messages, end_row = lay_out(tokens(raw), layout)
    shown_raw = raw
    -- The game's cursor says where the rows really are (a page can begin lower down, after
    -- empty rows the text no longer has).
    local shift = 0
    local row = mem.u32(ROW)
    if row and row < 40 then shift = row - end_row end
    for n = reported + 1, #messages do
        local message = messages[n]
        if message.done and #message.chars > 0 then
            -- Rows the game broke because they were full go on without a break.
            local line, cells = compose(message, layout, shift)
            local window = "message" .. n
            reader.emit{ text = line, speaker = message.speaker, window = window }
            windows[window] = { space = { 480, 272 }, box = layout.area, glyphs = cells }
            reader.layout(window, windows[window])
            reported = n
        end
    end
end)

-- The choice: one event, an answer to a line.
local task         -- where the task was found last
local choice_stood = 0
local choice_shown = false

local function find_task()
    if task and mem.read(task, #TASK_NAME) == TASK_NAME then return task end
    local area = mem.read(TASKS_FROM, TASKS_SIZE)
    local at = area and area:find(TASK_NAME, 1, true)
    task = at and TASKS_FROM + at - 1 or nil
    return task
end

local function read_choice(count)
    local found = find_task()
    if not found then return nil end
    local b, cells = reader.builder(), {}
    local top = ANSWER_MIDDLE_Y - count * ANSWER_PITCH / 2
    for n = 0, count - 1 do
        local at = mem.u32(found + TASK_ANSWERS + n * TASK_ANSWER)
        local raw = at and mem.cstr(at, 128) or ""
        local chars = {}
        for _, t in ipairs(tokens(raw)) do
            if t.char then chars[#chars + 1] = t end
        end
        if #chars == 0 then return nil end
        local width = 0
        for _, t in ipairs(chars) do width = width + t.width / 2 * ANSWER_STEP end
        local x = ANSWER_MIDDLE_X - width / 2
        if n > 0 then
            b:add("\n")
            cells[#cells + 1] = false
        end
        for _, t in ipairs(chars) do
            local w = t.width / 2 * ANSWER_STEP
            b:add(t.char)
            cells[#cells + 1] = { x, top + n * ANSWER_PITCH, w, 17 }
            x = x + w
        end
    end
    return b:text(), cells
end

local function close_choice()
    choice_stood = 0
    if reader.cursor then reader.cursor(nil) end
    if choice_shown then
        reader.layout("choice", nil)
        choice_shown = false
    end
end

reader.on_frame(function()
    local count = mem.u16(SELECT + SELECT_COUNT) or 0
    local up = mem.u8(SELECT) == 1 and (mem.f32(SELECT + SELECT_OPACITY) or 0) > 0.99 and
               count >= 1 and count <= 8
    if not up then
        close_choice()
        return
    end
    if choice_shown then
        -- Which answer the cursor is on: what a tap on an answer is turned into presses with.
        local at = mem.u16(SELECT + SELECT_CURSOR)
        if at and at < count and reader.cursor then reader.cursor(at) end
        return
    end
    choice_stood = choice_stood + 1
    if choice_stood < SHOWN_FRAMES then return end
    local answers, cells = read_choice(count)
    if not answers then return end
    reader.emit{ text = answers, channel = "choice", window = "choice" }
    reader.layout("choice", { space = { 480, 272 }, glyphs = cells })
    choice_shown = true
end)

reader.on_state_loaded(function()
    close_choice()
    task = nil
    reset()
    hidden = false
    stood, clock_before, boxed_before = 0, 0, nil
end)
