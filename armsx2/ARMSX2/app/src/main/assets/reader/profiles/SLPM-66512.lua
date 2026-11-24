-- reader: no hooks
-- reader: title Fate/stay night [Realta Nua] (Extra Edition)
-- reader: reads Dialogue, narration, and furigana
-- reader: version SLPM-66512, disc version 1.03
-- reader: touch press
-- reader: buttons start square up down circle cross
--
-- Fate/stay night [Realta Nua] (Kadokawa Shoten / Type-Moon, PS2, 2007), SLPM-66512.
-- Extra Edition (disc version 1.03). Program build ID F6C5993B.
--
-- The game runs on a custom PlayStation 2 port of the KAG3 / Kirikiri visual novel engine.
-- The text is rendered into a fullscreen novel-mode (NVL) message window.
--
-- Memory layout:
--   0x00337F40: Window visibility flag (1 when message window is shown, 0 when hidden).
--   0x00337F48: Pointer to the active null-terminated Shift-JIS message buffer.
--
-- Presentation:
--   Within each novel page, lines accumulate in the message buffer separated by %n.
--   When a new page starts or the window is cleared ([cm] / [er]), the buffer resets.
--   The script tracks growth of the buffer, emitting newly presented lines as they appear
--   to avoid duplicate reading stats, and debounces using SHOWN_FRAMES = 4.
--
-- Furigana:
--   Embedded in the message stream using %[BASE%:RUBY%] tags (e.g. %[纏%:まと%]).
--   Extracted using reader.builder():add_ruby(base, ruby).
--
-- Layout geometry (640x448 screen space):
--   NVL window bounding box: { 36, 20, 568, 408 }.
--   Text start: LEFT = 52, TOP = 30. Glyph width = 18, height = 20. Line pitch = 32.
--   Max row capacity: ROW_FULL = 29 fullwidth characters.
--   Character glyph positions are computed dynamically as lines accumulate on the page.
--
-- Control sequences:
--   Font scale (%s0.9, %s0.818), colors (%ch[r,g,b,a...]), indents (%i0), line waits (%l3,0)
--   are stripped from the reported dialogue text.

local BUILD = "F6C5993B"
if reader.build_id ~= BUILD then
    error("this profile is for Fate/stay night [Realta Nua] SLPM-66512 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

local CHECK_INSN = 0x0012E0F4
if mem.u32(CHECK_INSN) ~= 0x3C0F0033 then
    error("this profile is for Fate/stay night [Realta Nua] SLPM-66512 (different executable)", 0)
end

-- Addresses in game RAM
local WINDOW_ON   = 0x00337F40
local MESSAGE_PTR = 0x00337F48

-- Frames a message must remain unchanged before it is reported
local SHOWN_FRAMES = 4

-- Layout coordinates in 640x448 screen space
local SPACE = { 640, 448 }
local BOX = { 36, 20, 568, 408 }
local LEFT = 52
local TOP = 30
local GLYPH_W = 18
local GLYPH_H = 20
local LINE_PITCH = 32
local ROW_FULL = 29

local reported = nil
local pending = nil
local since = nil
local line_idx = 0
local windows = {}
local current_row = 0
local current_col = 0

local function withdraw()
    for w in pairs(windows) do
        reader.layout(w, nil)
    end
    windows = {}
    reported = nil
    line_idx = 0
    current_row = 0
    current_col = 0
end

reader.on_state_loaded(function()
    withdraw()
    pending = nil
    since = nil
end)

local function parse_kag_text(raw)
    local b = reader.builder()
    local cells = {}
    local at = 1
    local len = #raw

    local function add_char_cell(ch)
        if current_col >= ROW_FULL then
            current_row = current_row + 1
            current_col = 0
        end
        local is_half = #ch == 1
        local w = is_half and (GLYPH_W / 2) or GLYPH_W
        local x = math.floor(LEFT + current_col * GLYPH_W)
        local y = TOP + current_row * LINE_PITCH
        cells[#cells + 1] = { x, y, w, GLYPH_H }
        current_col = current_col + (is_half and 0.5 or 1)
    end

    while at <= len do
        local rest = raw:sub(at)
        if rest:sub(1, 1) == "%" then
            if rest:sub(1, 2) == "%[" then
                local close = rest:find("%]", 3, true)
                if close then
                    local inside = rest:sub(3, close - 1)
                    local sep = inside:find("%:", 1, true)
                    if sep then
                        local base = inside:sub(1, sep - 1)
                        local ruby = inside:sub(sep + 2)
                        b:add_ruby(base, ruby)
                        for ch in base:gmatch("[\1-\127\194-\244][\128-\191]*") do
                            add_char_cell(ch)
                        end
                    end
                    at = at + close + 1
                else
                    at = at + 1
                end
            elseif rest:sub(1, 2) == "%n" then
                if b:length() > 0 then
                    b:add("\n")
                    cells[#cells + 1] = false
                end
                current_row = current_row + 1
                current_col = 0
                at = at + 2
            elseif rest:sub(1, 3) == "%ch" then
                local close = rest:find("]", 4, true)
                at = at + (close or 3)
            else
                local tag = rest:match("^%%[a-zA-Z0-9.,]+")
                if tag then
                    at = at + #tag
                else
                    at = at + 1
                end
            end
        else
            local next_pct = rest:find("%", 1, true)
            local chunk = next_pct and rest:sub(1, next_pct - 1) or rest
            b:add(chunk)
            for ch in chunk:gmatch("[\1-\127\194-\244][\128-\191]*") do
                add_char_cell(ch)
            end
            at = at + #chunk
        end
    end
    return b, cells
end

reader.on_frame(function(frame)
    if (mem.u32(WINDOW_ON) or 0) == 0 then
        if reported then withdraw() end
        return
    end

    local ptr = mem.u32(MESSAGE_PTR)
    if not ptr or ptr == 0 then
        if reported then withdraw() end
        return
    end

    local raw = mem.cstr(ptr)
    if not raw or raw == "" then
        if reported then withdraw() end
        return
    end

    if raw == reported then
        pending = nil
        return
    end

    if raw ~= pending then
        pending = raw
        since = frame
        return
    end

    if frame - since < SHOWN_FRAMES then
        return
    end
    pending = nil

    local grown = reported and #raw > #reported and raw:sub(1, #reported) == reported
    local new_part = raw
    if grown then
        new_part = raw:sub(#reported + 1)
        line_idx = line_idx + 1
    else
        withdraw()
    end
    reported = raw

    -- Decode Shift-JIS to UTF-8 before parsing KAG tags
    local utf8_text = text.sjis(new_part)
    if not utf8_text or utf8_text == "" then
        return
    end

    local b, cells = parse_kag_text(utf8_text)
    local whole = b:text()
    if whole:gsub("[%s　\n]", "") == "" then
        return
    end

    local window = "message" .. (grown and ("+" .. line_idx) or "")
    windows[window] = true

    reader.emit{
        text = whole,
        ruby = b:ruby(),
        window = window,
        id = text.hash(new_part) .. ":" .. line_idx,
    }

    if reader.layout then
        reader.layout(window, {
            space = SPACE,
            box = BOX,
            glyphs = cells,
        })
    end
end)
