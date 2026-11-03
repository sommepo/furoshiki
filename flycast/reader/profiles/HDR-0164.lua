-- reader: no hooks
-- reader: title シェンムーII (Shenmue II)
-- reader: reads The subtitles: what is said in cutscenes and in conversations
-- reader: version HDR-0164 (Japan), seen on disc 1 of 4
-- Shenmue II (Sega, Dreamcast, 2001), Japanese version. docs/reader/games/shenmue2.md has
-- what is read, what is not, and how it was checked.
--
-- The game keeps the subtitle it is showing in the data of its scene task: a static pointer
-- of the program leads to that task's data, which begins with the tag "SCEN"; the line is
-- there as EUC-JP text, and its first byte is zero while no subtitle is up. The four discs
-- have one product number and a build ID each; the pointer is at the same place on all four,
-- and the tag says whether it leads to a scene (at a title screen it leads elsewhere, and
-- nothing is reported).

local SCENE_POINTER = 0x8C278B20  -- pointer to the scene task's data
local SCENE_TAG = 0x4E454353      -- "SCEN"
local TEXT = 0x84                 -- the subtitle being shown, EUC-JP, zero-terminated
local TEXT_END = 0x54             -- pointer to the end of that text

-- Where the game draws a subtitle, in the 640x480 picture: centred, 24 across for a
-- full-width character.
local CENTRE, TOP, CELL, HEIGHT = 320, 420, 24, 28

local shown = nil     -- the raw bytes of the line that is up
local since = 0

local function scene()
    local p = mem.u32(SCENE_POINTER)
    if not p or p < 0x8C010000 or p >= 0x8D000000 then return nil end
    if mem.u32(p) ~= SCENE_TAG then return nil end
    return p
end

local function withdraw()
    if shown then
        reader.layout("subtitle", nil)
        shown = nil
    end
end

reader.on_frame(function(frame)
    local p = scene()
    local raw = p and mem.cstr(p + TEXT, 256) or ""
    if raw == "" then
        withdraw()
        return
    end
    if raw == shown then return end
    -- A new line: reported once.
    withdraw()
    shown = raw
    local line = text.eucjp(raw)
    local cells, widths, total = {}, {}, 0
    for _, code in utf8.codes(line) do
        local w = code < 0x80 and CELL / 2 or CELL
        widths[#widths + 1] = w
        total = total + w
    end
    local x = CENTRE - total / 2
    for i, w in ipairs(widths) do
        cells[i] = { x, TOP, w, HEIGHT }
        x = x + w
    end
    reader.emit{ text = line, speaker = "", window = "subtitle", id = text.hash(raw) }
    reader.layout("subtitle", { space = { 640, 480 }, glyphs = cells })
end)

reader.on_state_loaded(function()
    withdraw()
end)
