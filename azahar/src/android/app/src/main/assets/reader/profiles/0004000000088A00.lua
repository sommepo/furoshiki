-- reader: title 真・女神転生IV (Shin Megami Tensei IV)
-- reader: reads Dialogue, narration and choices, page by page
-- reader: version The Japanese cartridge release (CTR-P-AMXJ)
-- 真・女神転生IV / Shin Megami Tensei IV (Japan), CTR-P-AMXJ, title 0004000000088A00.
-- Verified against the cartridge release whose decompressed ExeFS code has SHA-256
-- b2d31adbf940d040d0a84106fb4f232577c70eec3c6f19ce5f192030aa6e8984.
--
-- Messages come from MSG2 (.mbm) files: Shift-JIS double-byte text with two-byte control codes
-- F8xx (little-endian arguments) and an FFFF terminator. Every message shown in a text window
-- goes through one function, TextWindow::SetText, which expands variables (names, numbers) into
-- a fixed buffer and makes one 32-byte record per 16-bit unit. The window object then tracks
-- which page is on screen, so each page is reported when the game actually shows it.
--
-- Every frame the window draws the characters revealed so far, one call per character, with
-- the character's record and its position. Those calls are where the on-screen layout comes
-- from: the position of every character of the page, for pointing at the game's own text.
--
-- Not covered: subtitles of the pre-rendered movies (they are part of the video), and text the
-- game draws without a text window (menus, lists, battle numbers).

local SET_TEXT = 0x00287468        -- TextWindow::SetText(this, style, a, b, [sp] c, [sp+4] text)
local SET_TEXT_EXPANDED = 0x00287564 -- inside SetText: r0 = unit count, buffer below is final
local EXPANDED_BUFFER = 0x004DDF08
local MBM_WRAPPER_RETURN = 0x002972D8 -- return address inside "set text from MSG2 entry"
-- DrawGlyph(font, record + 4, colours...; s0 = pen x, s1 = y, s2 = glyph width, s3 = height).
-- Only called by the text window's draw routine.
local DRAW_GLYPH = 0x002AA878

-- The addresses above belong to one build of the program. Refuse any other (an update, another
-- region) instead of reading text from the wrong places.
local EXPECTED_CODE = {
    [SET_TEXT] = 0xE92D4FFF,               -- push {r0-r11, lr}
    [SET_TEXT_EXPANDED - 4] = 0xEB013A25,  -- bl <expand variables>
    [SET_TEXT_EXPANDED] = 0xE1A08000,      -- mov r8, r0
    [DRAW_GLYPH] = 0xE92D03F0,             -- push {r4-r9}
}
for address, word in pairs(EXPECTED_CODE) do
    if mem.u32(address) ~= word then
        error(string.format("this profile is for another build of the game (code at %08X differs)", address), 0)
    end
end

-- TextWindow fields
local W_RECORDS = 0x00    -- one 32-byte record per unit of the message
local W_UNIT_COUNT = 0x04 -- units in the message, including the terminator
local W_PAGE_START = 0x0C -- unit index where the page on screen begins
local W_FLAGS = 0x34
local W_STATE = 0x44      -- 1 while the text is being typed out
local FLAG_CLOSED = 0x10
local STATE_TYPING = 1

-- Records: +0 unit code, then what the font says about the glyph.
local RECORD_SIZE = 32
local R_ADVANCE = 10 -- how far the pen moves after this character, in pixels

-- Window style passed to SetText. Style 3 is the selection list on the bottom screen; the
-- others seen so far are drawn on the top screen.
local STYLE_CHOICES = 3

-- Argument bytes following each F8xx control code.
local ARG_BYTES = {
    [0x01] = 0, [0x02] = 0, [0x04] = 2, [0x11] = 2, [0x12] = 0, [0x13] = 8, [0x14] = 4,
    [0x16] = 0, [0x17] = 0, [0x18] = 0, [0x41] = 2, [0x42] = 2, [0x43] = 2, [0x70] = 2,
    [0x71] = 2, [0x72] = 2, [0x73] = 2, [0x74] = 2, [0x75] = 2, [0x77] = 2, [0x78] = 2,
    [0x79] = 2, [0x7A] = 2, [0x7B] = 8, [0x7C] = 4, [0x7D] = 2,
}

