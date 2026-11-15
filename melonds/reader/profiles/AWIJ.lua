-- reader: no hooks
-- reader: title ウィッシュルーム 天使の記憶
-- reader: reads The message windows: what is said, with its speaker, Kyle's thoughts, the narration and what is looked at; the answers and questions Kyle picks from
-- reader: version AWIJ (Japan)
-- reader: touch game
--
-- Wish Room: Tenshi no Kioku (Cing, 2007; Hotel Dusk: Room 215 elsewhere), the Japanese cartridge.
-- Held like a book, as the game was made (the user, 2026-11-15: "as designed"): the picture is
-- the DS's own, unturned ("nds.rotation" none), and the console (the Thor) is held on its side, the
-- upper screen on the left, the lower on the right; choose 右利き at the first question. The
-- places below are measured in a page as it is read, 192 wide and 256 high, and handed over in
-- each screen's own picture, 256 wide and 192 high (`unturn`).
--
-- The game keeps the message being shown in one object of its message engine (its first word,
-- 0x020F3704, the class), which a word of the engine's (CURRENT) points at while the window is
-- up; it points at other things while Kyle walks about. The object holds two slots of 0x100
-- bytes (+0x28, +0x128), each a page of the script copied from the room's text with its line
-- breaks made 0x0A, and for each the typewriter's place in it (+0x228, +0x22C). Its state
-- (+0x08) is 1 while a page is being typed, 2 or 3 once it is all there (3 with the ▼).
-- A page starts with its header, %c0%d1%w0%p0000102%n0 (after %! in a conversation): %d the screen (1 the upper), %p the
-- person (five digits) and the face (two), then its text with codes of its own: %cN a colour (2
-- blue, Kyle's thoughts), %mNNN a gesture, %eNN an effect, %sNNNNNNN a sound, %bNNNN the music.
-- The script itself is data/local/jp/textdata.wpf (docs/reader/games/wish-room.md).
-- Everything is read once a frame: no hooks.

local CURRENT = 0x020FDF8C
local CLASS = 0x020F3704
-- What Kyle picks from (an object of its own, which CURRENT points at while it is up): up to
-- four rows of 0x20 bytes from +0x3E, each a mark and the words in Shift-JIS, ended by 0. "◆"
-- marks an answer (two buttons over the other's portrait, 突っ込み's questions too); "д　" a
-- question of 尋ねる (on the notebook's page, drawn with a "?" over the mark).
local CHOICE_CLASS = 0x020F3840
local CHOICE_ROWS, CHOICE_ROW, CHOICE_MAX = 0x3E, 0x20, 4
local MARKS = {
    ["\x81\x9F"] = { skip = 2, x = 40, y = 182, pitch = 39 },        -- ◆ answers
    ["\x84\x74"] = { skip = 4, x = 58, y = 90, pitch = 63 },         -- д　 尋ねる's questions
}
local SLOT, SLOT_SIZE, TYPED = 0x28, 0x100, 0x228
local STATE = 0x08
local TYPING = 1

-- The people the %p numbers name (from the script: who says what to whom), as the nameplates
-- show them. 00101 is Kyle's narration over the black screen, 65533 and 65534 no one (what
-- happens, what is looked at): no speaker.
local PEOPLE = {
    [1] = "カイル", [2] = "ダニング", [3] = "ローザ", [4] = "ルイス", [5] = "ミラ",
    [6] = "ジェフ", [7] = "ヘレン", [8] = "マーティン", [9] = "アイリス", [10] = "ケビン",
    [11] = "メリッサ", [15] = "エド", [16] = "レイチェル",
}
local NARRATION = { [101] = true, [65533] = true, [65534] = true }
local BLACK_SCREEN = 101   -- the narration's own page: the black lower screen, its text higher up

-- Where the lines are, in a screen of 192x256 (the turned picture), measured in the pictures:
-- 12 dots a character (6 for a half-width one), 16 a line.
local SPACE = { 256, 192 }
-- A box in a page as it is read (192x256) to the screen's own picture: the page is that picture
-- turned a quarter to the left, so its top is the picture's right side.
local function unturn(cell)
    local x, y, w, h = cell[1], cell[2], cell[3], cell[4]
    return { 256 - y - h, x, h, w }
end
local X0, CELL_W, CELL_H, LINE = 16, 12, 12, 16
local Y_WINDOW = 199     -- the box at the foot of either screen (speakers, what is looked at)
local Y_NARRATION = 107  -- the black screen of the narration
-- Fixed digits after each code letter; %r is followed by a name up to its end.
local CODE_DIGITS = { c = 1, d = 1, w = 1, n = 1, p = 7, m = 3, e = 2, s = 7, b = 4, l = 1, t = 1 }

-- The page in a slot: its header's fields and its lines, each a list of {character, width}.
local function parse(bytes)
    -- A page of a conversation that may be pressed (突っ込み) starts with %! before its header.
    local d, p, after = bytes:match("^%%?!?%%c%d%%d(%d)%%w%d%%p(%d%d%d%d%d)..%%n%d()")
    if not d then return nil end
    local i = after
    local lines, cur = {}, {}
    lines[1] = cur
    local n = #bytes
    while i <= n do
        local b = bytes:byte(i)
        if b == 0 then break end
        if b == 0x0A then
            if bytes:byte(i + 1) == 0x0A then break end   -- the page's end
            cur = {}; lines[#lines + 1] = cur; i = i + 1
        elseif b == 0x25 then                             -- %: a code
            local letter = bytes:sub(i + 1, i + 1)
            local digits = CODE_DIGITS[letter]
            if digits then
                i = i + 2 + digits
            else
                local name = bytes:match("^%%%a[%w_]*", i) or "%"
                i = i + #name
            end
        elseif (b >= 0x81 and b <= 0x9F) or (b >= 0xE0 and b <= 0xFC) then
            cur[#cur + 1] = { text.sjis(bytes:sub(i, i + 1)), CELL_W }
            i = i + 2
        else
            cur[#cur + 1] = { text.sjis(bytes:sub(i, i)), CELL_W // 2 }
            i = i + 1
        end
    end
    while #lines > 0 and #lines[#lines] == 0 do lines[#lines] = nil end
    return { upper = d == "1", person = tonumber(p), lines = lines }
end

local said = {}     -- slot -> the bytes of the page reported from it
local shown = {}    -- slot -> the window key laid out

local function withdraw(k)
    if shown[k] then reader.layout(shown[k], nil); shown[k] = nil end
end

local function withdraw_all()
    for k in pairs(shown) do withdraw(k) end
end

local function forget()
    withdraw_all()
    said = {}
end

local choice_said = nil

local function choice(object)
    local rows, raw, place = {}, {}, nil
    for i = 0, CHOICE_MAX - 1 do
        local bytes = mem.read(object + CHOICE_ROWS + CHOICE_ROW * i, CHOICE_ROW) or ""
        local mark = MARKS[bytes:sub(1, 2)]
        if not mark or (place and mark ~= place) then break end
        place = mark
        local answer = bytes:sub(mark.skip + 1):match("^[^%z]*")
        raw[#raw + 1] = answer
        rows[#rows + 1] = text.sjis(answer)
    end
    local key = table.concat(raw, "\n")
    -- Once one is picked its row scrolls the words in full (marquee), then the object goes:
    -- whatever it shows meanwhile is the same choice, not a new one.
    if choice_said == true then return end
    if #rows == 0 then
        if choice_said then reader.layout("choice", nil); choice_said = true end
        return
    end
    if key == choice_said then return end
    if choice_said then
        reader.layout("choice", nil)
        choice_said = true
        return
    end
    choice_said = key
    local chars, cells = {}, {}
    for i, answer in ipairs(rows) do
        if i > 1 then chars[#chars + 1] = "\n"; cells[#cells + 1] = false end
        local x = place.x
        for _, ch in utf8.codes(answer) do
            local c = utf8.char(ch)
            local half = ch < 0x80 or (ch >= 0xFF61 and ch <= 0xFF9F)
            local w = half and CELL_W // 2 or CELL_W
            chars[#chars + 1] = c
            cells[#cells + 1] = unturn({ x, place.y + place.pitch * (i - 1), w, CELL_H })
            x = x + w
        end
    end
    local body = table.concat(chars)
    reader.emit({ text = body, window = "choice", channel = "choice", id = "choice:" .. text.hash(key) })
    reader.layout("choice", { screen = "bottom", space = SPACE, glyphs = cells })
end

reader.on_frame(function()
    local object = mem.u32(CURRENT)
    local valid = object and object >= 0x02000000 and object < 0x02400000
    local class = valid and mem.u32(object)
    if class == CHOICE_CLASS then
        if next(shown) then withdraw_all() end
        choice(object)
        return
    elseif choice_said then
        reader.layout("choice", nil); choice_said = nil
    end
    if class ~= CLASS then
        if next(shown) then withdraw_all() end
        return
    end
    local typing = mem.u32(object + STATE) == TYPING
    for k = 0, 1 do
        local start = object + SLOT + SLOT_SIZE * k
        local bytes = mem.read(start, SLOT_SIZE) or ""
        local page = parse(bytes)
        local at = (mem.u32(object + TYPED + 4 * k) or 0) - start
        local key = "slot" .. k
        if not page or #page.lines == 0 or at <= 0 or at > SLOT_SIZE then
            withdraw(k)
            said[k] = nil
        else
            -- The page's own bytes (up to its end) tell one page from the next.
            local stop = bytes:find("\n\n", 1, true) or bytes:find("\0", 1, true) or #bytes
            local raw = bytes:sub(1, stop)
            if said[k] ~= raw then
                withdraw(k)
                said[k] = nil
                if not typing and at >= stop - 1 then
                    local parts = {}
                    for _, line in ipairs(page.lines) do
                        for _, g in ipairs(line) do parts[#parts + 1] = g[1] end
                    end
                    local body = table.concat(parts)
                    if body:find("%S") then
                        said[k] = raw
                        reader.emit({
                            text = body,
                            speaker = not NARRATION[page.person] and PEOPLE[page.person] or nil,
                            window = key,
                            id = text.hash(raw),
                        })
                        do
                            local y0 = (not page.upper and page.person == BLACK_SCREEN) and Y_NARRATION or Y_WINDOW
                            local glyphs = {}
                            for row, line in ipairs(page.lines) do
                                local x = X0
                                for _, g in ipairs(line) do
                                    glyphs[#glyphs + 1] = unturn({ x, y0 + LINE * (row - 1), g[2], CELL_H })
                                    x = x + g[2]
                                end
                            end
                            reader.layout(key, {
                                screen = page.upper and "top" or "bottom",
                                space = SPACE,
                                glyphs = glyphs,
                            })
                            shown[k] = key
                        end
                    end
                end
            end
        end
    end
end)

reader.on_state_loaded(function()
    forget()
    if choice_said then reader.layout("choice", nil); choice_said = nil end
end)
