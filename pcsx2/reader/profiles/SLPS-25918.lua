-- reader: no hooks
-- reader: title アマガミ
-- reader: reads The message window's text with furigana, each message when it begins, and choices
-- reader: version SLPS-25918
-- reader: touch press
-- reader: buttons start triangle up down circle cross
-- Touch play's buttons (found in the harness, 2026-10-16): START at the title (○ does nothing
-- there), up and down for the title's and the menu's lists, ○ to choose (a tap on the picture
-- is ○ too, but a button is sure), × back out; △ the menu, × held
-- skips, L1 the backlog, SELECT the help screen, START auto (its help screen).
--
-- アマガミ (Enterbrain, PS2, 2009), SLPS-25918. Program D72C9E2A.
--
-- Two sources are read:
-- - The scene script: an array of 20-byte values (two numbers, unused, a kind, a field of the
--   kind) that includes the message texts, plus, per running script, a pointer to the value
--   it used last. While a message is up, the scene script points 12 bytes past its text.
-- - The draw list: one 24x14 glyph per character on screen (per field of the interlaced
--   picture, drawn twice for the shadow) with its position. The last glyphs in reading order
--   are the message's; a speaker's name and the previous message may stand before them.
-- The script pointer rarely stays on the message being shown (almost never for voiced lines),
-- so the message is identified from the glyphs. Once they stop changing, every text near the
-- running scripts is matched against them: same length, same row breaks, the same glyph
-- wherever the character is the same. Each match teaches glyph-to-character pairs; a text
-- with half its glyphs known matches at once. Before anything is learnt (after a start) a
-- text is taken when the glyphs have stood for STOOD checks and it is the only full match,
-- or when a script was at it, so the first short messages may be missed. A message is
-- reported once complete.
--
-- A text value is its Shift-JIS length and a pointer to its characters: 16-bit Shift-JIS with
-- the bytes swapped, or single bytes for inline marks (／ starts a row, W<n> waits, V<n> is
-- the voice, R word R <n> reading puts a reading over n characters). A choice is its answers
-- in sequence, each starting with ・; the script points at the first.

