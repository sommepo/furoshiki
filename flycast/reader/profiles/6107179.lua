-- reader: no hooks
-- reader: title What's シェンムー 〜湯川(元)専務をさがせ〜 (What's Shenmue)
-- reader: reads The subtitles: what is said in cutscenes and in conversations
-- reader: version 6107179 (Japan)
-- What's Shenmue (Sega, Dreamcast, 1999). docs/reader/games/whats-shenmue.md has what is
-- read, what is not, and how it was checked.
--
-- As in Shenmue II, the subtitle being shown is in the data of the scene task, which a
-- pointer of the program leads to and which begins with the tag "SCEN": EUC-JP text, with a
-- full-width ampersand where the second row begins, and a pointer to the text's end that is
-- zero while no subtitle is up. The game shows subtitles only when its settings say so
-- (声と字幕: ゲームモード or テキストモード).

local BUILD = "CB91066F"
if reader.build_id ~= BUILD then
    error("this profile is for What's Shenmue 6107179 (" .. BUILD .. "), the disc is " .. tostring(reader.build_id))
end

local SCENE_POINTER = 0x8C1B277C  -- pointer to the scene task's data
local SCENE_TAG = 0x4E454353      -- "SCEN"
local TEXT = 0x80                 -- the subtitle being shown, EUC-JP, zero-terminated
local TEXT_END = 0x58             -- pointer to the end of that text; 0 while none is up
local ROW = "\xA1\xF5"            -- ＆: the second row begins

-- Where the game draws a subtitle, in the 640x480 picture: the rows begin at the same place
-- on the left (further left when a row is longer than fourteen characters), 25 across for a
-- full-width character, the last row at the bottom.
local LEFT, LONG_LEFT, LONG, BOTTOM, CELL, PITCH, HEIGHT = 130, 82, 14, 413, 25, 30, 30

local shown = nil

local function address(p)
    if not p or p == 0 then return nil end
    p = (p & 0x1FFFFFFF) | 0x80000000
    if p < 0x8C010000 or p >= 0x8D000000 then return nil end
    return p
end

local function withdraw()
    if shown then
        reader.layout("subtitle", nil)
        shown = nil
    end
end

reader.on_frame(function(frame)
    local p = address(mem.u32(SCENE_POINTER))
    if not p or mem.u32(p) ~= SCENE_TAG then withdraw(); return end
    local stop = address(mem.u32(p + TEXT_END))
    if not stop or stop <= p + TEXT or stop > p + TEXT + 256 then withdraw(); return end
    local raw = mem.read(p + TEXT, stop - p - TEXT)
    raw = raw and raw:match("^[^%z]*") or ""
    if raw == "" then withdraw(); return end
    if raw == shown then return end
    withdraw()
    shown = raw
    local rows, at = {}, 1
    while true do
        local cut = raw:find(ROW, at, true)
        -- An ampersand's bytes can also be the end of one character and the start of the
        -- next: only one that begins on a character's first byte counts.
        while cut and (cut - at) % 2 == 1 do cut = raw:find(ROW, cut + 1, true) end
        rows[#rows + 1] = raw:sub(at, (cut or #raw + 1) - 1)
        if not cut then break end
        at = cut + 2
    end
    local lines, longest = {}, 0
    for n, row in ipairs(rows) do
        lines[n] = text.eucjp(row)
        local units = 0
        for _, code in utf8.codes(lines[n]) do units = units + (code < 0x80 and 0.5 or 1) end
        longest = math.max(longest, units)
    end
    local chars, cells = {}, {}
    for n, line in ipairs(lines) do
        if n > 1 then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
        local x = longest > LONG and LONG_LEFT or LEFT
        local y = BOTTOM - (#lines - n) * PITCH
        for _, code in utf8.codes(line) do
            local w = code < 0x80 and CELL / 2 or CELL
            chars[#chars + 1] = utf8.char(code)
            cells[#cells + 1] = { x, y, w, HEIGHT }
            x = x + w
        end
    end
    reader.emit{ text = table.concat(chars), speaker = "", window = "subtitle", id = text.hash(raw) }
    reader.layout("subtitle", { space = { 640, 480 }, glyphs = cells })
end)

reader.on_state_loaded(function()
    withdraw()
end)
