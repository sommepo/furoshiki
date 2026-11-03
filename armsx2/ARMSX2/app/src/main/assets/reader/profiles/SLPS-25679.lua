-- reader: no hooks
-- reader: title うたわれるもの 散りゆく者への子守唄
-- reader: reads The message window: what is said and told, with the speaker's name
-- reader: version SLPS-25679, disc version 1.04
-- うたわれるもの 散りゆく者への子守唄 (Aquaplus, PS2, 2006), SLPS-25679. Program BF9554BE.
--
-- Plain Shift-JIS; readings are written [word*reading]. Each message is laid out once into a
-- list of 24-byte entries: Shift-JIS code, x and y in the box, width; a code of 0 ends it.
-- The speaker's name is a separate string (empty when nobody is named). The box is at a fixed
-- place in a 640x448 picture (measured).

local BUILD = "BF9554BE"
if reader.build_id ~= BUILD then
    error("this profile is for うたわれるもの SLPS-25679 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local LIST = 0x00505744      -- pointer to the list of characters
local SOURCE = 0x0050574c    -- the message as the script has it, readings and all
local SPEAKER = 0x00506200   -- the speaker's name, Shift-JIS
local ENTRY, MAX_ENTRIES = 24, 300
-- A row's y is measured from TOP (a row with readings is lower by their height).
local LEFT, TOP, HEIGHT = 90, 340, 22
-- Readings in the message source: { text before it, base, reading } for each [base*reading],
-- and the text without them; nil when the source has other bracketed text.
local function readings()
    local raw = mem.cstr(SOURCE, 0x400)
    if not raw then return nil end
    local plain, out = {}, {}
    local at = 1
    while at <= #raw do
        local b = raw:byte(at)
        if b == 0x5b then   -- [
            local close = raw:find("]", at + 1, true)
            local star = raw:find("*", at + 1, true)
            if not close or not star or star > close then return nil end
            local base = text.sjis(raw:sub(at + 1, star - 1))
            out[#out + 1] = { before = table.concat(plain), base = base,
                              reading = text.sjis(raw:sub(star + 1, close - 1)) }
            plain[#plain + 1] = base
            at = close + 1
        elseif (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc) then
            plain[#plain + 1] = text.sjis(raw:sub(at, at + 1))
            at = at + 2
        else
            plain[#plain + 1] = text.sjis(raw:sub(at, at))
            at = at + 1
        end
    end
    return out, table.concat(plain)
end

-- A message is reported when its list has stayed the same for this many frames.
local STEADY = 4

-- The characters of the list: { text, x, y, w }.
local function characters()
    local list = mem.u32(LIST)
    if not list or list < 0x100000 or list >= 0x2000000 then return nil end
    local out = {}
    for k = 0, MAX_ENTRIES - 1 do
        local raw = mem.read(list + k * ENTRY, ENTRY)
        if not raw or #raw < ENTRY then return nil end
        local code, _, x, y, _, w = string.unpack("<I4i4i4i4i4i4", raw)
        if code == 0 then break end
        if code == 0x875b then code = 0x815c end   -- the game's long dash: ―
        local bytes = code > 0xff and string.char(code >> 8 & 0xff, code & 0xff) or string.char(code & 0xff)
        out[#out + 1] = { text = text.sjis(bytes), x = x, y = y, w = w }
    end
    return out
end

local last_key, steady, reported_key
local serial = 0
local shown = false

local function drop()
    if shown then reader.layout("message", nil) end
    shown = false
end

reader.on_state_loaded(function()
    drop()
    last_key, steady, reported_key = nil, 0, nil
end)

reader.on_frame(function()
    local chars = characters()
    if not chars or #chars == 0 then
        drop()
        last_key, steady = nil, 0
        return
    end
    local parts = {}
    for k, c in ipairs(chars) do parts[k] = c.text .. "@" .. c.x .. "," .. c.y end
    local key = table.concat(parts, " ")
    if key ~= last_key then
        last_key, steady = key, 0
        return
    end
    steady = steady + 1
    if steady < STEADY or key == reported_key then return end
    reported_key = key

    -- Which character each reading belongs to, by the characters before it.
    local groups, plain = readings()
    local all = {}
    for k, c in ipairs(chars) do all[k] = c.text end
    local starts = {}
    if groups and plain == table.concat(all) then
        for _, g in ipairs(groups) do
            starts[text.len(g.before) + 1] = g
        end
    end

    -- A row's leading full-width space is the game's indent, not the text's. Rows break where
    -- the box is full, so they are joined as they are.
    local b, cells = reader.builder(), {}
    local row_y, leading
    local k = 1
    while k <= #chars do
        local c = chars[k]
        if c.y ~= row_y then row_y, leading = c.y, true end
        local g = starts[k]
        if leading and c.text == "　" and not g then
            k = k + 1
        else
            leading = false
            local length = g and text.len(g.base) or 1
            if g then
                b:add_ruby(g.base, g.reading)
            end
            for j = k, math.min(#chars, k + length - 1) do
                if not g then b:add(chars[j].text) end
                cells[#cells + 1] = { LEFT + chars[j].x, TOP + chars[j].y, chars[j].w, HEIGHT }
            end
            k = k + length
        end
    end
    local whole = b:text()
    if whole:gsub("[%s　]", "") == "" then return end
    local name = mem.cstr(SPEAKER, 64)
    local speaker = name and name ~= "" and text.sjis(name) or nil
    serial = serial + 1
    reader.emit{ text = whole, speaker = speaker, window = "message",
                 id = text.hash(key) .. ":" .. serial }
    reader.layout("message", { space = { 640, 448 }, glyphs = cells })
    shown = true
end)