local BUILD = "D72C9E2A"
if reader.build_id ~= BUILD then
    error("this profile is for Amagami SLPS-25918 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local SCRIPTS, SCRIPTS_COUNT = 0x003a6bd8, 12   -- the running scripts, one pointer each
                                                -- (the value a script is at is 12 bytes before)
local AT, VALUE_SIZE = -12, 20
local V_LENGTH, V_CHARS, V_KIND, TEXT_KIND, TEXT_TYPE = 0x00, 0x04, 0x0c, 0x300, 0x010a994c
local NEAR = 0x3000    -- a scene's texts lie within so many bytes of where its script is
local LENGTH_MAX = 800
-- The draw list: a pool of 32-byte entries (owning list, next entry, unused, item, size). An
-- item with a picture is 0x60 bytes, its corners at 0x48 and 0x58 (x, y as 16-bit sixteenths)
-- in console coordinates; origin (1728, 1936) for this picture, every second line.
local POOL, POOL_END, ENTRY_SIZE = 0x003dc000, 0x003e9000, 0x20
local DRAWN_LIST, THING_SIZE = 0x002847c8, 0x60
local THINGS, THINGS_END = 0x00554000, 0x00578000
local ORIGIN_X, ORIGIN_Y = 1728, 1936
local GLYPH_W, GLYPH_H = 24, 14               -- a character's picture, in a field
local WINDOW_TOP, WINDOW_BOTTOM = 270, 430    -- the message window, in the picture of 448 lines
local CELL_HEIGHT = 28
-- Glyphs are read every EVERY frames. A match needs STEADY unchanged reads; a lone match
-- (nothing learnt yet) needs STOOD unchanged reads and at least ALONE_MIN glyphs.
local EVERY, STEADY, STOOD, ALONE_MIN = 4, 3, 20, 8
local ANSWER_MARK, SPACE = "・", "　"
local NAME_GLYPHS = 3
-- Frames a text is kept (RECENT) and counts as recent (LATELY) after a script was at it.
local RECENT, LATELY = 3600, 240
local SHEET = 1024     -- glyph sheet size; sheet positions are taken modulo it
local ANSWERS_MAX = 8
-- The bar that lights the answer the cursor is on: a plain item of the draw list (0x50 bytes,
-- its corners at 0x40 and 0x48), 560 by 36 in the picture, over the answer's row; there is
-- none until the d-pad is first pressed at a choice (○ picks nothing then). Measured at the
-- first choice of a new game.
local BAR_SIZE, BAR_W, BAR_H = 0x50, 560, 36

local function valid(p)
    return p and p >= 0x00100000 and p < 0x02000000
end

-- The text value at `address` as a list of units: a character (UTF-8) or one byte below 0x80.
local function read_text(address)
    local head = mem.read(address, VALUE_SIZE)
    if not head or #head < VALUE_SIZE then return nil end
    local length, chars = string.unpack("<I4I4", head, V_LENGTH + 1)
    local kind = string.unpack("<I4", head, V_KIND + 1)
    if kind ~= TEXT_KIND or not valid(chars) or length < 2 or length > LENGTH_MAX then return nil end
    -- The length is the text's in Shift-JIS: at most twice as many bytes here, a zero ends it.
    local raw = mem.read(chars, length * 2)
    if not raw or #raw < length * 2 then return nil end
    local units = {}
    for at = 1, #raw - 1, 2 do
        local low, high = raw:byte(at), raw:byte(at + 1)
        if low == 0 and high == 0 then
            raw = raw:sub(1, at - 1)
            break
        end
        if high == 0 then
            units[#units + 1] = string.char(low)
        else
            units[#units + 1] = text.sjis(string.char(high, low))
        end
    end
    return units, raw
end

local function is_digit(u) return u and #u == 1 and u >= "0" and u <= "9" end

-- A message's units as rows of characters, with the readings: { rows = { { ch... } },
-- ruby = { { row, first, count, reading } } }.
local function parse(units)
    local rows, row, ruby = {}, {}, {}
    local at = 1
    local function number()
        local n = 0
        while is_digit(units[at]) do
            n = n * 10 + tonumber(units[at])
            at = at + 1
        end
        return n
    end
    while at <= #units do
        local u = units[at]
        if u == "／" then
            rows[#rows + 1] = row
            row = {}
            at = at + 1
        elseif u == "R" then
            -- R word R count reading
            at = at + 1
            local first = #row + 1
            while at <= #units and units[at] ~= "R" do
                row[#row + 1] = units[at]
                at = at + 1
            end
            at = at + 1
            local count, reading = number(), {}
            for _ = 1, count do
                reading[#reading + 1] = units[at] or ""
                at = at + 1
            end
            if #row >= first then
                ruby[#ruby + 1] = { #rows + 1, first, #row - first + 1, table.concat(reading) }
            end
        elseif #u == 1 and u >= "A" and u <= "Z" then
            -- W and a number waits, V and a number is the voice, others move a face: a mark
            -- is a capital letter and the letters and digits after it.
            at = at + 1
            while units[at] and #units[at] == 1 and units[at]:match("%w") do at = at + 1 end
        elseif #u == 1 and u:byte() < 0x20 then
            at = at + 1
        else
            row[#row + 1] = u
            at = at + 1
        end
    end
    rows[#rows + 1] = row
    return { rows = rows, ruby = ruby }
end

-- Whether units are a text to read: kana or kanji in it, and nothing that is not a character.
local function japanese(units)
    local found = false
    for _, u in ipairs(units) do
        local b = u:byte(1)
        if #u == 1 then
            if b < 0x20 or b > 0x7e then return false end
        elseif #u == 3 then
            if b >= 0xe3 and b <= 0xe9 then found = true end
            if u == "\239\191\189" or (b == 0xef and u:byte(2) >= 0xbd and u ~= "～") then
                -- not a character, or half-width kana, which a message has none of
                if u:byte(2) == 0xbd and u:byte(3) >= 0xa1 or u:byte(2) == 0xbe then return false end
                if u == "\239\191\189" then return false end
            end
        else
            return false
        end
    end
    return found
end

-- A message's text: its rows are one text, the space a later row is set in by left out.
-- `slots` says of each character of the text whether the game draws a picture for it (it
-- draws none for a space).
local function compose_message(parsed)
    local b, ruby, slots = reader.builder(), {}, {}
    local starts = {}
    for index, row in ipairs(parsed.rows) do
        starts[index] = {}
        local leading = index > 1
        for k, ch in ipairs(row) do
            starts[index][k] = b:length()
            if leading and ch == SPACE then
                -- left out
            else
                leading = false
                b:add(ch)
                slots[#slots + 1] = ch ~= SPACE and "drawn" or "space"
            end
        end
        starts[index][#row + 1] = b:length()
    end
    for _, group in ipairs(parsed.ruby) do
        local from, to = starts[group[1]][group[2]], starts[group[1]][group[2] + group[3]]
        if from and to and to > from then ruby[#ruby + 1] = { from, to - from, group[4] } end
    end
    return b:text(), ruby, slots
end

-- A choice's text: the answers a line each, without their marks.
local function compose_choice(answers)
    local b, slots = reader.builder(), {}
    for index, row in ipairs(answers) do
        if index > 1 then
            b:add("\n")
            slots[#slots + 1] = "break"
        end
        slots[#slots + 1] = "mark"
        for k = 2, #row do
            b:add(row[k])
            slots[#slots + 1] = row[k] ~= SPACE and "drawn" or "space"
        end
    end
    return b:text(), slots
end

-- The glyphs in the message window in reading order: { x, y, id } in the 640x448 picture;
-- `id` is the glyph's position in the game's glyph sheet, the same for the same character.
-- Each character is drawn twice, the shadow slightly right and below.
local function drawn_glyphs()
    local pool = mem.read(POOL, POOL_END - POOL)
    local things = mem.read(THINGS, THINGS_END - THINGS)
    if not pool or not things then return nil end
    local found, seen = {}, {}
    for at = 1, #pool - ENTRY_SIZE + 1, ENTRY_SIZE do
        local list, _, _, thing, size = string.unpack("<I4I4I4I4I4", pool, at)
        if list == DRAWN_LIST and size == THING_SIZE and thing >= THINGS and thing + THING_SIZE <= THINGS_END then
            local o = thing - THINGS
            local u, v = string.unpack("<I2I2", things, o + 0x40 + 1)
            local x0, y0 = string.unpack("<I2I2", things, o + 0x48 + 1)
            local x1, y1 = string.unpack("<I2I2", things, o + 0x58 + 1)
            if x1 - x0 == GLYPH_W * 16 and y1 - y0 == GLYPH_H * 16 then
                local x, y = x0 // 16 - ORIGIN_X, (y0 // 16 - ORIGIN_Y) * 2
                if y >= WINDOW_TOP and y <= WINDOW_BOTTOM and x >= 0 and x < 640 and not seen[x * 1024 + y] then
                    seen[x * 1024 + y] = true
                    found[#found + 1] = { x, y, ((u // 16) % SHEET) * 65536 + (v // 16) % SHEET }
                end
            end
        end
    end
    -- Drop the shadows.
    local glyphs = {}
    for _, g in ipairs(found) do
        if not seen[(g[1] - 2) * 1024 + g[2] - 2] then glyphs[#glyphs + 1] = g end
    end
    table.sort(glyphs, function(a, b)
        if a[2] ~= b[2] then return a[2] < b[2] end
        return a[1] < b[1]
    end)
    return glyphs
end

-- Glyph to character, learnt from matched messages.
local learnt = {}
local drawn_as = {}   -- character to glyph
local blank          -- the empty glyph that pads names

-- Whether the glyphs from `start` on can be the first characters of `drawn` (characters,
-- "\n" between rows): rows break at the same places, and a glyph repeats exactly where its
-- character does. Also returns whether they cover all of it, the glyph count, and how many
-- glyphs are known to be their characters and how many are known as others.
local function fits(drawn, glyphs, start)
    local at = start - 1
    local by_char, by_id = {}, {}
    local first = true
    local agree, differ = 0, 0
    local complete = true
    for _, ch in ipairs(drawn) do
        if ch == "\n" then
            first = true
        else
            at = at + 1
            local g = glyphs[at]
            if not g then
                complete = false
                break
            end
            -- A row starts where y changes, and nowhere else inside the text.
            if at > start and (glyphs[at - 1][2] ~= g[2]) ~= first then return false end
            first = false
            if by_char[ch] and by_char[ch] ~= g[3] then return false end
            if by_id[g[3]] and by_id[g[3]] ~= ch then return false end
            by_char[ch], by_id[g[3]] = g[3], ch
            local known = learnt[g[3]]
            if known == ch then
                agree = agree + 1
            elseif known or (drawn_as[ch] and drawn_as[ch] ~= g[3]) then
                differ = differ + 1
            end
        end
    end
    if at < #glyphs then return false end
    return true, complete, #glyphs - start + 1, agree, differ
end

-- The texts of the scene: every text value near where the scripts are. { address = { units, raw } }
local TEXT_VALUE = string.pack("<I4I4I4", 0, TEXT_KIND, TEXT_TYPE)
local function scene_texts(slots)
    local out = {}
    for k = 0, SCRIPTS_COUNT - 1 do
        local script = string.unpack("<I4", slots, k * 4 + 1)
        if valid(script) then
            local from = script - NEAR
            local raw = mem.read(from, NEAR * 2)
            local at = 1
            while raw do
                local i = raw:find(TEXT_VALUE, at, true)
                if not i then break end
                local address = from + i - 1 - 8
                if i > 8 and not out[address] then
                    local units, chars = read_text(address)
                    if units and japanese(units) then out[address] = { units = units, raw = chars } end
                end
                at = i + 4
            end
        end
    end
    return out
end

-- The texts the scripts were at lately: by address { units, raw, when }.
local recent = {}
local last_key           -- the text reported last
local steady_key, steady = nil, 0
local resolved = false   -- whether the pictures on the screen were dealt with
local layouts = {}

-- Touch play: the rows of the choice on the screen (each answer's top in the picture), and the
-- answer the cursor was last reported on.
local answer_rows
local cursor_said

local function say_cursor(at)
    if at == cursor_said then return end
    cursor_said = at
    if reader.cursor then reader.cursor(at) end
end

local function drop()
    for name in pairs(layouts) do reader.layout(name, nil) end
    layouts = {}
    answer_rows = nil
    say_cursor(nil)
end

-- Which answer the bar lights: the row whose characters stand in it; -1 when there is no bar.
local function lit_answer()
    local pool = mem.read(POOL, POOL_END - POOL)
    if not pool then return -1 end
    for at = 1, #pool - ENTRY_SIZE + 1, ENTRY_SIZE do
        local thing, size = string.unpack("<I4I4", pool, at + 12)
        if size == BAR_SIZE and thing >= THINGS and thing + BAR_SIZE <= THINGS_END then
            local corners = mem.read(thing + 0x40, 12)
            if corners and #corners == 12 then
                local x0, y0 = string.unpack("<I2I2", corners, 1)
                local x1, y1 = string.unpack("<I2I2", corners, 9)
                if (x1 - x0) // 16 == BAR_W and (y1 - y0) * 2 // 16 == BAR_H then
                    local top = (y0 / 16 - ORIGIN_Y) * 2
                    for index, row in ipairs(answer_rows) do
                        if row >= top - 4 and row < top + BAR_H then return index - 1 end
                    end
                end
            end
        end
    end
    return -1
end

reader.on_state_loaded(function()
    drop()
    recent, learnt, drawn_as, blank, last_key, steady_key, resolved = {}, {}, {}, nil, nil, nil, false
end)

-- What the game draws of a text: its characters with "\n" between rows; for a choice, the
-- answers after the first, one per row.
local function drawn_of(address, units)
    local drawn, parsed = {}, nil
    if units[1] == ANSWER_MARK then
        local answers = {}
        for k = 0, ANSWERS_MAX - 1 do
            local more = read_text(address - k * 2 * VALUE_SIZE)
            if not more or more[1] ~= ANSWER_MARK then break end
            answers[#answers + 1] = parse(more).rows[1]
        end
        for index, row in ipairs(answers) do
            if index > 1 then drawn[#drawn + 1] = "\n" end
            for _, ch in ipairs(row) do
                if ch ~= SPACE then drawn[#drawn + 1] = ch end
            end
        end
        return drawn, nil, answers
    end
    parsed = parse(units)
    for index, row in ipairs(parsed.rows) do
        if index > 1 then drawn[#drawn + 1] = "\n" end
        for _, ch in ipairs(row) do
            if ch ~= SPACE then drawn[#drawn + 1] = ch end
        end
    end
    return drawn, parsed, nil
end

reader.on_frame(function()
    local frame = reader.frame()
    -- Several scripts run at once (one is the scene's); remember the texts they are at.
    local slots = mem.read(SCRIPTS, SCRIPTS_COUNT * 4)
    if slots and #slots == SCRIPTS_COUNT * 4 then
        for k = 0, SCRIPTS_COUNT - 1 do
            local script = string.unpack("<I4", slots, k * 4 + 1)
            if valid(script) then
                local address = script + AT
                local known = recent[address]
                if known then
                    known.when = frame
                else
                    local units, raw = read_text(address)
                    if units and japanese(units) then
                        recent[address] = { units = units, raw = raw, when = frame }
                    end
                end
            end
        end
    end
    if frame % EVERY ~= 0 then return end
    if answer_rows then say_cursor(lit_answer()) end
    for address, known in pairs(recent) do
        if frame - known.when > RECENT then recent[address] = nil end
    end

    local glyphs = drawn_glyphs()
    if not glyphs or #glyphs == 0 then
        if next(layouts) then drop() end
        steady_key, resolved = nil, false
        return
    end
    local parts = {}
    for k, g in ipairs(glyphs) do parts[k] = g[1] * 1024 + g[2] end
    local key = table.concat(parts, ",")
    if key ~= steady_key then
        -- The glyphs changed: withdraw the layouts.
        if next(layouts) then drop() end
        steady_key, steady, resolved = key, 1, false
        return
    end
    steady = steady + 1
    -- Matched when the glyphs have just settled, and again once they have stood longer than
    -- any wait inside a message.
    if resolved or (steady ~= STEADY and steady ~= STOOD) then return end
    local stood = steady == STOOD

    -- The glyphs have settled (or paused at a wait). Candidates: texts that fit and that a
    -- script was at recently, or with half their glyphs known and none contradicting; the
    -- one with the most known glyphs wins.
    -- A message starts a row, after a three-glyph name or without one; the previous message
    -- may still fill the rows above.
    local starts, named_start = {}, {}
    for k, g in ipairs(glyphs) do
        if k == 1 or glyphs[k - 1][2] ~= g[2] then
            starts[#starts + 1] = k
            if glyphs[k + NAME_GLYPHS] and glyphs[k + NAME_GLYPHS][2] == g[2] then
                starts[#starts + 1] = k + NAME_GLYPHS
                named_start[k + NAME_GLYPHS] = true
            end
        end
    end
    local best, tie
    local rival = -1       -- how well a longer text fits what is there so far
    local alone, several   -- the one text that is all there, if it is one
    for address, one in pairs(scene_texts(slots)) do
        local drawn, parsed, answers = drawn_of(address, one.units)
        local lately = recent[address] and frame - recent[address].when <= LATELY
        for _, start in ipairs(starts) do
            local ok, complete, count, agree, differ = fits(drawn, glyphs, start)
            if ok and not complete then
                if differ == 0 and agree > rival then rival = agree end
            elseif ok then
                if differ == 0 and count >= ALONE_MIN then
                    if alone and alone.raw ~= one.raw then
                        several = true
                    else
                        alone = { address = address, raw = one.raw, drawn = drawn, parsed = parsed, answers = answers,
                                  count = count, agree = agree, differ = 0, named = named_start[start], lately = 0 }
                    end
                end
                local sure = differ == 0 and count >= 3 and agree * 2 >= count
                local pointed = lately and count >= 6 and differ * 4 <= count
                if sure or pointed then
                    local this = { address = address, raw = one.raw, drawn = drawn, parsed = parsed, answers = answers,
                                   count = count, agree = agree, differ = differ, named = named_start[start],
                                   lately = lately and 1 or 0 }
                    local function better(x, y)
                        if (x.differ == 0) ~= (y.differ == 0) then return x.differ == 0 end
                        if x.agree ~= y.agree then return x.agree > y.agree end
                        if x.lately ~= y.lately then return x.lately > y.lately end
                        return nil
                    end
                    if not best then
                        best, tie = this, false
                    else
                        local verdict = better(this, best)
                        if verdict == true then
                            best, tie = this, false
                        elseif verdict == nil and this.raw ~= best.raw then
                            tie = true
                        end
                    end
                end
            end
        end
    end
    -- A longer text also fits so far: the message may still be coming.
    if best and best.lately == 0 and rival >= best.agree then best = nil end
    -- No glyphs known yet, but they have stood and exactly one text fits completely: take it
    -- (how the first messages after a start are identified).
    if (not best or tie) and stood and alone and not several then best, tie = alone, false end
    -- Two texts fit equally well: wait until more glyphs are known.
    if not best or tie then return end
    do
        local at = #glyphs - best.count
        for _, ch in ipairs(best.drawn) do
            if ch ~= "\n" then
                at = at + 1
                local id = glyphs[at][3]
                if learnt[id] and learnt[id] ~= ch then drawn_as[learnt[id]] = nil end
                if drawn_as[ch] and drawn_as[ch] ~= id then learnt[drawn_as[ch]] = nil end
                learnt[id], drawn_as[ch] = ch, id
            end
        end
    end
    resolved = true
    local text_key = text.hash(best.raw) .. ":" .. string.format("%x", best.address)
    local whole, ruby, slots_, window, channel
    if best.answers then
        whole, slots_ = compose_choice(best.answers)
        window, channel = "choice", "choice"
    else
        whole, ruby, slots_ = compose_message(best.parsed)
        window = "message"
    end
    if #whole == 0 then return end
    if text_key ~= last_key then
        last_key = text_key
        drop()
        -- The speaker's name, once its glyphs are known: a blank glyph sits between the two
        -- characters of a two-character name and on both sides of a one-character name.
        local speaker
        if best.named and not best.answers then
            local first = #glyphs - best.count - NAME_GLYPHS + 1
            local a, b, c = glyphs[first][3], glyphs[first + 1][3], glyphs[first + 2][3]
            if a == c then
                blank = a
            elseif learnt[a] and learnt[c] and not learnt[b] then
                blank = b
            end
            local name = {}
            for _, id in ipairs({ a, b, c }) do
                if id ~= blank then
                    if not learnt[id] then name = nil break end
                    name[#name + 1] = learnt[id]
                end
            end
            speaker = name and #name > 0 and table.concat(name) or nil
        end
        reader.emit{ text = whole, speaker = speaker, ruby = ruby, channel = channel, window = window, id = text_key }
    end
    local cells, at = {}, #glyphs - best.count
    local rows = {}
    for _, slot in ipairs(slots_) do
        if slot == "drawn" then
            at = at + 1
            cells[#cells + 1] = { glyphs[at][1], glyphs[at][2], GLYPH_W - 1, CELL_HEIGHT }
        elseif slot == "mark" then
            at = at + 1
            rows[#rows + 1] = glyphs[at][2] -- an answer's mark begins its row
        else
            cells[#cells + 1] = false
        end
    end
    if best.answers and not answer_rows then
        answer_rows = rows
        say_cursor(lit_answer())
    end
    reader.layout(window, { space = { 640, 448 }, glyphs = cells })
    layouts[window] = true
end)
