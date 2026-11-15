-- reader: title 極限脱出 9時間9人9の扉 (Nine Hours, Nine Persons, Nine Doors)
-- reader: reads The novel's text and the talk window with speakers (by the word), the escape rooms, the choices and the file pages
-- reader: version BSKJ
-- reader: audio never
--
-- 極限脱出 9時間9人9の扉 (Chunsoft, DS, 2009), the Japanese cartridge BSKJ. docs/reader/games/999.md
-- has the whole of it.
--
-- The game keeps a scene's text in memory as a list of lines (one copy of each, sorted), in a
-- Shift-JIS of its own: kanji and katakana are ordinary pairs, but hiragana and the commonest
-- punctuation are single bytes, the codes of half-width katakana (A6 を ... DD ん; A1 。 A2 「 A3 」
-- A4 、 A5 …). ⑲ in a line is a short pause, ⑳ and a letter (and digits) a code (a colour, a
-- page), ▼ at the end the mark that waits for a press: none of them is shown as text.
--
-- Every text window the game shows is an object of one class, and a line is put into one by
-- the class's own method (the vtable's ninth slot): one hook there reads every line as it is
-- set, and who called it says which window it is. The talk window's caller copies the
-- speaker's name into its object first (a hook just before the call reads it: "&淳平"). The
-- characters' places come from the routine that draws each one as it is typed; the choices and
-- the file pages come whole, a row at a time, through the class's next method. No cursor: the
-- game keeps which answer is chosen only in a running routine's locals.

local SET_TEXT = 0x020180CC   -- TextWindow::SetText(this, line, ...)
local FROM_NOVEL = 0x02054EE8 -- the novel's narration (the lower screen)
local FROM_TALK = 0x0204DB34  -- the talk window above, with a name box
local TALK_CALL = 0x0204DB30  -- the talk window's call: its object (r5) holds the name at +0xAD

local function check(addr, word)
    local v = mem.u32(addr)
    if v ~= word then
        error(string.format("this profile is for BSKJ's first release: %08X holds %08X, not %08X",
            addr, v or 0, word))
    end
end
check(SET_TEXT, 0xE92D000F)
check(TALK_CALL, 0xE12FFF34)
check(FROM_NOVEL - 4, 0xE12FFF34)

-- The single bytes A1-DD as Shift-JIS pairs.
local KANA = "\x81\x42\x81\x75\x81\x76\x81\x41\x81\x63\x82\xF0\x82\x9F\x82\xA1\x82\xA3\x82\xA5" ..
    "\x82\xA7\x82\xE1\x82\xE3\x82\xE5\x82\xC1\x81\x5B\x82\xA0\x82\xA2\x82\xA4\x82\xA6\x82\xA8" ..
    "\x82\xA9\x82\xAB\x82\xAD\x82\xAF\x82\xB1\x82\xB3\x82\xB5\x82\xB7\x82\xB9\x82\xBB\x82\xBD" ..
    "\x82\xBF\x82\xC2\x82\xC4\x82\xC6\x82\xC8\x82\xC9\x82\xCA\x82\xCB\x82\xCC\x82\xCD\x82\xD0" ..
    "\x82\xD3\x82\xD6\x82\xD9\x82\xDC\x82\xDD\x82\xDE\x82\xDF\x82\xE0\x82\xE2\x82\xE4\x82\xE6" ..
    "\x82\xE7\x82\xE8\x82\xE9\x82\xEA\x82\xEB\x82\xED\x82\xF1"

