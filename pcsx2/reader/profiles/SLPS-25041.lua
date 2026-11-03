-- reader: no hooks
-- reader: title シャドウハーツ
-- reader: reads The message windows of scenes and talk, with the speaker's name
-- reader: version SLPS-25041, disc version 1.02
-- シャドウハーツ (Aruze / Sacnoth, PS2, 2001), SLPS-25041. Program 3E46A5F0.
--
-- Shift-JIS with three-byte commands starting with % (%t0, %p1, %N0). Each window is an object
-- held by a window keeper; keepers form a chain from a fixed pointer. Each row of a message is
-- its own window, linked to the next, with its text, its y offset, a typing state and a list of
-- character positions. The keeper holds the window's place in a 640x448 picture. A speaker's
-- name is a window whose text starts with %t.
--
-- Battles, menus and shops use another kind of keeper and are not read.

local BUILD = "3E46A5F0"
if reader.build_id ~= BUILD then
    error("this profile is for シャドウハーツ SLPS-25041 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local ROOT = 0x004d95f8          -- pointer; at +4 the first window keeper
local KEEPER_KIND = 0x004d72c0   -- a keeper of message windows starts with this
local K_NEXT, K_X, K_Y, K_WINDOW = 0x04, 0x18, 0x1c, 0x38
local W_NEXT_ROW, W_TEXT, W_ROW_Y, W_STATE, W_LIST, W_COUNT = 0x00, 0x08, 0x14, 0x80, 0x84, 0x90
local MAX_KEEPERS, MAX_ROWS, MAX_CHARS = 16, 8, 400
-- Row state low byte: 0x81 while typing, 0x84 or 0xc4 when done. The next row starts two
-- frames after one is done, so a message is reported when every row is done and nothing has
-- changed for STEADY frames.
local STEADY = 10

local function valid(p) return p and p >= 0x100000 and p < 0x2000000 end

-- The characters of a window's text that are shown: commands left out.
local function shown_text(raw)
    local out, at = {}, 1
    while at <= #raw do
        local b = raw:byte(at)
        if b == 0x25 then           -- %: a command of three bytes
            at = at + 3
        elseif (b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc) then
            out[#out + 1] = text.sjis(raw:sub(at, at + 1))
            at = at + 2
        elseif b == 0x0a or b == 0x0d then
            at = at + 1
        else
            out[#out + 1] = text.sjis(raw:sub(at, at))
            at = at + 1
        end
    end
    return out
end

-- The message windows up: { raw, chars, cells, name } each, in the keepers' order.
local function windows()
    local root = mem.u32(ROOT)
    if not valid(root) then return {} end
    local keeper = mem.u32(root + K_NEXT)
    local out = {}
    for _ = 1, MAX_KEEPERS do
        if not valid(keeper) or mem.u32(keeper) ~= KEEPER_KIND then break end
        local window = mem.u32(keeper + K_WINDOW)
        local x0, y0 = mem.f32(keeper + K_X), mem.f32(keeper + K_Y)
        local raws, chars, cells, first_raw, typing = {}, {}, {}, nil, false
        for _ = 1, MAX_ROWS do
            if not valid(window) then break end
            local raw = mem.cstr(mem.u32(window + W_TEXT) or 0, 0x400)
            local count = mem.u32(window + W_COUNT)
            local list = mem.u32(window + W_LIST)
            local dy = mem.f32(window + W_ROW_Y) or 0
            if not raw or raw == "" or not count or count > MAX_CHARS then break end
            first_raw = first_raw or raw
            raws[#raws + 1] = raw
            if (mem.u32(window + W_STATE) or 0) & 0x0f ~= 4 then typing = true end
            local row = shown_text(raw)
            local entries = count > 0 and valid(list) and mem.read(list, count * 8)
            for k, c in ipairs(row) do
                chars[#chars + 1] = c
                if cells and #row == count and entries and #entries == count * 8 and x0 and y0 then
                    local x, y = string.unpack("<I2I2", entries, (k - 1) * 8 + 3)
                    cells[#chars] = { x0 + x + 2, y0 + dy + y + 3, 25, 24 }
                else
                    cells = nil
                end
            end
            window = mem.u32(window + W_NEXT_ROW)
        end
        if first_raw and #chars > 0 then
            out[#out + 1] = { raw = table.concat(raws, "\n"), chars = chars, cells = cells,
                              name = first_raw:sub(1, 2) == "%t", typing = typing }
        end
        keeper = mem.u32(keeper + K_NEXT)
    end
    return out
end

local last_key, steady = nil, 0
local reported              -- the message reported last: { key, count, speaker }
local serial = 0
local layouts = {}

local function drop()
    for name in pairs(layouts) do reader.layout(name, nil) end
    layouts = {}
end

reader.on_state_loaded(function()
    drop()
    last_key, steady, reported = nil, 0, nil
end)

reader.on_frame(function()
    local list = windows()
    local message, speaker
    for _, w in ipairs(list) do
        if w.name then
            speaker = speaker or table.concat(w.chars)
        elseif not message then
            message = w
        end
    end
    if not message then
        drop()
        last_key, steady, reported = nil, 0, nil
        return
    end
    local key = message.raw .. "\0" .. (speaker or "")
    if key ~= last_key or message.typing then
        last_key, steady = key, 0
        return
    end
    steady = steady + 1
    if steady < STEADY or (reported and reported.key == key) then return end

    -- A message continued after a pause: only the new part is reported.
    local first = 1
    if reported and reported.speaker == speaker and reported.count < #message.chars
            and key:sub(1, #reported.raw) == reported.raw then
        first = reported.count + 1
    else
        drop()
    end
    reported = { key = key, raw = message.raw, count = #message.chars, speaker = speaker }

    local b, glyphs = reader.builder(), {}
    for k = first, #message.chars do
        b:add(message.chars[k])
        glyphs[#glyphs + 1] = message.cells and message.cells[k] or false
    end
    local whole = b:text()
    if whole:gsub("[%s　]", "") == "" then return end
    serial = serial + 1
    local name = "message" .. (serial % 8)
    reader.emit{ text = whole, speaker = speaker ~= "" and speaker or nil, window = name,
                 id = text.hash(key) .. ":" .. serial }
    if message.cells then
        reader.layout(name, { space = { 640, 448 }, glyphs = glyphs })
        layouts[name] = true
    end
end)
