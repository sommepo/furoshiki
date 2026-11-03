-- reader: title ときめきメモリアル forever with you (Tokimeki Memorial Forever with You)
-- reader: reads The message window with speakers, and choices
-- reader: touch press
-- reader: buttons up down left right circle cross start
-- reader: version ULJM-05078
-- Tokimeki Memorial Forever with You (Konami, PSP, 2006), ULJM-05078. EBOOT.BIN 04BDCD25.
--
-- THIS PROFILE HOOKS ONE ADDRESS (the game's "print a text" routine), and so reads nothing in
-- Adrenaline Reader, which has no hooks yet. The game is the PlayStation original ported with
-- its ways kept (the PlayStation's 2 MB of memory are a block of the PSP's): a text is turned
-- into pictures of its characters in video memory the moment it is printed, and its codes are
-- kept nowhere (no buffer, no pointer to it). Where the text comes from differs from scene to
-- scene (the opening and the command screens are code of their own, each with its own table
-- and counter; the events are scripts whose place is kept on the stack), so there is no one
-- place in memory to read. docs/reader/games/tokimeki-memorial.md has the whole of it.
--
-- What is read from memory, once a frame: the game's list of printed texts (where each stands,
-- how many characters it has, whether it is shown), how many characters of them the typing has
-- shown so far, and the cursor of a choice.
--
-- print(x, y, colour, text, ...): the game's routine that prints a text (Shift-JIS) at x, y in
--   the PlayStation's screen (320x224, from its middle). It makes the next entry of the list.
-- objects: the list, 8 bytes an entry: x and y (two bytes each, signed), a byte, the number of
--   characters, a byte, and a byte that is 1 while the entry is shown. A message of more than
--   three rows moves its entries up (and hides the first) as it is typed.
-- count: how many entries the list has (two bytes); glyphs: how many characters they have.
-- revealed: how many characters, counted over the whole list, the typing has shown.
local BUILD = "04BDCD25"
if reader.build_id ~= BUILD then
    error("this profile is for Tokimeki Memorial Forever with You, ULJM-05078 (EBOOT " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end
local base = reader.base - 0x08804000 -- the executable is not relocatable: this is zero
local PRINT = base + 0x08826ae0
local OBJECTS = base + 0x08f8ad30
local OBJECT_SIZE, OBJECT_MAX = 8, 0x34
local OBJECT_CHARS, OBJECT_SHOWN = 5, 7
local COUNT = base + 0x08f8aed0
local GLYPHS = base + 0x08f8aed2
local REVEALED = base + 0x08f8aed8

-- The game's second menu (the first is the command screen's icons): how many entries it has,
-- and the one the cursor is on (from 0). A choice in the message window is this menu.
local MENU_COUNT = base + 0x08fca329
local MENU_CURSOR = base + 0x08fca32e

-- Where the PlayStation's picture (320x224) is on the PSP's screen: x, y (two bytes each),
-- width, height (two each). It follows the frame chosen at the first start: full screen
-- ("TYPE A") 0, 0, 480, 272; enlarged ("TYPE B") 48, 0, 384, 272; the two of the original's
-- size (C and D) were not seen in a game. The PlayStation's x and y are from the middle of its screen. A
-- character is 12x12 and the next one stands 14 further.
local PICTURE = base + 0x0908bdd0
local ADVANCE, HEIGHT = 14, 12
local picture = { x = 0, y = 0, sx = 480 / 320, sy = 272 / 224 }
local function read_picture()
    local x, y = mem.u16(PICTURE), mem.u16(PICTURE + 2)
    local w, h = mem.u16(PICTURE + 4), mem.u16(PICTURE + 6)
    if x and y and w and h and w >= 160 and w <= 480 and h >= 112 and h <= 272 and
       x + w <= 480 and y + h <= 272 then
        picture = { x = x, y = y, sx = w / 320, sy = h / 224 }
    else
        picture = { x = 0, y = 0, sx = 480 / 320, sy = 272 / 224 }
    end
end
local function screen_x(x) return picture.x + (x + 160) * picture.sx end
local function screen_y(y) return picture.y + (y + 112) * picture.sy end