local MAX_MESSAGE_BYTES = 8192

-- Layout reports: while text is being typed out the picture changes every frame, and one
-- report every few frames is plenty. A window that is not drawn any more has no layout.
local TYPING_REPORT_FRAMES = 6
local HIDDEN_AFTER_FRAMES = 3
-- The text box, as far as pointing is concerned: the band of the screen the lines are in.
local SCREEN_WIDTH = { top = 400, bottom = 320 }
local BOX_ABOVE, BOX_BELOW = 12, 6 -- room for the furigana above the first line; a margin below

-- Reads a message up to and including its FFFF terminator.
local function read_message(address)
    local bytes = mem.read(address, MAX_MESSAGE_BYTES)
    local i = 1
    while i < #bytes do
        local a, b = bytes:byte(i, i + 1)
        if a == 0xFF and b == 0xFF then
            return bytes:sub(1, i + 1)
        elseif a == 0xF8 then
            local skip = ARG_BYTES[b]
            if skip == nil then
                return nil, string.format("unknown control code F8%02X", b)
            end
            i = i + 2 + skip
        else
            i = i + 2
        end
    end
    return nil, "unterminated message"
end

local function u32_at(bytes, i)
    local a, b, c, d = bytes:byte(i, i + 3)
    return a | (b << 8) | (c << 16) | (d << 24)
end

-- A page keeps, for every code point of its text, the unit it was made from (false for a line
-- break): the unit is how the character's record, and with it its place on screen, is found.
local function new_page(start_unit)
    return { builder = reader.builder(), start = start_unit, units = {} }
end

