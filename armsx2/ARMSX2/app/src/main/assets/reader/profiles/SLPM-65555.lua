-- reader: no hooks
-- reader: title ドラゴンクエストV 天空の花嫁
-- reader: reads The message window: what is said and told, a speaker's lines as they have come up in full
-- reader: version SLPM-65555
-- ドラゴンクエストV 天空の花嫁 (Square Enix, PS2, 2004), SLPM-65555. Program B64E9939.
--
-- The message window holds its rows as one Shift-JIS text (line feeds between rows, names and
-- numbers filled in, all full-width). Beside it: the position in the message source (0 while
-- no window is up), the character count, how many are typed (a float), and a byte set while
-- rows scroll. When the message continues, rows scroll up and new ones are typed below; the
-- new rows are reported once complete.
--
-- There is no position table. Characters are laid out from the window's left edge using the
-- font's width table (code, width, glyph) in a 512-wide picture, as the game does.

local BUILD = "B64E9939"
if reader.build_id ~= BUILD then
    error("this profile is for Dragon Quest V SLPM-65555 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local STATE = 0x00745a60
local S_SOURCE, S_MOVING, S_COUNT, S_TYPED, S_TEXT = 0x04, 0x09, 0x14, 0x18, 0x1c
local TEXT_MAX = 0x200
-- The font: "FONT", and at +0x1c its table (8 bytes a character: code u16, width u8, a mark,
-- the picture's address) and at +0x20 how many.
local FONT_POINTER, FONT_DEFAULT = 0x007056cc, 0x00b50000
local F_TABLE, F_COUNT = 0x1c, 0x20
-- Where the window's first row begins and how far the rows are apart, in a picture of 512 by
-- 448 (measured in pictures of the game).
local LEFT, TOP, PITCH, HEIGHT = 32, 317, 31.2, 30
local DEFAULT_WIDTH, SPACE_WIDTH = 24, 15.5

local widths

local function load_widths()
    local font = mem.u32(FONT_POINTER)
    if not font or mem.read(font, 4) ~= "FONT" then font = FONT_DEFAULT end
    if mem.read(font, 4) ~= "FONT" then return nil end
    local tab, count = mem.u32(font + F_TABLE), mem.u32(font + F_COUNT)
    if not tab or not count or count == 0 or count > 8000 then return nil end
    local raw = mem.read(tab, count * 8)
    if not raw or #raw < count * 8 then return nil end
    local out = {}
    for k = 0, count - 1 do
        local code, width = string.unpack("<I2B", raw, k * 8 + 1)
        out[code] = width
    end
    -- The table is valid when it has the full-width space. The game draws the space narrower
    -- than the table says (measured).
    if not out[0x8140] then return nil end
    out[0x8140] = SPACE_WIDTH
    return out
end

-- A row of Shift-JIS as characters: { text, width }.
local function characters(row)
    local out, at = {}, 1
    while at <= #row do
        local b = row:byte(at)
        local code, size = b, 1
        if (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc) then
            if at == #row then break end
            code, size = b << 8 | row:byte(at + 1), 2
        end
        out[#out + 1] = { text = text.sjis(row:sub(at, at + size - 1)),
                          width = widths and widths[code] or DEFAULT_WIDTH }
        at = at + size
    end
    return out
end

local SPACE = "　"
local shown = {}       -- the rows of the window when it was last read in full
local parts = {}       -- what is reported of the rows on the screen: { first, last, name }
local serial = 0
local layouts = {}

local function drop_all()
    for name in pairs(layouts) do reader.layout(name, nil) end
    layouts = {}
end

-- Text and cells of rows first..last of `rows`. Leading spaces of later rows are dropped;
-- rows are joined with a full-width space.
local function compose(rows, first, last)
    local b, cells, speaker = reader.builder(), {}, nil
    for index = first, last do
        local row = rows[index]
        local x, y = LEFT, TOP + (index - 1) * PITCH
        local from = 1
        if index == first then
            -- A name before the opening bracket is the speaker (＊ is somebody unnamed).
            for k = 1, math.min(#row, 12) do
                if row[k].text == "「" then
                    local name = {}
                    for j = 1, k - 1 do name[#name + 1] = row[j].text end
                    name = table.concat(name)
                    if name ~= "" and name ~= "＊" and not name:find(SPACE, 1, true) then speaker = name end
                    if speaker or name == "＊" then
                        for j = 1, k - 1 do x = x + row[j].width end
                        from = k
                    end
                    break
                elseif row[k].text == SPACE then
                    break
                end
            end
        else
            b:add(SPACE)
            cells[#cells + 1] = false
        end
        local leading = index > first
        for k = from, #row do
            local c = row[k]
            if leading and c.text == SPACE then
                x = x + c.width
            else
                leading = false
                b:add(c.text)
                cells[#cells + 1] = { x, y, c.width, HEIGHT }
                x = x + c.width
            end
        end
    end
    return b:text(), cells, speaker
end

-- Whether a row begins somebody's lines: a name, or ＊, and the opening bracket.
local function begins_speech(row)
    for k = 1, math.min(#row, 12) do
        if row[k].text == "「" then return true end
        if row[k].text == SPACE then return false end
    end
    return false
end

local function same(a, b)
    if #a ~= #b then return false end
    for k = 1, #a do if a[k].text ~= b[k].text then return false end end
    return true
end

reader.on_state_loaded(function()
    drop_all()
    shown, parts = {}, {}
end)

reader.on_frame(function()
    local head = mem.read(STATE, S_TEXT)
    if not head or #head < S_TEXT then return end
    local source = string.unpack("<I4", head, S_SOURCE + 1)
    if source == 0 then
        if next(layouts) then drop_all() end
        shown, parts = {}, {}
        return
    end
    local moving = head:byte(S_MOVING + 1)
    local count = string.unpack("<I4", head, S_COUNT + 1)
    local typed = string.unpack("<f", head, S_TYPED + 1)
    if moving ~= 0 or typed ~= typed or typed < count or count == 0 then return end
    local raw = mem.cstr(STATE + S_TEXT, TEXT_MAX)
    if not raw or raw == "" then return end
    widths = widths or load_widths()

    local rows = {}
    for line in (raw .. "\n"):gmatch("([^\n]*)\n") do rows[#rows + 1] = characters(line) end
    while #rows > 0 and #rows[#rows] == 0 do rows[#rows] = nil end
    if #rows == 0 then return end
    local unchanged = #rows == #shown
    for k = 1, #rows do unchanged = unchanged and same(rows[k], shown[k]) end
    if unchanged then return end

    -- Rows kept after scrolling: the old last rows that are now the first.
    local kept = 0
    for k = math.min(#shown, #rows - 1), 1, -1 do
        local fits = true
        for j = 1, k do
            if not same(shown[#shown - k + j], rows[j]) then fits = false break end
        end
        if fits then kept = k break end
    end
    local moved = #shown - kept
    drop_all()
    local still = {}
    for _, part in ipairs(parts) do
        part.first, part.last = part.first - moved, part.last - moved
        if part.last >= 1 and part.first >= 1 and kept > 0 then
            still[#still + 1] = part
        end
    end
    parts = still

    -- New rows, one part per speaker.
    local first = kept + 1
    while first <= #rows do
        local last = first
        while last < #rows and not begins_speech(rows[last + 1]) do last = last + 1 end
        serial = serial + 1
        local part = { first = first, last = last, name = "message" .. (serial % 8) }
        local whole, _, speaker = compose(rows, first, last)
        if #whole > 0 then
            reader.emit{ text = whole, speaker = speaker, window = part.name,
                         id = text.hash(whole) .. ":" .. string.format("%x", source) .. ":" .. serial }
            parts[#parts + 1] = part
        end
        first = last + 1
    end
    for _, part in ipairs(parts) do
        local _, cells = compose(rows, part.first, part.last)
        reader.layout(part.name, { space = { 512, 448 }, glyphs = cells })
        layouts[part.name] = true
    end
    shown = rows
end)
