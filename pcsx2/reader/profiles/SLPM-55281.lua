-- reader: no hooks
-- reader: title CLOCK ZERO 〜終焉の一秒〜 (Clock Zero: Shuuen no Ichibyou)
-- reader: reads Dialogue, narration, speakers, and choices
-- reader: version SLPM-55281, disc version 1.01
-- reader: touch press
-- reader: buttons start select up down left right circle cross triangle square
--
-- CLOCK ZERO 〜終焉の一秒〜 (Otomate / Idea Factory, PS2, 2010), SLPM-55281. Program B04CB48F.
--
-- The game runs on Idea Factory / Otomate's STCM2 scripting engine with the CZADV message
-- system. Text is kept in Shift-JIS format with `#` prefix command sequences:
--   - `#n`: line break
--   - `#[A-Za-z]+[...]`: text attributes (color, position, font scale, speed, effects)
--
-- Messages are written to the window array S_Window (up to 20 windows of 1040 bytes each).
-- Window 0 is the primary story window. When text layout is finished, bit 1 (0x2) of
-- proc (+0x336) is set, and window state (+0x35a) enters active display mode.
-- Speaker names appear in S_Speaker at 0x00271190 when active (state at 0x002711ae is
-- non-negative; 0xff / -1 when there is no speaker, e.g. for narration).
--
-- Choices are managed by st_SelectProc at 0x01ac1230 and st_SelectData at 0x01ac1280.
-- When a selection is presented, choice count (+0x38) is non-zero (1 to 8), an index table
-- (+0x18) maps active choices to entries in st_SelectData (60 bytes each), and now_sel
-- (+0x12) holds the current cursor index (0-indexed, or -1 before selection).

local BUILD = "B04CB48F"
if reader.build_id ~= BUILD then
    error("this profile is for CLOCK ZERO 〜終焉の一秒〜 SLPM-55281 (program " .. BUILD ..
          "), the game is " .. tostring(reader.build_id))
end

-- Addresses (ELF loads at link address 0x00100000).
local S_WINDOW = 0x01a07840       -- window array: 20 entries of 1040 (0x410) bytes
local WINDOW_SIZE = 1040
local WINDOW_MAX = 20

local S_SPEAKER = 0x00271190      -- speaker name buffer (Shift-JIS string, up to 30 bytes)
local SPEAKER_STATE = 0x002711ae  -- signed byte: -1 (0xff) when speaker is closed

local SELECT_PROC = 0x01ac1230    -- st_SelectProc structure (76 bytes)
local SELECT_DATA = 0x01ac1280    -- st_SelectData array (8 entries of 60 bytes each)
local SELECT_DATA_SIZE = 60

-- Layout coordinates in 640x448 screen space.
local SPACE = { 640, 448 }
local BOX = { 46, 314, 548, 114 }       -- dialogue text window bounding box
local LEFT, TOP = 64, 330               -- text start position inside box
local GLYPH_W, GLYPH_H = 20, 22         -- character glyph dimensions
local LINE_PITCH = 24                   -- vertical line pitch
local ROW_FULL = 26                     -- max characters per line

local CHOICE_PITCH = 38                 -- choice vertical pitch
local CHOICE_HEIGHT = 22                -- choice text height

-- Frames a line must stay stable before it is reported.
local SHOWN_FRAMES = 3

local reported_raw
local pending_raw
local since
local answers_shown
local cursor_said

local function s8(val)
    if not val then return nil end
    return val >= 0x80 and val - 0x100 or val
end

local function s16(val)
    if not val then return nil end
    return val >= 0x8000 and val - 0x10000 or val
end

-- Clean engine formatting: convert #n to newline and strip attribute tags.
local function clean_text(raw)
    if not raw or raw == "" then return "" end
    local s = raw:gsub("#n", "\n")
    s = s:gsub("#%a+%b[]", "")
    s = s:gsub("[\x01-\x09\x0b-\x1f]", "")
    s = s:match("^%s*(.-)%s*$") or ""
    if s == "" then return "" end
    return text.sjis(s)
end

-- Retrieve active speaker name.
local function get_speaker()
    local state = s8(mem.u8(SPEAKER_STATE))
    if state and state ~= -1 then
        local raw = mem.cstr(S_SPEAKER, 30)
        if raw and raw ~= "" then
            local spk = text.sjis(raw):match("^%s*(.-)%s*$")
            if spk and spk ~= "" then
                return spk
            end
        end
    end
    return nil
end

local function withdraw()
    if reader.layout then
        reader.layout("dialogue", nil)
        reader.layout("choice", nil)
    end
end