-- A line of the game's text at addr as characters: each one's UTF-8 and the code the game draws
-- it by (a pair as one number, 0x8F7E; a single byte as itself, 0xCA).
local function characters(addr, limit, plain)
    local s = mem.read(addr, limit or 512) or ""
    local z = s:find("\0", 1, true)
    if z then s = s:sub(1, z - 1) end
    local out, i = {}, 1
    while i <= #s do
        local b = s:byte(i)
        if (b >= 0x81 and b <= 0x9F) or (b >= 0xE0 and b <= 0xFC) then
            out[#out + 1] = { u = text.sjis(s:sub(i, i + 1)), code = b * 256 + (s:byte(i + 1) or 0) }
            i = i + 2
        elseif b >= 0xA1 and b <= 0xDD and not plain then
            local k = (b - 0xA1) * 2 + 1
            out[#out + 1] = { u = text.sjis(KANA:sub(k, k + 1)), code = b }
            i = i + 1
        else
            out[#out + 1] = { u = string.char(b), code = b }
            i = i + 1
        end
    end
    return out
end

local function joined(chars)
    local t = {}
    for n, c in ipairs(chars) do t[n] = c.u end
    return table.concat(t)
end

-- What is shown of a line: no pauses, codes or the waiting mark. ⑳ and a letter (and figures)
-- is a code (⑳C5 a colour, ⑳n, ⑳X40 a place on the row), ⑳B and a letter a button's picture;
-- a capital letter of its own is a picture of a button or an icon in the game's font
-- (十字ボタンの上C, 【戻る】F), not a letter; a run of three ideographic spaces or more pads a
-- row to the window's edge, so it is a new row ("\n", drawn as nothing).
local function clean(chars)
    local out, i = {}, 1
    local function ascii(c) return c and c.code < 0x80 end
    while i <= #chars do
        local c = chars[i]
        if c.u == "⑳" then
            i = i + 1
            if chars[i] and chars[i].u == "B" then i = i + 2
            else
                if ascii(chars[i]) then i = i + 1 end
                while chars[i] and chars[i].u:match("^%d$") do i = i + 1 end
            end
        elseif c.u == "⑲" or c.u == "▼" or c.u == "\r" or c.u == "\n" then
            i = i + 1
        elseif c.u:match("^%u$") and not (chars[i - 1] and chars[i - 1].u:match("^%w$"))
            and not (chars[i + 1] and chars[i + 1].u:match("^%w$")) then
            i = i + 1
        elseif c.u == "　" then
            local j = i
            while chars[j] and chars[j].u == "　" do j = j + 1 end
            if j - i >= 3 then out[#out + 1] = { u = "\n" }
            else for k = i, j - 1 do out[#out + 1] = chars[k] end end
            i = j
        else
            out[#out + 1] = c
            i = i + 1
        end
    end
    -- No spaces or new rows at either end.
    while out[1] and (out[1].u == "　" or out[1].u == " " or out[1].u == "\n") do table.remove(out, 1) end
    while out[#out] and (out[#out].u == "　" or out[#out].u == " " or out[#out].u == "\n") do out[#out] = nil end
    return out
end

-- Where each character is drawn. Every text window draws its characters one at a time, as it
-- types them, through one routine: draw(renderer, code, x, y), x and y within the window's text
-- area, whose place on the screen and which screen are the renderer's. The window being typed
-- into is the caller's r5. A line's characters are matched to what is drawn by their codes, in
-- order (a code with no character, a button's picture, is passed over).
local DRAW_GLYPH = 0x0200CB5C   -- draw(renderer, code, x, y)
local FROM_TYPER = 0x02017B6C   -- the window's typing (not its name box's)
local LINE = 15                 -- a row's height
check(DRAW_GLYPH, 0xE92D4FF8)
check(FROM_TYPER - 4, 0xEBFFD3FB)

local lines = {}      -- window key -> { chars, next, cells, changed, screen }
local typing = {}     -- window object -> the key of the line it is typing
local paragraphs = 0  -- the novel's page keeps its paragraphs: one window each, novel1, novel2...

local function close(key)
    reader.layout(key, nil)
end

local function start_line(object, key, chars)
    local cells = {}
    for n, c in ipairs(chars) do cells[n] = false end
    lines[key] = { key = key, chars = chars, next = 1, cells = cells, changed = false }
    typing[object] = key
    close(key)
end

-- A new paragraph of the novel: its own window. A page begins again (its first character drawn
-- at the top) and the paragraphs before it are gone from the screen.
local function novel_key()
    paragraphs = paragraphs + 1
    return "novel" .. paragraphs
end

local function new_page(keep)
    for key in pairs(lines) do
        if key ~= keep and key:match("^novel") then
            close(key)
            lines[key] = nil
        end
    end
end

reader.hook(DRAW_GLYPH, function(cpu)
    if cpu.lr ~= FROM_TYPER then return end
    local line = lines[typing[cpu.r5] or ""]
    if not line then return end
    local renderer, code, x, y = cpu.r0, cpu.r1, cpu.r2, cpu.r3
    local chars = line.chars
    local n = line.next
    while chars[n] and chars[n].code == nil do n = n + 1 end  -- a new row: nothing drawn
    if not chars[n] or chars[n].code ~= code then return end
    line.screen = (mem.u8(renderer + 0x2C) or 0) ~= 0 and "bottom" or "top"
    line.ox, line.oy = mem.u32(renderer + 0x34) or 8, mem.u32(renderer + 0x38) or 8
    line.cells[n] = { line.ox + x, line.oy + y + 1, 12, LINE - 1 }
    -- The one before ends where this one begins, on the same row.
    local before = line.cells[n - 1]
    if before and before[2] == line.oy + y + 1 and line.ox + x > before[1] then
        before[3] = line.ox + x - before[1]
    end
    if n == 1 and y == 0 and line.key:match("^novel") then new_page(line.key) end
    line.next = n + 1
    line.changed = true
end)


local speaker = nil
local reported = {}

-- The window's other way in (the vtable's tenth slot, at 0x30): a whole row put in at once, not
-- typed. The choices put each answer in so, and the file screen each row of a document's page
-- (in ordinary Shift-JIS there: its hiragana are pairs). Rows that come in frames one after
-- another (a page takes a few, four rows a frame) are one choice or one page.
local PUT_ROW = 0x02015F9C      -- TextWindow::PutRow(this, row, ...)
local FROM_CHOICE = 0x02051B0C  -- a choice's answers, one call each
local FROM_FILE = 0x0203ED18    -- a document's page on the file screen, a row a call
local FROM_FILE_LIST = 0x0203E590 -- the file screen's list of documents (not read)
local FROM_FILE_PAGE = 0x0203EB28 -- and its page number (not read)
check(PUT_ROW, 0xE92D000F)
check(FROM_CHOICE - 4, 0xE12FFF3C)
check(FROM_FILE - 4, 0xE12FFF3C)

local rows = nil      -- { from, list } gathered this frame
local shown_choice = nil

local function flush_rows()
    if not rows then return end
    local from, list = rows.from, rows.list
    rows = nil
    if from == FROM_CHOICE then
        local body = table.concat(list, "\n")
        if body ~= shown_choice then
            shown_choice = body
            reader.emit{ text = body, window = "choice", channel = "choice", id = "choice:" .. text.hash(body) }
        end
    elseif from == FROM_FILE then
        -- Blank rows between paragraphs stay as one new row.
        local body = table.concat(list, "\n"):gsub("\n\n+", "\n"):gsub("^\n", ""):gsub("\n$", "")
        if body ~= "" then reader.emit{ text = body, window = "file" } end
    end
end

reader.hook(PUT_ROW, function(cpu)
    local from = cpu.lr
    if from ~= FROM_CHOICE and from ~= FROM_FILE then
        if from ~= FROM_FILE_LIST and from ~= FROM_FILE_PAGE and not reported[from] then
            reported[from] = true
            reader.log(string.format("a row put in from %08X (object %08X)", from, cpu.r0))
        end
        return
    end
    if rows and rows.from ~= from then flush_rows() end
    rows = rows or { from = from, list = {} }
    rows.fresh = true
    rows.list[#rows.list + 1] = joined(clean(characters(cpu.r1, 256, from == FROM_FILE)))
end)

reader.hook(TALK_CALL, function(cpu)
    -- "&淳平"; "&HERO" (Latin letters) is the narrator's voice in the escape rooms, shown
    -- with no name box.
    local name = joined(characters(cpu.r5 + 0xAD, 16)):gsub("^&", "")
    speaker = (name ~= "" and not name:match("^[%w_]+$")) and name or nil
end)

reader.hook(SET_TEXT, function(cpu)
    local from = cpu.lr
    shown_choice = nil
    local chars = clean(characters(cpu.r1))
    local line = joined(chars)
    if line == "" then return end
    if from == FROM_NOVEL then
        local key = novel_key()
        start_line(cpu.r0, key, chars)
        reader.emit{ text = line, window = key }
    elseif from == FROM_TALK then
        start_line(cpu.r0, "talk", chars)
        reader.emit{ text = line, window = "talk", speaker = speaker }
        speaker = nil
    else
        if not reported[from] then
            reported[from] = true
            reader.log(string.format("a window set from %08X (object %08X)", from, cpu.r0))
        end
        reader.emit{ text = line, window = string.format("w%08X", from) }
    end
end)

reader.on_state_loaded(function()
    speaker, rows, shown_choice = nil, nil, nil
    for key in pairs(lines) do close(key) end
    lines, typing = {}, {}
end)

reader.on_frame(function()
    if rows then
        if rows.fresh then rows.fresh = false else flush_rows() end
    end
    for _, line in pairs(lines) do
        if line.changed then
            line.changed = false
            reader.layout(line.key, { screen = line.screen, glyphs = line.cells })
        end
    end
end)