-- Turns message bytes into a speaker and a list of pages. Each page knows the unit index at
-- which it starts, which is what the window's page field counts in.
local function decode(raw)
    local page = new_page(0)
    local pages = { page }
    local speaker
    local run, run_units = {}, {} -- pending Shift-JIS characters of the current text run
    local target = "text"         -- "text", "name", "ruby_base" or "ruby_reading"
    local name, ruby_base, ruby_units, ruby_reading = {}, {}, {}, {}

    local function flush()
        if #run == 0 then return end
        if target == "name" then
            name[#name + 1] = text.sjis(table.concat(run))
        elseif target == "ruby_reading" then
            ruby_reading[#ruby_reading + 1] = text.sjis(table.concat(run))
        else
            -- One by one, so that each code point knows its unit even if a unit does not
            -- decode to exactly one.
            for k, pair in ipairs(run) do
                local s = text.sjis(pair)
                local units = page.units
                if target == "ruby_base" then
                    ruby_base[#ruby_base + 1] = s
                    units = ruby_units
                else
                    page.builder:add(s)
                end
                for _ = 1, text.len(s) do
                    units[#units + 1] = run_units[k]
                end
            end
        end
        run, run_units = {}, {}
    end

    local i = 1
    while i < #raw do
        local a, b = raw:byte(i, i + 1)
        if a == 0xFF and b == 0xFF then
            break
        elseif a == 0xF8 then
            flush()
            local args = i + 2
            local next_i = args + ARG_BYTES[b]
            if b == 0x01 then
                if target == "text" then
                    page.builder:add("\n")
                    page.units[#page.units + 1] = false
                end
            elseif b == 0x02 then
                page = new_page((next_i - 1) // 2)
                pages[#pages + 1] = page
            elseif b == 0x12 then
                target = "name"
                name = {}
            elseif b == 0x13 then
                page.voice = u32_at(raw, args + 4)
            elseif b == 0x16 then
                target = "ruby_base"
                ruby_base, ruby_units, ruby_reading = {}, {}, {}
            elseif b == 0x17 then
                target = "ruby_reading"
            elseif b == 0x18 then
                page.builder:add_ruby(table.concat(ruby_base), table.concat(ruby_reading))
                for _, unit in ipairs(ruby_units) do
                    page.units[#page.units + 1] = unit
                end
                target = "text"
            end
            i = next_i
        elseif a == 0x00 and b == 0x00 then
            flush()
            if target == "name" then
                speaker = table.concat(name)
                target = "text"
            end
            i = i + 2
        else
            run[#run + 1] = raw:sub(i, i + 1)
            run_units[#run_units + 1] = (i - 1) // 2
            i = i + 2
        end
    end
    flush()
    return speaker, pages
end

local function trim(s)
    return (s:gsub("^%s+", ""):gsub("%s+$", ""))
end

-- Messages currently laid out, keyed by window address.
local windows = {}
-- Context of the SetText call in progress, captured at its entry.
local pending

-- Reports a page as text. Returns true when this page is now the window's latest text.
local function emit_page(window, entry, index)
    local page = entry.pages[index]
    if page == nil then return false end
    if page.emitted then return entry.latest == index end
    page.emitted = true
    local full = page.builder:text()
    local body = trim(full)
    if body == "" then return false end
    -- Offsets count from the untrimmed text; leading whitespace is rare, so shift them.
    page.lead = text.len(full:match("^%s*"))
    page.length = text.len(body)
    local ruby = {}
    for _, span in ipairs(page.builder:ruby()) do
        ruby[#ruby + 1] = { span[1] - page.lead, span[2], span[3] }
    end
    reader.emit{
        text = body,
        ruby = ruby,
        speaker = entry.speaker,
        channel = entry.channel,
        id = #entry.pages > 1 and (entry.id .. "." .. index) or entry.id,
        window = window,
        meta = {
            voice = page.voice,
            page = index,
            pages = #entry.pages,
            context = string.format("%08X", entry.context),
            style = entry.style,
        },
    }
    entry.latest = index
    return true
end

-- Starts collecting character positions afresh (new text, or another page).
local function reset_positions(entry)
    entry.x, entry.y, entry.w, entry.h = {}, {}, {}, {}     -- as last reported
    entry.fx, entry.fy, entry.fw, entry.fh = {}, {}, {}, {} -- as drawn this frame
    entry.changed = false
end

-- Reports where the characters of the page on screen are, when that changed.
local function report_layout(window, entry, frame)
    -- Take over what was drawn this frame. A unit drawn several times in a frame (an outline
    -- under the character) keeps the last position, which is the character itself.
    local drawn = false
    for unit, x in pairs(entry.fx) do
        drawn = true
        local y, w, h = entry.fy[unit], entry.fw[unit], entry.fh[unit]
        if entry.x[unit] ~= x or entry.y[unit] ~= y or entry.w[unit] ~= w or entry.h[unit] ~= h then
            entry.x[unit], entry.y[unit], entry.w[unit], entry.h[unit] = x, y, w, h
            entry.changed = true
        end
    end
    if drawn then
        entry.fx, entry.fy, entry.fw, entry.fh = {}, {}, {}, {}
        entry.drawn_frame = frame
    elseif entry.shown and frame - entry.drawn_frame > HIDDEN_AFTER_FRAMES then
        reader.layout(window, nil)
        entry.shown = false
        entry.changed = true -- report again if it comes back
        return
    end

    local page = entry.current and entry.pages[entry.current]
    if not drawn or not entry.changed or page == nil then return end
    if mem.u8(window + W_STATE) == STATE_TYPING and entry.shown
        and frame - entry.reported_frame < TYPING_REPORT_FRAMES then
        return
    end

    local glyphs = {}
    local top, bottom
    for k = 1, page.length do
        local unit = page.units[k + page.lead]
        local x = unit and entry.x[unit]
        if x then
            local y, h = entry.y[unit], entry.h[unit]
            glyphs[k] = { x, y, entry.w[unit], h }
            if top == nil or y < top then top = y end
            if bottom == nil or y + h > bottom then bottom = y + h end
        else
            glyphs[k] = false
        end
    end
    if top == nil then return end
    reader.layout(window, {
        screen = entry.screen,
        box = { 0, top - BOX_ABOVE, SCREEN_WIDTH[entry.screen], bottom - top + BOX_ABOVE + BOX_BELOW },
        glyphs = glyphs,
    })
    entry.shown, entry.changed, entry.reported_frame = true, false, frame
end

reader.hook(SET_TEXT, function(cpu)
    local context = cpu.lr
    if cpu.lr == MBM_WRAPPER_RETURN then
        -- Called through the MSG2 lookup wrapper: its own return address identifies the caller.
        context = mem.u32(cpu.sp + 0x14) or cpu.lr
    end
    pending = { window = cpu.r0, style = cpu.r1, context = context }
end)

reader.hook(SET_TEXT_EXPANDED, function(cpu)
    local info = pending
    pending = nil
    if info == nil then return end
    if windows[info.window] then
        reader.layout(info.window, nil)
        windows[info.window] = nil
    end
    local raw, problem = read_message(EXPANDED_BUFFER)
    if raw == nil then
        reader.warn(string.format("message in window %08X skipped: %s", info.window, problem))
        return
    end
    local speaker, pages = decode(raw)
    local entry = {
        speaker = speaker,
        pages = pages,
        id = text.hash(raw),
        unit_count = cpu.r0,
        channel = info.style == STYLE_CHOICES and "choice" or "dialogue",
        screen = info.style == STYLE_CHOICES and "bottom" or "top",
        context = info.context,
        style = info.style,
        drawn_frame = 0,
        reported_frame = 0,
    }
    reset_positions(entry)
    windows[info.window] = entry
    if emit_page(info.window, entry, 1) then
        entry.current = 1
    end
end)

reader.hook(DRAW_GLYPH, function(cpu)
    if next(windows) == nil then return end
    local record = cpu.r1 - 4
    for window, entry in pairs(windows) do
        local records = entry.records
        if records == nil then
            -- Allocated by SetText after the point where the text is read.
            records = mem.u32(window + W_RECORDS)
            if records ~= nil and records ~= 0 then
                entry.records = records
            end
        end
        local offset = record - (entry.records or 0)
        if entry.records and offset >= 0 and offset < entry.unit_count * RECORD_SIZE
            and offset % RECORD_SIZE == 0 then
            local unit = offset // RECORD_SIZE
            -- The cell from the pen position to where the next character starts, which is
            -- what a finger aims at; the glyph itself is a little narrower.
            entry.fx[unit], entry.fy[unit] = cpu.s0, cpu.s1
            entry.fw[unit] = mem.u8(record + R_ADVANCE) or cpu.s2
            entry.fh[unit] = cpu.s3
            return
        end
    end
end)

reader.on_frame(function(frame)
    for window, entry in pairs(windows) do
        local count = mem.u32(window + W_UNIT_COUNT)
        local flags = mem.u32(window + W_FLAGS)
        if count ~= entry.unit_count or flags == nil or (flags & FLAG_CLOSED) ~= 0 then
            -- Closed, or the window now holds something else.
            reader.layout(window, nil)
            windows[window] = nil
        else
            local start = mem.u32(window + W_PAGE_START)
            if start ~= entry.page_start then
                entry.page_start = start
                entry.current = nil
                for index, page in ipairs(entry.pages) do
                    if page.start == start then
                        if emit_page(window, entry, index) then
                            entry.current = index
                        end
                        break
                    end
                end
                -- Positions drawn before the page was known belong to it too: keep those of
                -- this frame, forget the page before.
                entry.x, entry.y, entry.w, entry.h = {}, {}, {}, {}
                entry.changed, entry.shown = false, false
            end
            report_layout(window, entry, frame)
        end
    end
end)

reader.on_state_loaded(function()
    -- Window contents after a state load were laid out before the load; nothing to report.
    windows = {}
    pending = nil
end)