-- Compute character cells for dialogue text.
local function compute_dialogue_cells(text_str)
    local cells = {}
    local x, y = LEFT, TOP
    local col = 0
    for ch in text_str:gmatch("[\1-\127\194-\244][\128-\191]*") do
        if ch == "\n" then
            cells[#cells + 1] = false
            x = LEFT
            y = y + LINE_PITCH
            col = 0
        else
            local is_half = #ch == 1
            local w = is_half and (GLYPH_W / 2) or GLYPH_W
            if col >= ROW_FULL then
                x = LEFT
                y = y + LINE_PITCH
                col = 0
            end
            cells[#cells + 1] = { x, y, w, GLYPH_H }
            x = x + w
            col = col + (is_half and 0.5 or 1)
        end
    end
    return cells
end

-- Compute character cells for choices.
local function compute_choice_cells(rows)
    local cells = {}
    local num = #rows
    local start_y = 224 - (num * CHOICE_PITCH) / 2
    for r, row in ipairs(rows) do
        if r > 1 then
            cells[#cells + 1] = false
        end
        local row_w = 0
        for ch in row:gmatch("[\1-\127\194-\244][\128-\191]*") do
            row_w = row_w + (#ch == 1 and (GLYPH_W / 2) or GLYPH_W)
        end
        local x = 320 - row_w / 2
        local y = start_y + (r - 1) * CHOICE_PITCH
        for ch in row:gmatch("[\1-\127\194-\244][\128-\191]*") do
            local is_half = #ch == 1
            local w = is_half and (GLYPH_W / 2) or GLYPH_W
            cells[#cells + 1] = { x, y, w, CHOICE_HEIGHT }
            x = x + w
        end
    end
    return cells
end

-- Choices on screen.
local function choice()
    local num = mem.u16(SELECT_PROC + 0x38) or 0
    if num < 1 or num > 8 then
        if answers_shown then
            if reader.layout then reader.layout("choice", nil) end
            answers_shown = nil
        end
        return
    end

    local rows = {}
    for i = 0, num - 1 do
        local idx = mem.u32(SELECT_PROC + 0x18 + i * 4) or i
        if idx < 0 or idx > 7 then idx = i end
        local entry = SELECT_DATA + idx * SELECT_DATA_SIZE
        local active = mem.u8(entry + 4)
        local raw = mem.cstr(entry + 5, 55)
        if active ~= 1 or not raw or raw == "" then
            return -- not all choice rows populated yet
        end
        rows[#rows + 1] = clean_text(raw)
    end

    local key = table.concat(rows, "\n")
    if key == "" then return end
    if key == answers_shown then return end
    answers_shown = key

    withdraw()
    reader.emit{
        text = key,
        channel = "choice",
        window = "choice",
        id = "choice:" .. text.hash(key)
    }
    if reader.layout then
        local cells = compute_choice_cells(rows)
        reader.layout("choice", { space = SPACE, glyphs = cells })
    end
end

-- Touch play: cursor tracking for choices.
local function report_cursor()
    local num = mem.u16(SELECT_PROC + 0x38) or 0
    local at
    if answers_shown and num >= 1 and num <= 8 then
        local cur = s16(mem.u16(SELECT_PROC + 0x12)) or -1
        if cur >= 0 and cur < num then
            at = cur
        else
            at = -1
        end
    end
    if at == cursor_said then return end
    cursor_said = at
    if reader.cursor then
        reader.cursor(at)
    end
end

-- Story dialogue and narration.
local function dialogue(frame)
    if answers_shown then return end

    local active_found = false
    for i = 0, WINDOW_MAX - 1 do
        local win = S_WINDOW + i * WINDOW_SIZE
        local actnum = s8(mem.u8(win))
        local name = mem.cstr(win + 0x01, 20)

        -- Dialogue is shown in main_win (and related main windows)
        if actnum and actnum >= 0 and name and name:find("^main") then
            local mode = mem.u16(win + 0x35a) or 0
            local proc = mem.u8(win + 0x336) or 0
            local open = mem.u8(win + 0x343) or 0

            if mode == 2 or mode == 3 or (proc & 0x2) ~= 0 or open ~= 0 then
                local raw = mem.cstr(win + 0x15, 800)
                if raw and raw ~= "" then
                    active_found = true
                    if raw ~= reported_raw then
                        if raw ~= pending_raw then
                            pending_raw, since = raw, frame
                        elseif frame - since >= SHOWN_FRAMES then
                            local body = clean_text(raw)
                            if body ~= "" then
                                local spk = get_speaker()
                                reported_raw = raw
                                pending_raw = nil
                                reader.emit{
                                    text = body,
                                    speaker = spk,
                                    channel = "dialogue",
                                    window = "dialogue",
                                    id = "dialogue:" .. text.hash(raw)
                                }
                                if reader.layout then
                                    local cells = compute_dialogue_cells(body)
                                    reader.layout("dialogue", {
                                        space = SPACE,
                                        box = BOX,
                                        glyphs = cells
                                    })
                                end
                            end
                        end
                    end
                    return
                end
            end
        end
    end

    pending_raw = nil
    if not active_found and reported_raw then
        withdraw()
        reported_raw = nil
    end
end

reader.on_state_loaded(function()
    reported_raw, pending_raw, since = nil, nil, nil
    answers_shown, cursor_said = nil, nil
    withdraw()
end)

reader.on_frame(function(frame)
    choice()
    report_cursor()
    dialogue(frame)
end)
