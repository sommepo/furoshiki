-- reader: no hooks
-- reader: title サクラ大戦 (Sakura Taisen)
-- reader: reads The message window and the answers of a timed choice (LIPS)
-- reader: version HDR-0072 (Japan), both discs
-- reader: touch press
-- Sakura Taisen (Sega, Dreamcast, 2000). docs/reader/games/sakura-taisen.md has what is read,
-- what is not, and how it was checked.
--
-- A message is a Shift-JIS string with "$$" where a new row begins. The message window's
-- record has a pointer to the message being printed and one to how far the printing has got,
-- and a flag that is set while it prints. A timed choice has a record of its own at a fixed
-- place: an object's pointer while it is up, its answers 100 bytes apart, and how many.

local BUILDS = { ["94AC613E"] = true, ["F73F4DE9"] = true }    -- disc 1, disc 2
if not BUILDS[reader.build_id] then
    error("this profile is for Sakura Taisen HDR-0072 (94AC613E, F73F4DE9), the disc is " .. tostring(reader.build_id))
end

local WINDOW = 0x8C1D726C     -- pointer to the message window's record
local START, POS, BUSY = 0x74, 0x78, 0x6C

local CHOICE = 0x8C1F7AD4     -- the timed choice
local CHOICE_OBJECT, CHOICE_TEXT, CHOICE_STEP, CHOICE_COUNT = 0x04, 0x0C, 0x64, 0x138
local CURSOR = 0x64 -- in the choice's object: the answer the cursor is on, from 0

-- The message window's grid, in the 640x480 picture.
local LEFT, TOP, CELL, PITCH, HEIGHT = 128, 338, 30, 32, 30
-- A choice's rows: centred, narrowed to fit.
local CHOICE_CENTRE, CHOICE_WIDTH, CHOICE_CELL, CHOICE_HEIGHT = 320, 404, 29, 30
-- The rows share a height of 180 between them and the box's edges.
local CHOICE_MIDDLE, CHOICE_SPAN = 196, 180

-- Two of the game's own characters stand where the standard has other ones.
local OWN = { ["〓"] = "⁉", ["∈"] = "‼" }

local shown = { message = false, choice = false }
local last_start, last_text, fresh = nil, nil, true
local last_choice = nil

local function withdraw(window)
    if shown[window] then
        reader.layout(window, nil)
        shown[window] = false
    end
end

local function ram(p) return p and p >= 0x8C010000 and p < 0x8D000000 end

local function decode(raw)
    return (text.sjis(raw):gsub("[\xE2-\xE3][\x80-\xBF][\x80-\xBF]", OWN))
end

-- The characters of a Shift-JIS string, one or two bytes each.
local function each(raw)
    local out, i = {}, 1
    while i <= #raw do
        local b = raw:byte(i)
        local n = ((b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)) and 2 or 1
        out[#out + 1] = raw:sub(i, i + n - 1)
        i = i + n
    end
    return out
end

local function message()
    local w = mem.u32(WINDOW)
    if not ram(w) then withdraw("message"); last_start = nil; return end
    local start, pos = mem.u32(w + START), mem.u32(w + POS)
    if not ram(start) or not ram(pos) or pos < start or pos - start > 1024 then
        withdraw("message"); last_start = nil
        return
    end
    if mem.u32(w + BUSY) ~= 0 then
        withdraw("message"); fresh = true
        return
    end
    local raw = mem.read(start, pos - start)
    if not raw or #raw == 0 or mem.u8(pos) ~= 0 then withdraw("message"); return end
    if start == last_start and raw == last_text and not fresh then return end
    last_start, last_text, fresh = start, raw, false
    local chars, cells = {}, {}
    local row, x = 0, LEFT
    for _, line in ipairs((function()
        local lines, at = {}, 1
        while true do
            local cut = raw:find("$$", at, true)
            lines[#lines + 1] = raw:sub(at, (cut or #raw + 1) - 1)
            if not cut then return lines end
            at = cut + 2
        end
    end)()) do
        if #chars > 0 then
            chars[#chars + 1] = "\n"; cells[#cells + 1] = false
            row, x = row + 1, LEFT
        end
        for _, c in ipairs(each(line)) do
            local wide = #c == 2 and CELL or CELL / 2
            chars[#chars + 1] = c
            cells[#cells + 1] = { x, TOP + row * PITCH, wide, HEIGHT }
            x = x + wide
        end
    end
    if #chars == 0 then return end
    reader.emit{ text = decode(table.concat(chars)), speaker = "", window = "message",
                 id = text.hash(raw) .. ":" .. start }
    reader.layout("message", { space = { 640, 480 }, box = { 16, 320, 608, 128 }, glyphs = cells })
    shown.message = true
end

local function choice()
    local count = mem.u32(CHOICE + CHOICE_COUNT)
    local object = mem.u32(CHOICE + CHOICE_OBJECT)
    if not ram(object) or not count or count < 1 or count > 5 then
        withdraw("choice"); last_choice = nil
        reader.cursor(nil)
        return
    end
    -- Which answer the cursor is on: what a tap on an answer is turned into presses with.
    local at = mem.u32(object + CURSOR)
    if at and at < count then reader.cursor(at) else reader.cursor(nil) end
    local answers, key = {}, {}
    for n = 0, count - 1 do
        local raw = mem.read(CHOICE + CHOICE_TEXT + n * CHOICE_STEP, CHOICE_STEP)
        raw = raw and raw:match("^[^%z]*") or ""
        answers[#answers + 1] = raw
        key[#key + 1] = raw
    end
    key = table.concat(key, "\n")
    if key == last_choice then return end
    last_choice = key
    local chars, cells = {}, {}
    for n, raw in ipairs(answers) do
        if n > 1 then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
        local list = each(raw)
        local units = 0
        for _, c in ipairs(list) do units = units + (#c == 2 and 1 or 0.5) end
        local cell = math.min(CHOICE_CELL, CHOICE_WIDTH / math.max(units, 1))
        local x = CHOICE_CENTRE - units * cell / 2
        local y = CHOICE_MIDDLE + (n - 1 - (count - 1) / 2) * CHOICE_SPAN / (count + 1) - CHOICE_HEIGHT / 2
        for _, c in ipairs(list) do
            local wide = #c == 2 and cell or cell / 2
            chars[#chars + 1] = c
            cells[#cells + 1] = { x, y, wide, CHOICE_HEIGHT }
            x = x + wide
        end
    end
    reader.emit{ text = decode(table.concat(chars)), speaker = "", window = "choice",
                 channel = "choice", id = "choice:" .. text.hash(key) }
    reader.layout("choice", { space = { 640, 480 }, box = { 64, 100, 512, 180 }, glyphs = cells })
    shown.choice = true
end

reader.on_frame(function(frame)
    message()
    choice()
end)

reader.on_state_loaded(function()
    withdraw("message"); withdraw("choice")
    reader.cursor(nil)
    last_start, fresh, last_choice = nil, true, nil
end)
