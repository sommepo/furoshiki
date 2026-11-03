-- reader: title July
-- reader: reads The message window, with who speaks, and the choices
-- reader: version T35401M (header 481991C9)
-- reader: no hooks
--
-- July (Fortyfive, 1998), the Japanese disc. The message being shown is a string of the
-- game's own at a fixed place in memory: Shift-JIS text, two tabs before each line, and codes
-- that begin with % (%N a new line, %< and %> the text of a thought or a choice in the middle
-- of the picture, %Q2 a choice of two answers, others for colours, waits and places). The
-- window's object says where the typing has got to and points at the speaker's name. See
-- docs/reader/games/july.md.

local BUILD = "481991C9"
if reader.build_id ~= BUILD then
    error("this profile is for the disc whose header is " .. BUILD .. ", this one is " .. tostring(reader.build_id))
end

local MESSAGE = 0x8C226D60 -- the message shown: a C string
local TYPED = 0x8C0BFD24   -- u32: where the typing is in it (0 while it is not typed out)
local SPEAKER_NO = 0x8C0BFD54 -- u32: who speaks (0: nobody's name is shown)
local SPEAKER = 0x8C0BFD58 -- u32: their name, a C string

-- Where the lines go, in the 640x480 picture: the window's, and those in the middle of the
-- picture (a thought, a choice). Characters are 28 pixels apart, half as much for a narrow one.
local WINDOW = { x = 66, y = 343, line = 31 }
local MIDDLE = { x = 240, y = 80, line = 32 }
local WIDE, HEIGHT = 28, 26

-- The message as lines of characters, without the codes: { lines = {{chars}...}, middle,
-- answers = n (a choice), question = the lines before the answers }.
local function parse(raw)
    local result = { lines = { {} }, middle = false, answers = 0, first_answer = nil }
    local i, n = 1, #raw
    while i <= n do
        local b = raw:byte(i)
        if b == 0x25 then -- %
            local c = raw:sub(i + 1, i + 1)
            local j = i + 2
            while j <= n and raw:sub(j, j):match("[%+%-%d]") do j = j + 1 end
            if c == "N" then
                result.lines[#result.lines + 1] = {}
            elseif c == "<" then
                result.middle = true
            elseif c == ">" then
                result.first_answer = #result.lines
            elseif c == "Q" then
                result.answers = tonumber(raw:sub(i + 2, j - 1)) or 0
            end
            i = j
        elseif b == 0x09 or b == 0x0D or b == 0x0A then
            i = i + 1
        elseif (b >= 0x81 and b <= 0x9F) or (b >= 0xE0 and b <= 0xFC) then
            local line = result.lines[#result.lines]
            line[#line + 1] = { text.sjis(raw:sub(i, i + 1)), WIDE }
            i = i + 2
        else
            local line = result.lines[#result.lines]
            line[#line + 1] = { text.sjis(raw:sub(i, i)), WIDE / 2 }
            i = i + 1
        end
    end
    return result
end

-- Text and cells of lines from..to (blank lines left out of the text).
local function piece(parsed, from, to, place)
    local chars, cells = {}, {}
    for l = from, to do
        local line = parsed.lines[l]
        if #line > 0 then
            if #chars > 0 then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
            local x, y = place.x, place.y + (l - 1) * place.line
            for _, ch in ipairs(line) do
                chars[#chars + 1] = ch[1]
                cells[#cells + 1] = { x, y, ch[2], HEIGHT }
                x = x + ch[2]
            end
        end
    end
    return table.concat(chars), cells
end

local said = nil
local shown = { message = false, choice = false }
local still = 0
local last_raw = nil

local function withdraw()
    if shown.message then reader.layout("message", nil) end
    if shown.choice then reader.layout("choice", nil) end
    shown.message, shown.choice = false, false
end

reader.on_frame(function()
    local raw = mem.cstr(MESSAGE, 600) or ""
    if raw ~= last_raw then last_raw, still = raw, 0 else still = still + 1 end
    if raw == "" then
        if said then withdraw(); said = nil end
        return
    end
    if raw == said then return end
    -- Typed out: the typing has reached the end. A message that is not typed (a choice) is
    -- taken when it has stood still a moment.
    local typed = mem.u32(TYPED) or 0
    if typed ~= 0 then
        if typed < MESSAGE or typed > MESSAGE + #raw or mem.u8(typed) ~= 0 then return end
    elseif still < 20 then
        return
    end
    said = raw
    withdraw()
    local parsed = parse(raw)
    local place = parsed.middle and MIDDLE or WINDOW
    local speaker = ""
    if (mem.u32(SPEAKER_NO) or 0) ~= 0 and not parsed.middle then
        local name = mem.u32(SPEAKER)
        speaker = name and text.sjis(mem.cstr(name, 32) or "") or ""
    end
    local last_question = parsed.first_answer and parsed.first_answer - 1 or #parsed.lines
    local body, cells = piece(parsed, 1, last_question, place)
    if body ~= "" then
        reader.emit{ text = body, speaker = speaker, window = "message", id = text.hash(raw) }
        reader.layout("message", { space = { 640, 480 }, glyphs = cells })
        shown.message = true
    end
    if parsed.answers > 0 and parsed.first_answer then
        local answers, acells = piece(parsed, parsed.first_answer, #parsed.lines, place)
        if answers ~= "" then
            reader.emit{ text = answers, window = "choice", channel = "choice",
                         id = "choice:" .. text.hash(raw) }
            reader.layout("choice", { space = { 640, 480 }, glyphs = acells })
            shown.choice = true
        end
    end
end)

reader.on_state_loaded(function()
    withdraw()
    said, last_raw, still = nil, nil, 0
end)