-- The message window's rows are at y 48/50, 64/66 and 80/82; the name stands left of the
-- first row (x -128 to -120), the opening bracket at -86, the rows at -72; the answers of a
-- choice at -128 (their bracket) and -114. Nothing of the window begins further right: what
-- does is another screen's (the new game's "これでいい？", the album screen's questions).
-- The window itself spans about x -144 to 144, y 38 to 104.
local BOX_TOP, BOX_BOTTOM, BOX_LEFT, BOX_RIGHT = 40, 90, -140, -72
local QUOTE_X = -86
local function box_area()
    local left, top = screen_x(-144), screen_y(38)
    return { left, top, screen_x(144) - left, screen_y(104) - top }
end

-- A message has to stand typed out for this many frames before it is read: what a quick
-- press runs through is not.
local SHOWN_FRAMES = 4

local function s16(v)
    v = (v or 0) & 0xffff
    if v >= 0x8000 then return v - 0x10000 end
    return v
end

-- A text's characters as the game prints them: two bytes each, until a zero byte; a code
-- below 0x8140 ends it (the game prints an error and stops there); 0x9999 is the game's own
-- picture of a trademark sign.
local function characters(raw)
    local out, i = {}, 1
    while i < #raw do
        local b1, b2 = raw:byte(i, i + 1)
        if b1 == 0 or b2 == 0 then break end
        local code = b1 * 256 + b2
        if code < 0x8140 then break end
        out[#out + 1] = code == 0x9999 and "™" or text.sjis(raw:sub(i, i + 1)) or "　"
        i = i + 2
    end
    return out
end

local function blank(chars)
    for _, c in ipairs(chars) do
        if c ~= "　" then return false end
    end
    return true
end

-- What was printed into the message window since it was last cleared: one message (or one
-- choice). prints: { index, x, y, chars, first (the first character's number in the list) }.
local message     -- { prints, speaker, done (prints read), windows, choice, serial }
local stood = 0
local serial = 0

local function close_windows(m)
    if not m then return end
    for name in pairs(m.windows) do reader.layout(name, nil) end
    m.windows = {}
    if m.choice then
        if reader.cursor then reader.cursor(nil) end
    end
end

local function finish()
    close_windows(message)
    message = nil
    stood = 0
end

reader.hook(PRINT, function(cpu)
    local index = s16(mem.u16(COUNT))
    if index < 0 or index >= OBJECT_MAX then return end
    local x, y = s16(cpu.a0), s16(cpu.a1)
    local in_box = y >= BOX_TOP and y <= BOX_BOTTOM and x >= BOX_LEFT and x <= BOX_RIGHT
    -- An entry made again where one of the message's stood: the list was cleared.
    if message and index <= message.prints[#message.prints].index then finish() end
    if not in_box then return end
    local raw = cpu.a3 ~= 0 and (mem.cstr(cpu.a3, 256) or "") or ""
    if not message then
        serial = serial + 1
        message = { prints = {}, done = 0, windows = {}, serial = serial }
    end
    local p = { index = index, x = x, y = y, chars = characters(raw),
                first = s16(mem.u16(GLYPHS)) }
    message.prints[#message.prints + 1] = p
end)

-- Whether the list still holds the print (the game has not cleared or replaced it).
local function standing(p)
    local count = s16(mem.u16(COUNT))
    if p.index >= count then return false end
    return mem.u8(OBJECTS + p.index * OBJECT_SIZE + OBJECT_CHARS) == #p.chars
end

-- The name: a print left of the opening bracket, with the bracket (or a row that begins with
-- it) right after it on the same row.
local function speaker_of(prints)
    local first, second = prints[1], prints[2]
    if first and second and first.x < QUOTE_X and second.y == first.y and second.x >= QUOTE_X and
       not blank(first.chars) then
        local name = table.concat(first.chars):gsub("　+$", "")
        if name ~= "" then return name, 2 end
    end
    return nil, 1
end

-- A choice: rows of a bracket of one character and the answer right after it.
local function answers_of(prints)
    local rows = {}
    local i = 1
    while i <= #prints do
        local mark, answer = prints[i], prints[i + 1]
        if not (answer and #mark.chars == 1 and answer.y == mark.y and
                answer.x == mark.x + ADVANCE and #answer.chars > 0) then
            return nil
        end
        rows[#rows + 1] = { mark, answer }
        i = i + 2
    end
    if #rows < 2 then return nil end
    return rows
end

-- The cells of a run of prints, where the game has them now (a long message moves its rows
-- up as it is typed), and false for a character that is not shown.
local function cells_of(runs)
    local cells = {}
    for _, item in ipairs(runs) do
        if item == "\n" then
            cells[#cells + 1] = false
        else
            local at = OBJECTS + item.index * OBJECT_SIZE
            local x, y = s16(mem.u16(at)), s16(mem.u16(at + 2))
            local shown = mem.u8(at + OBJECT_SHOWN) == 1 and standing(item)
            for k = 1, #item.chars do
                if shown then
                    cells[#cells + 1] = { screen_x(x + (k - 1) * ADVANCE), screen_y(y),
                                          ADVANCE * picture.sx, HEIGHT * picture.sy }
                else
                    cells[#cells + 1] = false
                end
            end
        end
    end
    return cells
end

local function same(a, b)
    return a and b and a.x == b.x and a.y == b.y and table.concat(a.chars) == table.concat(b.chars)
end

-- The text of a run of prints: a print that goes on where the one before ended is on the
-- same row (the bracket and its row); any other begins a new row. A text printed twice in the
-- same place (a large sound word, "ドン", is printed once more as its shadow) is read once.
local function compose(prints)
    local b, runs, before = reader.builder(), {}, nil
    for _, p in ipairs(prints) do
        if not blank(p.chars) and not same(p, before) then
            if before and not (p.y == before.y and p.x == before.x + #before.chars * ADVANCE) then
                b:add("\n")
                runs[#runs + 1] = "\n"
            end
            b:add(table.concat(p.chars))
            runs[#runs + 1] = p
            before = p
        end
    end
    return b:text(), runs
end

local function signature(runs)
    local parts = { string.format("%g,%g,%g,%g", picture.x, picture.y, picture.sx, picture.sy) }
    for _, item in ipairs(runs) do
        if item ~= "\n" then
            local at = OBJECTS + item.index * OBJECT_SIZE
            parts[#parts + 1] = string.format("%d,%d,%d", mem.u16(at) or 0, mem.u16(at + 2) or 0,
                                              mem.u8(at + OBJECT_SHOWN) or 0)
        end
    end
    return table.concat(parts, ";")
end

local function show(name, w)
    local s = signature(w.runs)
    if s == w.signature then return end
    w.signature = s
    reader.layout(name, { space = { 480, 272 }, box = box_area(), glyphs = cells_of(w.runs) })
end

reader.on_frame(function()
    if not message then return end
    read_picture()
    local prints = message.prints
    if not standing(prints[1]) then
        finish()
        return
    end
    -- What was read keeps its cells where the game has them now.
    for name, w in pairs(message.windows) do show(name, w) end

    if message.choice then
        local count = mem.u8(MENU_COUNT)
        local at = mem.u8(MENU_CURSOR)
        if reader.cursor and count == message.choice and at and at < count then
            reader.cursor(at)
        end
        return
    end
    if message.done >= #prints then return end

    -- Everything printed has to be typed out (the typing counts characters over the whole
    -- list) and stand a moment.
    local last = 0
    for _, p in ipairs(prints) do last = math.max(last, p.first + #p.chars) end
    if s16(mem.u16(REVEALED)) < last then
        stood = 0
        return
    end
    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0

    local rows = message.done == 0 and answers_of(prints)
    if rows then
        local b, runs = reader.builder(), {}
        for n, row in ipairs(rows) do
            if n > 1 then
                b:add("\n")
                runs[#runs + 1] = "\n"
            end
            b:add(table.concat(row[1].chars) .. table.concat(row[2].chars))
            runs[#runs + 1] = row[1]
            runs[#runs + 1] = row[2]
        end
        reader.emit{ text = b:text(), channel = "choice", window = "choice" }
        message.windows.choice = { runs = runs }
        show("choice", message.windows.choice)
        message.choice = #rows
        message.done = #prints
        return
    end

    local from = message.done + 1
    if message.done == 0 then
        local skip
        message.speaker, skip = speaker_of(prints)
        from = skip
    end
    local pending = {}
    for i = from, #prints do pending[#pending + 1] = prints[i] end
    message.done = #prints
    local line, runs = compose(pending)
    if line == "" then return end
    local window = "message" .. message.serial .. "." .. from
    reader.emit{ text = line, speaker = message.speaker, window = window }
    message.windows[window] = { runs = runs }
    show(window, message.windows[window])
end)

reader.on_state_loaded(function()
    finish()
end)
