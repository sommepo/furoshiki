-- reader: title アコンカグア
-- reader: reads The message windows' lines (what the survivors say, what the hero thinks and finds, the item window), page by page
-- reader: version SCPS-10131, disc 1 (boot program 9108E514)
-- reader: no hooks
-- reader: touch press
--
-- Aconcagua (Sony Computer Entertainment, 2000), the Japanese release, disc 1. Disc 2
-- (SCPS-10132) has the same boot program and the same game program, and its profile is this
-- file under the other name.
--
-- The boot program only loads the game: every place of the game is a program of its own
-- (PROGRAM.BIN holds 142 programs, the same on both discs; 27 of them are places), loaded at
-- 0x80026000 over the one before, and each carries its own copy of the message windows' code
-- and its own windows, at addresses of its own. So the profile finds them in what is loaded:
-- the routine that sets a window up for a piece of the script (its first instructions hold no
-- address and are the same in every place, in one of two builds), and every call of it, whose
-- first argument is a window. That is looked for again whenever another program is loaded.
--
-- A window (0x170 bytes) keeps where its first glyph is drawn (+0x08 x, +0x0A y, in the
-- game's picture, 208 lines high, which the console shows 8 lines lower), where it is in the
-- script (+0x50; 0 once it is done), what it waits for (+0x54: -1 a press, more than 0 frames,
-- 0 while the page is being made), and the page as it is drawn: Shift-JIS at +0x68 (a space
-- where a line ends), a byte of attributes for each byte at +0xE8 (0 where a line ends), its
-- length at +0x168. The game makes the whole page at once and draws a full-width glyph every
-- 16 pixels, a line every 17. A window that is done is slid out of the picture, sometimes with
-- its page kept: only glyphs inside the picture count. A yes/no question is a page like the
-- others; the game takes its answer from two buttons, there is no cursor to report. The movies'
-- subtitles are drawn into their pictures and are not read. See docs/reader/games/aconcagua.md.

local BUILD = "9108E514"
if reader.build_id ~= BUILD then
    error("this profile is for the boot program " .. BUILD .. ", the disc has " .. tostring(reader.build_id))
end

local LOAD = 0x80026000      -- where each program of the game is loaded
local LOAD_END = 0x80182000  -- past the largest of them
-- The start of the routine that sets a window up (window, script), as the places have it:
-- compiled plainly in 25, compiled for speed in two. Neither holds an address.
local function words(list)
    local out = {}
    for i, w in ipairs(list) do out[i] = string.pack("<I4", w) end
    return table.concat(out)
end
local SETUPS = {
    words{
        0x27BDFFF0, 0xAFBE0008, 0x03A0F021, 0xAFC40010, 0xAFC50014, 0x8FC20010,
        0x00000000, 0xAC400000, 0x8FC20010, 0x00000000, 0xAC400004, 0x8FC20010,
        0x00000000, 0xA4400008, 0x8FC20010, 0x00000000, 0xA440000A, 0x8FC20010,
        0x24030001, 0xA443000C, 0x8FC20010, 0x00000000, 0xA040000E, 0x8FC20010,
        0x00000000, 0xA040000F, 0x8FC20010, 0x8FC30014, 0x00000000, 0xAC430050,
    },
    words{
        0x2406000F, 0x2483003C, 0x24020001, 0xAC800000, 0xAC800004, 0xA4800008,
        0xA480000A, 0xA482000C, 0xA080000E, 0xA080000F, 0xAC850050, 0xAC800054,
        0xA0800058, 0xA0800059, 0xA080005A, 0xA080005B, 0xAC800064, 0xAC800168,
        0xAC600010, 0x24C6FFFF, 0x04C1FFFD, 0x2463FFFC,
    },
}
local PLACE_X, PLACE_Y = 0x08, 0x0A
local SCRIPT, WAIT = 0x50, 0x54
local CHARS, ATTRS, LENGTH = 0x68, 0xE8, 0x168
local CELL, LINE = 16, 17    -- a full-width glyph's step, a line's
local DRAWN_HEIGHT = 208     -- the game draws a picture this high, shown in the middle

local part = nil             -- the first bytes of the program loaded now
local windows = {}           -- its windows' addresses
local since_scan = 0          -- frames since the windows were looked for
local since_load = 0          -- frames since another program was loaded
local shown = {}             -- window address -> what was reported: {key, x, y}

local function s16(v) return v and (v >= 0x8000 and v - 0x10000 or v) end

local function withdraw(w)
    if shown[w] then reader.layout(string.format("w%X", w), nil) end
    shown[w] = nil
end

local function withdraw_all()
    for w in pairs(shown) do withdraw(w) end
end

-- Every aligned place of `needle` in `code`, as addresses.
local function places(code, needle)
    local out, at = {}, 1
    while true do
        local i = string.find(code, needle, at, true)
        if not i then return out end
        if (i - 1) % 4 == 0 then out[#out + 1] = LOAD + i - 1 end
        at = i + 1
    end
end

-- The window a call passes: a0 as the few instructions around the call set it (lui and
-- addiu into a0, or into a register then moved to a0; the last is the call's delay slot).
local function argument(code, call)
    local i = call - LOAD + 1
    local hi, full = {}, {}
    local source = 4
    for k = -6, 1 do
        local p = i + 4 * k
        if k ~= 0 and p >= 1 and p + 3 <= #code then
            local w = string.unpack("<I4", code, p)
            local op, rs, rt = w >> 26, (w >> 21) & 31, (w >> 16) & 31
            if op == 0x0F then
                hi[rt] = w & 0xFFFF
                if rt == 4 then source = 4 end
            elseif op == 0x09 and rs == rt and hi[rt] then
                full[rt] = ((hi[rt] << 16) + s16(w & 0xFFFF)) & 0xFFFFFFFF
            elseif op == 0 and (w & 0x3F) == 0x21 and ((w >> 11) & 31) == 4 and rt == 0 then
                source = rs
            elseif (op == 0 and ((w >> 11) & 31) == 4 and (w & 0x3F) >= 0x20)
                or (((op >= 0x08 and op <= 0x0E) or (op >= 0x20 and op <= 0x26)) and rt == 4) then
                source = nil -- a0 set some other way
            end
        end
    end
    return source and full[source]
end

-- Find the windows of the program that is loaded.
local function scan()
    windows = {}
    -- mem.read gives at most 1 MB at a time.
    local pieces = {}
    for a = LOAD, LOAD_END - 1, 0x80000 do
        local piece = mem.read(a, math.min(0x80000, LOAD_END - a))
        if not piece then return end
        pieces[#pieces + 1] = piece
    end
    local code = table.concat(pieces)
    local seen = {}
    for _, setup in ipairs(SETUPS) do
        for _, at in ipairs(places(code, setup)) do
            local call = string.pack("<I4", 0x0C000000 | ((at >> 2) & 0x3FFFFFF))
            for _, site in ipairs(places(code, call)) do
                local w = argument(code, site)
                if w and not seen[w] and w >= LOAD and w < 0x80200000 then
                    seen[w] = true
                    windows[#windows + 1] = w
                end
            end
        end
    end
    local names = {}
    for i, w in ipairs(windows) do names[i] = string.format("%08X", w) end
    if reader.changed("windows", table.concat(names, " ")) and #names > 0 then
        reader.log("message windows of this program: " .. table.concat(names, " "))
    end
end

-- The picture's size, and how much lower than the game draws it the console shows it.
local function picture_size()
    local w, h = reader.picture()
    w, h = w or 320, h or 224
    return w, h, math.max(0, (h - DRAWN_HEIGHT) // 2)
end

-- The page a window shows: its text, each character's cell, and where the window is. Nothing
-- when what is there does not look like a page (a window of a program loaded before, whose
-- place a smaller program has since taken, is not one any more).
local function page(w, yoff)
    local n = mem.u32(w + LENGTH) or 0
    if n <= 0 or n > 128 then return nil end
    local chars, attrs = mem.read(w + CHARS, n), mem.read(w + ATTRS, n)
    if not chars or not attrs or #chars < n or #attrs < n then return nil end
    local x0, y0 = s16(mem.u16(w + PLACE_X)), s16(mem.u16(w + PLACE_Y))
    if not x0 or not y0 or x0 < 0 or x0 >= 320 or y0 < -64 or y0 >= 240 then return nil end
    local out, cells = {}, {}
    local col, row = 0, 0
    local i = 1
    while i <= n do
        local b, a = chars:byte(i), attrs:byte(i)
        if b == 0 then break end
        if a == 0 then
            -- The end of a line (a space with no attributes).
            if b ~= 0x20 then return nil end
            out[#out + 1] = "\n"; cells[#cells + 1] = false
            col, row = 0, row + 1
            i = i + 1
        elseif a & 0x30 == 0 then
            return nil
        elseif (b >= 0x81 and b <= 0x9F) or (b >= 0xE0 and b <= 0xEF) then
            local t = chars:byte(i + 1)
            if not t or t < 0x40 or t == 0x7F or t > 0xFC then return nil end
            out[#out + 1] = text.sjis(chars:sub(i, i + 1))
            cells[#cells + 1] = { x0 + col, y0 + row * LINE + yoff, CELL, CELL }
            col = col + CELL
            i = i + 2
        elseif b >= 0x20 and b <= 0x7E then
            -- A single-byte character (a Spanish word): taken to be half as wide.
            out[#out + 1] = string.char(b)
            cells[#cells + 1] = { x0 + col, y0 + row * LINE + yoff, CELL / 2, CELL }
            col = col + CELL / 2
            i = i + 1
        else
            return nil
        end
    end
    -- A page that ends with a line's end: no empty line after it.
    while #out > 0 and out[#out] == "\n" do out[#out] = nil; cells[#cells] = nil end
    if #out == 0 then return nil end
    -- A window that is done may keep its page and its place in the script: the game slides it
    -- out of the picture (the item window to x 318, the message window to y 259). Glyphs
    -- outside the picture are not drawn; a page with none in it is not shown.
    local width, height = picture_size()
    local seen = false
    for k, c in ipairs(cells) do
        if c then
            local cx, cy = c[1] + c[3] / 2, c[2] + c[4] / 2
            if cx < 0 or cx >= width or cy < 0 or cy >= height then
                cells[k] = false
            else
                seen = true
            end
        end
    end
    if not seen then return nil end
    return table.concat(out), cells, x0, y0
end

reader.on_frame(function()
    since_scan = since_scan + 1
    since_load = since_load + 1
    local head = mem.read(LOAD, 64)
    -- Another program: look at once, and again every second for a while (it may still be
    -- arriving) or for as long as no window is found.
    if head ~= part then
        withdraw_all()
        part = head
        since_load = 0
        since_scan = 0
        scan()
    elseif since_scan >= 60 and (#windows == 0 or since_load < 600) then
        since_scan = 0
        scan()
    end
    local width, height, yoff = picture_size()
    local open = {}
    for _, w in ipairs(windows) do
        local script = mem.u32(w + SCRIPT) or 0
        local wait = mem.s32(w + WAIT) or 0
        -- A page is complete when the window waits: for a press (-1) or some frames.
        if script >= 0x80010000 and script < 0x80200000 and (wait == -1 or (wait > 0 and wait < 0x10000)) then
            local body, cells, x, y = page(w, yoff)
            if body then
                open[w] = true
                local key = string.format("%X:", script) .. body
                local s = shown[w]
                local name = string.format("w%X", w)
                if not s or s.key ~= key then
                    shown[w] = { key = key, x = x, y = y }
                    reader.emit{ text = body, window = name, id = text.hash(key) }
                    reader.layout(name, { space = { width, height }, glyphs = cells })
                elseif s.x ~= x or s.y ~= y then
                    s.x, s.y = x, y
                    reader.layout(name, { space = { width, height }, glyphs = cells })
                end
            end
        end
    end
    for w in pairs(shown) do
        if not open[w] then withdraw(w) end
    end
end)

reader.on_state_loaded(function()
    withdraw_all()
    part = nil
end)
