-- reader: no hooks
-- reader: title 新世紀エヴァンゲリオン 綾波育成計画 (Ayanami Raising Project)
-- reader: reads The message window: what is said and told
-- reader: version T46303M (Japan)
-- Shin Seiki Evangelion: Ayanami Ikusei Keikaku (Gainax/Broccoli, Dreamcast, 2002).
-- docs/reader/games/ayanami-ikusei-keikaku.md has what is read, what is not, and how it was
-- checked.
--
-- The message window's text is an object on the game's heap: a Shift-JIS string with line
-- feeds where the game broke it into rows, after a mark of sixteen bytes (four of 0xFF, the
-- text's inset in the window as two floats, a zero word), with the place it is drawn at and
-- a flag that is set once all of it is typed before that, and one for the window being up. Nothing in the program's fixed
-- data points at the object, so the profile looks through the heap for the mark.

local BUILD = "98FD76CA"
if reader.build_id ~= BUILD then
    error("this profile is for Ayanami Ikusei Keikaku T46303M (" .. BUILD .. "), the disc is " .. tostring(reader.build_id))
end

local HEAP = 0x8C2C4FD0       -- pointer to the heap's start
local SPAN = 0x40000          -- how much of it is looked through
local MARK = "\xFF\xFF\xFF\xFF\x00\x00\x60\x41\x00\x00\x40\x41\x00\x00\x00\x00"
local X, Y, UP, TYPED = -0x48, -0x44, -0x50, -0x54
local SEARCH_EVERY = 30       -- frames between two searches while the object is not known

local CELL, PITCH, HEIGHT = 24, 26, 24

local object = nil            -- the text's address
local shown, last = false, nil
local searched = -SEARCH_EVERY

local function withdraw()
    if shown then reader.layout("message", nil); shown = false end
end

local function ram(p) return p and p >= 0x8C010000 and p < 0x8D000000 end

local function find(frame)
    if frame - searched < SEARCH_EVERY then return nil end
    searched = frame
    local heap = mem.u32(HEAP)
    if not ram(heap) then return nil end
    local span = math.min(SPAN, 0x8D000000 - heap)
    local raw = mem.read(heap, span)
    if not raw then return nil end
    local at = raw:find(MARK, 1, true)
    return at and heap + at - 1 + #MARK
end

reader.on_frame(function(frame)
    if object and mem.read(object - #MARK, #MARK) ~= MARK then object = nil end
    if not object then
        object = find(frame)
        if not object then withdraw(); last = nil; return end
    end
    local raw = mem.cstr(object, 512)
    local x0, y0 = mem.f32(object + X), mem.f32(object + Y)
    if not raw or raw == "" or not x0 or x0 < 0 or x0 >= 640 or y0 < 0 or y0 >= 480 then
        withdraw(); last = nil
        return
    end
    -- Not while the window is away (a change of scene), and not while the text is typed.
    if mem.u32(object + UP) ~= 1 then withdraw(); last = nil; return end
    if mem.u32(object + TYPED) ~= 1 then withdraw(); return end
    local key = raw .. ":" .. x0 .. ":" .. y0
    if key == last then return end
    last = key
    local chars, cells = {}, {}
    local x, y, i = x0, y0, 1
    while i <= #raw do
        local b = raw:byte(i)
        if b == 0x0A then
            chars[#chars + 1] = "\n"; cells[#cells + 1] = false
            x, y, i = x0, y + PITCH, i + 1
        else
            local n = ((b >= 0x81 and b <= 0x9f) or (b >= 0xe0 and b <= 0xfc)) and 2 or 1
            local wide = n == 2 and CELL or CELL / 2
            chars[#chars + 1] = raw:sub(i, i + n - 1)
            cells[#cells + 1] = { x, y, wide, HEIGHT }
            x, i = x + wide, i + n
        end
    end
    while chars[#chars] == "\n" do chars[#chars], cells[#cells] = nil, nil end
    if #chars == 0 then withdraw(); return end
    reader.emit{ text = text.sjis(table.concat(chars)), speaker = "", window = "message", id = text.hash(key) .. ":" .. frame }
    reader.layout("message", { space = { 640, 480 }, box = { x0 - 14, y0 - 12, 384, 136 }, glyphs = cells })
    shown = true
end)

reader.on_state_loaded(function() withdraw(); last, object = nil, nil; searched = -SEARCH_EVERY end)
