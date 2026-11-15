-- reader: title レイトン教授VS逆転裁判 (Professor Layton vs. Phoenix Wright: Ace Attorney)
-- reader: reads Dialogue, narration, subtitles and the puzzles' texts, with speakers, tappable in the picture
-- reader: version The Japanese cartridge release (CTR-P-AVSJ)
-- レイトン教授VS逆転裁判 / Professor Layton vs. Phoenix Wright: Ace Attorney (Japan), CTR-P-AVSJ,
-- title 0004000000078B00. Verified against the cartridge release whose decompressed ExeFS code has
-- SHA-256 02f1d848d5e43c15274ee798af91600ce4bf186efcf4ba090b961b5b5709b1ee.
--
-- The game's scripts (txt/jp/*.xs in vs1.fa, Level-5's XSCR) keep their strings as Shift-JIS with
-- markup: tags in angle brackets (<W30> a pause, <R0.2> a speed, <CR>...</C> a colour, <ME"name"/..>
-- a face...), furigana as [base/reading], a few button pictures as [btn_000], and \n between lines.
-- Every text box of the game is filled by one function, MessageWindow::SetText, which only the
-- script's set-text command calls (through a small wrapper): the message box, the subtitles of the
-- animated scenes, the banners of an investigation, a puzzle's title, question, instructions and
-- task. One call is one box: a message never runs over two pages.
--
-- The speaker: the name plate is a picture, but the script keeps the speaker's name as a string of
-- its own, given to the command in the variable before the message's (investigations), or kept in
-- the string table just before the message (elsewhere). Either is taken when it is a bare name (no
-- markup, at most twelve characters).
--
-- Where each character is: SetText lays the text out at once into quads (AddQuad, one per
-- character and per furigana character, in the order of the window's character list) in the
-- window's own pixels; the window's node says where the window is on which screen. Its place is read
-- again every frame it is drawn (bars slide in), and a window not drawn for a few frames is closed.
--
-- Not covered: text drawn as pictures (the chapter titles, the name plates, the puzzles' results),
-- menus that are not script text. Court (testimony, つきつける/ゆさぶる prompts, the answers to
-- choose from) was not reached in the harness; it is script text set through the same command, and
-- an answer would come as a line of its own on the dialogue channel.

local SET_TEXT = 0x001C68E0        -- MessageWindow::SetText(window, pos, text, style, ...)
local SET_TEXT_FINAL = 0x001C69A8  -- inside it: bl <lay out>; r0 = window, r2 = the string, formatted

-- The script's set-text command calls SetText through a small wrapper (its only caller); this is
-- its return address, checked so that nothing else is ever taken for text.
local FROM_SCRIPT = 0x00208A20

local EXPECTED_CODE = {
    [SET_TEXT] = 0xE92D4FF0,       -- push {r4-r11, lr}
    [SET_TEXT_FINAL] = 0xEB000A00, -- bl 0x1C91B0
    [FROM_SCRIPT - 4] = 0xEBFEF7AF, -- bl SetText
}
for address, word in pairs(EXPECTED_CODE) do
    if mem.u32(address) ~= word then
        error(string.format("this profile is for another build of the game (code at %08X differs)", address), 0)
    end
end

local DEBUG = false
local MAX_MESSAGE_BYTES = 2048
local MAX_NAME_BYTES = 24

local function is_lead(c)
    return (c >= 0x81 and c <= 0x9F) or (c >= 0xE0 and c <= 0xFC)
end

-- Splits Shift-JIS bytes into characters (one or two bytes each).
local function chars(bytes)
    local out, i = {}, 1
    while i <= #bytes do
        local c = bytes:byte(i)
        if is_lead(c) and i < #bytes then
            out[#out + 1] = bytes:sub(i, i + 1)
            i = i + 2
        else
            out[#out + 1] = bytes:sub(i, i)
            i = i + 1
        end
    end
    return out
end

-- Turns a script string into text with ruby. Returns the builder, or nil when nothing is left.
local function decode(raw)
    local b = reader.builder()
    local cs = chars(raw)
    local i, n, any = 1, #cs, false
    while i <= n do
        local c = cs[i]
        if c == "<" then
            -- A tag: skip to its '>' (a tag may hold a quoted name in Shift-JIS).
            while i <= n and cs[i] ~= ">" do i = i + 1 end
            i = i + 1
        elseif c == "[" then
            local j = i + 1
            local base, reading, slash = {}, {}, false
            while j <= n and cs[j] ~= "]" do
                if cs[j] == "/" and not slash then
                    slash = true
                elseif cs[j] == "<" then
                    while j <= n and cs[j] ~= ">" do j = j + 1 end
                elseif slash then
                    reading[#reading + 1] = cs[j]
                else
                    base[#base + 1] = cs[j]
                end
                j = j + 1
            end
            if slash then
                b:add_ruby(text.sjis(table.concat(base)), text.sjis(table.concat(reading)))
                any = true
            end -- else a button picture ([btn_000], [hint01]): nothing to read
            i = j + 1
        elseif c == "\r" then
            i = i + 1
        else
            b:add(text.sjis(c))
            if c ~= "\n" then any = true end
            i = i + 1
        end
    end
    if not any then return nil end
    return b
end

-- The string kept just before the line in the script's table, when it is a bare name.
-- The table's first string has no zero before it but the end of the table's index of string
-- addresses (each 0x08xxxxxx, pointing past itself).
local function starts_string(at)
    if mem.u8(at - 1) == 0 then return true end
    local p = mem.u32(at - 4)
    return p ~= nil and p >= at and p < at + 0x10000
end

local function speaker_before(address)
    if mem.u8(address - 1) ~= 0 then return nil end
    for length = 2, MAX_NAME_BYTES, 2 do
        local start = address - 1 - length
        if starts_string(start) then
            local name = mem.read(start, length)
            if not name or #name ~= length then return nil end
            -- A bare name: double-byte characters only, no markup.
            for k = 1, length, 2 do
                if not is_lead(name:byte(k)) then return nil end
            end
            local ok, s = pcall(text.sjis, name)
            return ok and s or nil
        end
    end
    return nil
end

-- The caller of the call to SetText under way, by window.
local caller = {}
reader.hook(SET_TEXT, function(cpu)
    caller[cpu.r0] = cpu.lr
end)

-- Where each character is drawn. SetText lays the message out at once (MessageWindow::Layout,
-- called at SET_TEXT_FINAL, returning to LAID_OUT): the window keeps a list of 8-byte entries
-- (+0x94 the array, +0x9C the count), one per character in the order of the text (type 0 a
-- character, 1 a character under furigana, 2 a character of the furigana, 5 a line break, others
-- commands), and AddQuad(mesh, &topleft, &bottomright, &uv) is called once for every type 0, 1 and
-- 2 entry, in the same order, with the glyph's corners in the window's own pixels. The window
-- belongs to a node (the wrapper's r0; node+0x138 is the window) whose translation is the
-- window's place on its screen (+0x28, +0x2C), and whose byte +0x12D says which screen.
local WRAPPER = 0x002089EC
local LAID_OUT = 0x001C69AC
local ADD_QUAD = 0x001D2BE4
local DRAW = 0x001CAF9C            -- MessageWindow::Draw(window, ...): every frame it is shown
local LAYOUT_CODE = {
    [WRAPPER] = 0xE52DE004,        -- str lr, [sp, #-4]!
    [LAID_OUT] = 0xE3500000,       -- cmp r0, #0
    [ADD_QUAD] = 0xE92D43F0,       -- push {r4-r9, lr}
    [DRAW] = 0xE92D40F0,           -- push {r4-r7, lr}
}
for address, word in pairs(LAYOUT_CODE) do
    if mem.u32(address) ~= word then
        error(string.format("this profile is for another build of the game (code at %08X differs)", address), 0)
    end
end
local W_ENTRIES, W_ENTRY_COUNT = 0x94, 0x9C
-- The node's matrices: its own (translation at +0x28, +0x2C) and the one it is drawn with, its
-- parents' included (+0x58, +0x5C). A window can move (a bar sliding in): its place is read again
-- every frame it is drawn.
local N_WINDOW, N_X, N_Y, N_SCREEN = 0x138, 0x58, 0x5C, 0x12D
local PITCH = 16                   -- the font's advance, in pixels
local HIDDEN_AFTER_FRAMES = 3

local node_of = {}                 -- window -> its node, from the wrapper
reader.hook(WRAPPER, function(cpu)
    local window = mem.u32(cpu.r0 + N_WINDOW)
    if window then node_of[window] = cpu.r0 end
end)

local laying_out, quads = nil, nil
local shown = {}                   -- window -> { glyphs = ..., layout = ..., drawn = frame }

-- The cells of the characters of `full` (b:text(), before trimming), or nil when the window's
-- list does not match the text.
local function cells(window, full)
    local node = node_of[window]
    if not node or not quads then return nil end
    local screen = mem.u8(node + N_SCREEN)
    if screen ~= 0 and screen ~= 1 then return nil end
    local entries, count = mem.u32(window + W_ENTRIES), mem.u16(window + W_ENTRY_COUNT)
    if not entries or not count or count > 4096 then return nil end
    -- The main characters' quads, line by line.
    local mains, lines, q, line = {}, {}, 0, 1
    for i = 0, count - 1 do
        local kind = mem.u16(entries + 8 * i)
        if kind == 0 or kind == 1 or kind == 2 then
            q = q + 1
            local quad = quads[q]
            if not quad then return nil end
            if kind ~= 2 then mains[#mains + 1] = { quad = quad, line = line } end
        elseif kind == 5 then
            line = line + 1
        end
    end
    if q ~= #quads then return nil end
    -- Each line's left edge and baseline.
    for _, m in ipairs(mains) do
        local l = lines[m.line]
        if not l then l = { left = m.quad[1], bottom = m.quad[4] } lines[m.line] = l end
        if m.quad[1] < l.left then l.left = m.quad[1] end
        if m.quad[4] > l.bottom then l.bottom = m.quad[4] end
    end
    local glyphs, k = {}, 0
    for _, cp in utf8.codes(full) do
        if cp == 10 then
            glyphs[#glyphs + 1] = false
        else
            k = k + 1
            local m = mains[k]
            if not m then return nil end
            local l = lines[m.line]
            local slot = math.floor((m.quad[1] - l.left + 4) / PITCH)
            glyphs[#glyphs + 1] = { l.left + slot * PITCH, l.bottom - 17, PITCH, 19 }
        end
    end
    if k ~= #mains then return nil end
    return glyphs, (screen == 1 and "bottom" or "top"), node
end

reader.hook(ADD_QUAD, function(cpu)
    if not laying_out then return end
    quads[#quads + 1] = { mem.f32(cpu.r1) or 0, mem.f32(cpu.r1 + 4) or 0,
                          mem.f32(cpu.r2) or 0, mem.f32(cpu.r2 + 4) or 0 }
end)

-- The script's message command (0x00194D94; r2 its context, r3 its arguments, [sp] how many). In
-- the investigation scenes the message is a variable of the script's (argument type 4, a number),
-- set just after the variable before it was given the speaker's name: that name is the speaker.
-- Elsewhere the message is the string itself and the name is the string before it in the table.
local MSG_COMMAND = 0x00194D94
if mem.u32(MSG_COMMAND) ~= 0xE92D4FFF then -- push {r0-r11, lr}
    error(string.format("this profile is for another build of the game (code at %08X differs)", MSG_COMMAND), 0)
end
local VALUE_STRING, VALUE_VARIABLE = 8, 4

-- The address of variable `id`'s value (8 bytes: a type, then the value), as the script machine
-- finds it (0x002233E0).
local function variable(ctx, id)
    local arr, count, index
    if id >= 0xFA0 then
        local g = mem.u32(ctx + 0x18)
        if not g then return nil end
        arr, count, index = mem.u32(g + 0x20), mem.u16(g + 0x24), id - 0xFA0
    elseif id >= 0xBB8 then
        arr, count, index = mem.u32(ctx + 0x30), mem.u16(ctx + 0x2A), id - 0xBB8
    elseif id >= 0x7D0 then
        arr, count, index = mem.u32(ctx + 0x34), mem.u16(ctx + 0x2C), id - 0x7D0
    elseif id > 0x3E8 then
        arr, count, index = mem.u32(ctx + 0x38), (mem.u16(ctx + 0x2E) or 0) & 0x7FFF, id - 0x3E9
    else
        return nil
    end
    if not arr or not count or index < 0 or index >= count then return nil end
    return arr + 8 * index
end

local function bare_name(bytes)
    if not bytes or #bytes < 2 or #bytes > 24 then return nil end
    local k = 1
    while k <= #bytes do
        if not is_lead(bytes:byte(k)) or k == #bytes then return nil end
        k = k + 2
    end
    local ok, s = pcall(text.sjis, bytes)
    return ok and s or nil
end

local command_speaker = nil        -- false: the command gave none; nil: no command under way
reader.hook(MSG_COMMAND, function(cpu)
    command_speaker = false
    local args, ctx = cpu.r3, cpu.r2
    local kind, id = mem.u16(args), mem.u32(args + 4)
    if not kind or (kind & 0xFF) ~= VALUE_VARIABLE or not id then return end
    local v = variable(ctx, id - 1)
    if v and ((mem.u16(v) or 0) & 0xF) == VALUE_STRING then
        command_speaker = bare_name(mem.cstr(mem.u32(v + 4) or 0, 32)) or false
    end
end)

local pending = nil                -- what SET_TEXT_FINAL found, completed at LAID_OUT
local last_raw, last_frame = nil, nil

reader.hook(SET_TEXT_FINAL, function(cpu)
    local window, address = cpu.r0, cpu.r2
    local from = caller[window]
    caller[window] = nil
    pending = nil
    if from ~= FROM_SCRIPT then
        if DEBUG then
            reader.log(string.format("not from the script: %08X %s", from or 0, text.sjis(mem.cstr(address, 200) or "")))
        end
        return
    end
    laying_out, quads = window, {}
    pending = { window = window, address = address }
end)

-- Reports where a shown window's characters are, at its node's place now; again only when it moved.
local function place(window)
    local s = shown[window]
    if not s then return end
    local nx, ny = mem.f32(s.node + N_X), mem.f32(s.node + N_Y)
    if not nx or not ny then return end
    nx, ny = math.floor(nx + 0.5), math.floor(ny + 0.5)
    if s.x == nx and s.y == ny then return end
    s.x, s.y = nx, ny
    local glyphs = {}
    local x0, y0, x1, y1 = math.huge, math.huge, -math.huge, -math.huge
    for i, g in ipairs(s.glyphs) do
        if g then
            local c = { g[1] + nx, g[2] + ny, g[3], g[4] }
            glyphs[i] = c
            x0, y0 = math.min(x0, c[1]), math.min(y0, c[2])
            x1, y1 = math.max(x1, c[1] + c[3]), math.max(y1, c[2] + c[4])
        else
            glyphs[i] = false
        end
    end
    if x0 == math.huge then return end
    reader.layout(window, { screen = s.screen, glyphs = glyphs,
                            box = { x0 - 8, y0 - 6, (x1 - x0) + 16, (y1 - y0) + 12 } })
end

reader.hook(LAID_OUT, function(cpu)
    local p = pending
    pending = nil
    local window = laying_out
    laying_out = nil
    if not p or window ~= p.window then return end
    local raw = mem.cstr(p.address, MAX_MESSAGE_BYTES)
    if not raw or #raw == 0 then return end
    local b = decode(raw)
    if not b then
        if shown[window] then reader.layout(window, nil) shown[window] = nil end
        return
    end
    local full = b:text()
    local lead = text.len(full:match("^\n*"))
    local trail = text.len(full:match("\n*$"))
    local line = full:gsub("^\n+", ""):gsub("\n+$", "")
    if line:gsub("\n", ""):gsub(" ", ""):gsub("\227\128\128", "") == "" then
        if shown[window] then reader.layout(window, nil) shown[window] = nil end
        return
    end
    local ruby = b:ruby()
    if lead > 0 then
        for _, r in ipairs(ruby) do r[1] = r[1] - lead end
    end
    -- The same message given to a second window in the same frame (a puzzle's task, set twice).
    if last_raw == raw and last_frame == reader.frame() then return end
    last_raw, last_frame = raw, reader.frame()
    local speaker = command_speaker or nil
    command_speaker = nil
    if not speaker and (p.address < window or p.address > window + 0x1000) then
        speaker = speaker_before(p.address)
    end
    reader.emit{
        text = line,
        speaker = speaker,
        ruby = ruby,
        id = text.hash(raw),
        window = window,
    }
    local glyphs, screen, node = cells(window, full)
    if glyphs then
        local trimmed = {}
        for i = lead + 1, #glyphs - trail do trimmed[#trimmed + 1] = glyphs[i] end
        shown[window] = { drawn = reader.frame(), node = node, screen = screen, glyphs = trimmed }
        place(window)
    else
        if shown[window] then reader.layout(window, nil) end
        shown[window] = nil
        if DEBUG then reader.log("no layout for " .. line) end
    end
end)

-- A window that is not drawn any more has no text on screen.
reader.hook(DRAW, function(cpu)
    local s = shown[cpu.r0]
    if s then
        s.drawn = reader.frame()
        place(cpu.r0)
    end
end)

reader.on_frame(function(frame)
    for window, s in pairs(shown) do
        if frame - s.drawn > HIDDEN_AFTER_FRAMES then
            reader.layout(window, nil)
            shown[window] = nil
        end
    end
end)

reader.on_state_loaded(function()
    reader.forget()
    shown, pending, laying_out, quads = {}, nil, nil, nil
end)
