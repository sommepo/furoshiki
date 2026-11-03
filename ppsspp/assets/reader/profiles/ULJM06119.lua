-- reader: no hooks
-- reader: title DUNAMIS15
-- reader: reads The message window with speakers and furigana
-- reader: touch press
-- reader: buttons up down left right circle cross square triangle
-- reader: version ULJM-06119
-- DUNAMIS15 (5pb./MAGES., PSP, 2011), ULJM-06119. EBOOT.BIN 6C2E2A66.
--
-- Nothing is hooked: the profile reads the game's memory once a frame (so that it can also
-- run where hooks are not supported).
--
-- The game is written in Squirrel (a scripting language) on a 2D engine of its own (classes
-- named M...: MTextRender, MMotion, ...). Everything the profile reads is in the heap, so it is
-- found by its shape and followed from one object:
--
-- player: the script's object that plays the story (class MyPlayer, a Squirrel instance of 91
--   fields; no other instance is that size). It is found by a search of the heap (again after
--   the game goes back to its title, which makes a new one) and checked every frame.
--   Its fields, from 0: [31] msg (the message window's script object), [34] msgWaitState (2
--   while the line is all there and waits for the button), [38] skipMode, [76] msgwin (the
--   window's motion).
-- msg: [1] _text and [2] _name, script objects whose native object (at +0x20) holds the
--   engine's MTextRender at +0x34: the message and the name plate over it.
-- MTextRender: the text laid out, a row to a deque of glyphs (+0x114, a vector of deques of
--   40 bytes), how many glyphs there are (+0x124) and how many are shown yet (+0x128), and
--   where the text area is, from the middle of the screen (+0x144, +0x148, two floats).
-- glyph (52 bytes): +0 a pointer to the font's entry for the character, whose first word is
--   its code point; +4, +8 its place in the text area (floats); +12, +16 its size; +40, +44
--   the readings above it (a vector of 16 bytes each: a pointer to the reading as UTF-16,
--   where it stands from the character, its size).
-- MMotion of msgwin: the window's animation; the clip it plays (+0x54, as an offset from the
--   motion's data at +0x5c) is the one that hides the window while the player hides it (×),
--   under the game's menu and between scenes.
-- The story's Squirrel thread (the SQVM whose stack holds the player): while one of the
--   game's dialogs is up (the TIPS on L, the backlog, the system menu...), an instance of its
--   class is on that stack. The classes are found by name in the root table.

local BUILD = "6C2E2A66"
if reader.build_id ~= BUILD then
    error("this profile is for DUNAMIS15 ULJM-06119 (EBOOT " .. BUILD .. "), the game is " ..
          tostring(reader.build_id))
end

-- Squirrel's own classes (vtables in the program, which is not relocatable).
local INSTANCE_VT = 0x0897cfa8 -- SQInstance
local TEXT_RENDER_VT = 0x0897b6d8 -- MTextRender
local MOTION_VT = 0x08984eb0 -- MMotion
local VM_VT = 0x0897e0e0 -- SQVM, a Squirrel thread
local OT_INSTANCE, OT_INTEGER, OT_STRING = 0x0a008000, 0x05000002, 0x08000010
local OT_TABLE, OT_CLASS, OT_CLASS_PLAIN = 0x0a000020, 0x0a004000, 0x08004000
-- A thread: its stack (+0x18, 8 bytes an entry: type, value), how far it is used (+0x30), its
-- root table (+0x38, type and value).
local VM_STACK, VM_TOP, VM_ROOT = 0x18, 0x30, 0x38

-- The player object: an instance of this size (its memsize at +0x28), its fields from +0x2c,
-- 8 bytes each (type, value).
local PLAYER_SIZE = 0x2dc
local FIELD_MSG, FIELD_WAIT, FIELD_SKIP, FIELD_MSGWIN, FIELD_SCENE = 31, 34, 38, 76, 46
local MSG_TEXT, MSG_NAME = 1, 2
local WAITING = 2 -- msgWaitState while the line waits for the button

-- The window's motion clips, as offsets from the motion's data: the window hidden.
local CLIP_HIDDEN = 0x1bcc

local HEAP_FROM, HEAP_SIZE, CHUNK = 0x08800000, 0x01800000, 0x100000

-- The screen is 480x272; the text area's place is kept from its middle.
local MIDDLE_X, MIDDLE_Y = 240, 136
local STEP = 17           -- a full-width character's step in the window (its font is 18)
local GLYPH, BLOCK = 52, 468 -- a glyph; nine of them to a block of the deque

-- A line is reported once it has stood complete for this many frames (what the skip runs
-- through is not read).
local SHOWN_FRAMES = 4

local function field(object, n)
    return mem.u32(object + 0x2c + 8 * n), mem.u32(object + 0x30 + 8 * n)
end

local function is_player(at)
    if mem.u32(at) ~= INSTANCE_VT or mem.u32(at + 0x28) ~= PLAYER_SIZE then return false end
    return field(at, FIELD_MSG) == OT_INSTANCE and field(at, FIELD_WAIT) == OT_INTEGER and
           field(at, FIELD_MSGWIN) == OT_INSTANCE and field(at, FIELD_SCENE) == OT_STRING
end

-- The player, and the Squirrel thread that plays the story: the one whose stack holds the
-- player. One search of the heap finds both.
local function find_player()
    local instance, vm = string.pack("<I4", INSTANCE_VT), string.pack("<I4", VM_VT)
    local player, vms = nil, {}
    for from = HEAP_FROM, HEAP_FROM + HEAP_SIZE - CHUNK, CHUNK do
        local s = mem.read(from, CHUNK) or ""
        for _, pattern in ipairs{ instance, vm } do
            local i = 1
            while true do
                local j = s:find(pattern, i, true)
                if not j then break end
                if (j - 1) % 4 == 0 and j + 0x2b <= #s then
                    if pattern == vm then
                        vms[#vms + 1] = from + j - 1
                    elseif not player and string.unpack("<I4", s, j + 0x28) == PLAYER_SIZE and
                           is_player(from + j - 1) then
                        player = from + j - 1
                    end
                end
                i = j + 4
            end
        end
    end
    if not player then return nil end
    for _, at in ipairs(vms) do
        local stack, top = mem.u32(at + VM_STACK), mem.u32(at + VM_TOP)
        if stack and top and top <= 1024 then
            local raw = mem.read(stack, top * 8) or ""
            for k = 1, #raw - 7, 8 do
                local t, v = string.unpack("<I4I4", raw, k)
                if t == OT_INSTANCE and v == player then return player, at end
            end
        end
    end
    return player, nil
end

-- The classes of the game's dialogs (the system menu, the TIPS, the backlog, the
-- configuration, the choices, the questions...): the root table's classes whose names end
-- in "Dialog".
local function dialog_classes(vm)
    local classes = {}
    if mem.u32(vm + VM_ROOT) ~= OT_TABLE then return classes end
    local root = mem.u32(vm + VM_ROOT + 4)
    local nodes, count = mem.u32(root + 0x20), mem.u32(root + 0x24)
    if not nodes or not count or count > 4096 then return classes end
    local raw = mem.read(nodes, count * 20) or ""
    for k = 1, #raw - 19, 20 do
        local vt, v, kt, key = string.unpack("<I4I4I4I4", raw, k)
        if kt == OT_STRING and (vt == OT_CLASS or vt == OT_CLASS_PLAIN) then
            local length = mem.u32(key + 0x14) or 0
            local name = length < 64 and mem.read(key + 0x1c, length) or ""
            if name:sub(-6) == "Dialog" then classes[v] = name end
        end
    end
    return classes
end

-- A dialog of the game is over the text: one of its instances is on the story thread's stack
-- (the thread waits in it).
local function dialog_up(vm, classes)
    local stack, top = mem.u32(vm + VM_STACK), mem.u32(vm + VM_TOP)
    if not stack or not top or top > 1024 then return false end
    local raw = mem.read(stack, top * 8) or ""
    for k = 1, #raw - 7, 8 do
        local t, v = string.unpack("<I4I4", raw, k)
        if t == OT_INSTANCE and classes[mem.u32(v + 0x1c) or 0] then return true end
    end
    return false
end

-- The engine's object under a script object (its native part at +0x20).
local function render_of(script_object)
    local native = mem.u32(script_object + 0x20)
    local render = native and mem.u32(native + 0x34)
    if render and mem.u32(render) == TEXT_RENDER_VT then return render end
end

-- The rows of a render: each a list of { code, x, y, w, h, readings = {text...} }.
local function rows_of(render)
    local first_row, end_row = mem.u32(render + 0x114), mem.u32(render + 0x118)
    if not first_row or not end_row or end_row < first_row or end_row - first_row > 40 * 16 then
        return nil
    end
    local rows = {}
    for deque = first_row, end_row - 1, 40 do
        local cur, last, node = mem.u32(deque + 8), mem.u32(deque + 16), mem.u32(deque + 20)
        local finish = mem.u32(deque + 24)
        local row = {}
        while cur ~= finish and #row < 200 do
            local entry = mem.u32(cur)
            local code = entry and mem.u32(entry)
            if not code or code == 0 or code > 0x10ffff then return nil end
            local g = { code = code, x = mem.f32(cur + 4), y = mem.f32(cur + 8),
                        w = mem.f32(cur + 12), h = mem.f32(cur + 16) }
            local rb, re = mem.u32(cur + 40), mem.u32(cur + 44)
            if rb and re and re > rb and re - rb <= 16 * 8 then
                g.readings = {}
                for r = rb, re - 1, 16 do
                    local at = mem.u32(r)
                    local reading = at and mem.read(at, 32) or ""
                    -- UTF-16 up to its zero.
                    local units = {}
                    for k = 1, #reading - 1, 2 do
                        local u = string.unpack("<I2", reading, k)
                        if u == 0 then break end
                        units[#units + 1] = utf8.char(u)
                    end
                    g.readings[#g.readings + 1] = table.concat(units)
                end
            end
            row[#row + 1] = g
            cur = cur + GLYPH
            if cur == last then
                node = node + 4
                cur = mem.u32(node)
                if not cur then return nil end
                last = cur + BLOCK
            end
        end
        rows[#rows + 1] = row
    end
    return rows
end

local function plain(rows)
    local parts = {}
    for _, row in ipairs(rows or {}) do
        for _, g in ipairs(row) do parts[#parts + 1] = utf8.char(g.code) end
    end
    return table.concat(parts)
end

-- The line as text, its readings and a cell for every character. A row the game broke
-- because it was full goes on without a break; one that ends short of the width (the writer
-- broke it, with room for three more characters: the game's rules for where a row may end
-- can take one or two to the next) keeps its break.
local function compose(render, rows)
    local left = MIDDLE_X + (mem.f32(render + 0x144) or -227)
    local top = MIDDLE_Y + (mem.f32(render + 0x148) or 52)
    local width = mem.f32(render + 0x48) or 460
    local b, cells, ruby = reader.builder(), {}, {}
    for r, row in ipairs(rows) do
        for i, g in ipairs(row) do
            local after = row[i + 1]
            local w = after and after.x > g.x and after.x - g.x or STEP
            if w > g.w then w = g.w end
            if g.readings then
                -- The game gives a reading to each kanji ([く]宮[どう]藤); readings of
                -- characters next to each other are one span, so that a word read as a whole
                -- (転校生, ペリ・フェリ・エス) keeps its reading.
                local before = ruby[#ruby]
                if before and before[1] + before[2] == b:length() then
                    before[2] = before[2] + 1
                    before[3] = before[3] .. table.concat(g.readings)
                else
                    ruby[#ruby + 1] = { b:length(), 1, table.concat(g.readings) }
                end
            end
            b:add(utf8.char(g.code))
            cells[#cells + 1] = { left + g.x, top + g.y, w, g.h }
        end
        local last = row[#row]
        if r < #rows and last and last.x + 4 * STEP < width then
            b:add("\n")
            cells[#cells + 1] = false
        end
    end
    return b:text(), ruby, cells, { left - 4, top - 10, width + 8, 90 }
end

local player, vm, dialogs
local next_search = 0
local reported_key  -- what was reported last: the line's text
local stood = 0
local shown = false
local layout

local function withdraw()
    if shown then
        reader.layout("message", nil)
        shown = false
    end
end

local function forget()
    withdraw()
    reported_key, layout, stood = nil, nil, 0
end

reader.on_frame(function(frame)
    if not player or not is_player(player) or not vm or mem.u32(vm) ~= VM_VT then
        player = nil
        forget()
        if frame < next_search then return end
        player, vm = find_player()
        if not player or not vm then
            player = nil
            next_search = frame + 60
            return
        end
        dialogs = dialog_classes(vm)
    end
    local msg_type, msg = field(player, FIELD_MSG)
    if msg_type ~= OT_INSTANCE or mem.u32(msg) ~= INSTANCE_VT then
        forget()
        return
    end
    local text_type, text_object = field(msg, MSG_TEXT)
    local name_type, name_object = field(msg, MSG_NAME)
    if name_type ~= OT_INSTANCE then name_object = nil end
    local render = text_type == OT_INSTANCE and render_of(text_object)
    if not render then
        forget()
        return
    end
    -- The window hidden (by ×, under the menu, between scenes) or a dialog over it (the TIPS):
    -- nothing can be pointed at.
    local msgwin_type, msgwin = field(player, FIELD_MSGWIN)
    local native = msgwin_type == OT_INSTANCE and mem.u32(msgwin + 0x20)
    local motion = native and mem.u32(native + 0x44)
    local hidden = dialog_up(vm, dialogs)
    if not hidden and motion and mem.u32(motion) == MOTION_VT then
        hidden = (mem.u32(motion + 0x54) or 0) - (mem.u32(motion + 0x5c) or 0) == CLIP_HIDDEN
    end
    local total, typed = mem.u32(render + 0x124) or 0, mem.u32(render + 0x128) or 0
    if total == 0 or typed < total then
        -- The next line is being typed out (or the window is empty): the one before is gone.
        forget()
        return
    end
    if hidden then
        withdraw()
        stood = 0
        return
    end
    if layout and not shown then
        reader.layout("message", layout)
        shown = true
    end
    local _, waiting = field(player, FIELD_WAIT)
    local _, skipping = field(player, FIELD_SKIP)
    if waiting ~= WAITING or (skipping or 0) ~= 0 then
        stood = 0
        return
    end
    local rows = rows_of(render)
    if not rows then return end
    local key = plain(rows)
    if key == reported_key or key == "" then return end
    stood = stood + 1
    if stood < SHOWN_FRAMES then return end
    stood = 0
    local line, ruby, cells, box = compose(render, rows)
    local speaker
    local name_render = name_object and render_of(name_object)
    if name_render then
        local name = plain(rows_of(name_render))
        if name ~= "" then speaker = name end
    end
    withdraw()
    reader.emit{ text = line, speaker = speaker, ruby = ruby, window = "message" }
    reported_key = key
    layout = { space = { 480, 272 }, box = box, glyphs = cells }
    reader.layout("message", layout)
    shown = true
end)

reader.on_state_loaded(function()
    forget()
    next_search = 0
end)
