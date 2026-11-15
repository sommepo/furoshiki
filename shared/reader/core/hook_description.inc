// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// A game read by a hook description (docs/core-hooks.md): reader.hooks{...}, the part of the
// profiles' prelude that runs one. A description names addresses of the game's own code, where
// the text's pointer is at each (a register), its encoding and a few plain filters: what other
// text hookers know of a game on an emulator, with no script written for it. Lua source, in
// two pieces (lua_api.cpp runs them after its own prelude): the regular expressions the
// filters use, then the runner.

/// ECMAScript's regular expressions, the common part, over code points: text.resub and
/// text.retest. Alternation, groups, (?: ) (?= ) (?! ), greedy and lazy counts, classes,
/// \d \w \s \b, ^ $ (flag m), back references, flags i (ASCII) m s.
const char HookRegexPrelude[] = R"lua(
do
    local codepoint, char, unpack, concat = utf8.codepoint, utf8.char, table.unpack, table.concat

    local function points(s)
        local out, n = {}, 0
        for _, c in utf8.codes(s) do
            n = n + 1
            out[n] = c
        end
        return out, n
    end

    local function is_digit(c) return c >= 48 and c <= 57 end
    local function is_word(c)
        return (c >= 48 and c <= 57) or (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or c == 95
    end
    local function is_space(c)
        return c == 32 or (c >= 9 and c <= 13) or c == 0xA0 or c == 0x3000 or c == 0xFEFF or c == 0x1680
            or (c >= 0x2000 and c <= 0x200A) or c == 0x2028 or c == 0x2029 or c == 0x202F or c == 0x205F
    end
    local classes = { d = is_digit, w = is_word, s = is_space }
    local function fold(c)
        if c >= 65 and c <= 90 then return c + 32 end
        return c
    end

    local function hex(p, from, count)
        local value = 0
        for i = from, from + count - 1 do
            local c = p[i]
            local digit = c and ((c >= 48 and c <= 57 and c - 48) or (c >= 65 and c <= 70 and c - 55)
                or (c >= 97 and c <= 102 and c - 87))
            if not digit then error("regex: bad hex escape", 0) end
            value = value * 16 + digit
        end
        return value
    end

    -- Parses the pattern's code points into a tree.
    local function parse(p, n)
        local at, groups = 1, 0
        local alt

        -- An escape after the backslash at `at - 1`: a class test, or a code point.
        local function escape(in_set)
            local c = p[at]
            if not c then error("regex: \\ at the end", 0) end
            at = at + 1
            local l = char(c)
            if classes[l] then return classes[l] end
            if l == "D" or l == "W" or l == "S" then
                local test = classes[l:lower()]
                return function(x) return not test(x) end
            end
            if l == "n" then return nil, 10 end
            if l == "r" then return nil, 13 end
            if l == "t" then return nil, 9 end
            if l == "f" then return nil, 12 end
            if l == "v" then return nil, 11 end
            if l == "0" then return nil, 0 end
            if l == "x" then
                at = at + 2
                return nil, hex(p, at - 2, 2)
            end
            if l == "u" then
                if p[at] == 123 then -- \u{...}
                    local close = at + 1
                    while p[close] and p[close] ~= 125 do close = close + 1 end
                    local value = hex(p, at + 1, close - at - 1)
                    at = close + 1
                    return nil, value
                end
                at = at + 4
                return nil, hex(p, at - 4, 4)
            end
            if l == "b" and in_set then return nil, 8 end
            if (l == "p" or l == "P" or l == "k") then error("regex: \\" .. l .. " is not known here", 0) end
            return nil, c
        end

        local function set()
            local negate = false
            if p[at] == 94 then
                negate = true
                at = at + 1
            end
            local tests, singles, ranges = {}, {}, {}
            local first = true
            while true do
                local c = p[at]
                if not c then error("regex: [ without ]", 0) end
                if c == 93 and not first then
                    at = at + 1
                    break
                end
                first = false
                at = at + 1
                local test
                if c == 92 then
                    test, c = escape(true)
                end
                if test then
                    tests[#tests + 1] = test
                elseif p[at] == 45 and p[at + 1] and p[at + 1] ~= 93 then
                    at = at + 1
                    local hi = p[at]
                    at = at + 1
                    if hi == 92 then
                        local t
                        t, hi = escape(true)
                        if t then error("regex: a class as a range's end", 0) end
                    end
                    ranges[#ranges + 1] = { c, hi }
                else
                    singles[c] = true
                end
            end
            return { t = "set", test = function(x, icase)
                local found = singles[x] or false
                if not found and icase then
                    found = singles[fold(x)] or (x >= 97 and x <= 122 and singles[x - 32]) or false
                end
                if not found then
                    for i = 1, #ranges do
                        local r = ranges[i]
                        if x >= r[1] and x <= r[2] then
                            found = true
                            break
                        end
                        if icase then
                            local y = fold(x)
                            local z = (x >= 97 and x <= 122) and x - 32 or x
                            if (y >= r[1] and y <= r[2]) or (z >= r[1] and z <= r[2]) then
                                found = true
                                break
                            end
                        end
                    end
                end
                if not found then
                    for i = 1, #tests do
                        if tests[i](x) then
                            found = true
                            break
                        end
                    end
                end
                return found ~= negate
            end }
        end

        local function count()
            -- {n} {n,} {n,m} at `at` (after the brace), or nothing: the brace is then itself.
            local from = at
            local function number()
                local start, value = at, 0
                while p[at] and is_digit(p[at]) do
                    value = value * 10 + p[at] - 48
                    at = at + 1
                end
                return at > start and value or nil
            end
            local low = number()
            local high = low
            if low and p[at] == 44 then
                at = at + 1
                high = number() or math.huge
            end
            if low and p[at] == 125 then
                at = at + 1
                return low, high
            end
            at = from
            return nil
        end

        local function atom()
            local c = p[at]
            at = at + 1
            if c == 40 then -- (
                local node
                if p[at] == 63 then
                    local kind = p[at + 1]
                    if kind == 58 then
                        at = at + 2
                        node = { t = "group", alt() }
                    elseif kind == 61 or kind == 33 then
                        at = at + 2
                        node = { t = "look", alt(), negate = kind == 33 }
                    else
                        error("regex: (?" .. (kind and char(kind) or "") .. " is not known here", 0)
                    end
                else
                    groups = groups + 1
                    local index = groups
                    node = { t = "group", alt(), index = index }
                end
                if p[at] ~= 41 then error("regex: ( without )", 0) end
                at = at + 1
                return node
            elseif c == 91 then
                return set()
            elseif c == 46 then
                return { t = "any" }
            elseif c == 94 then
                return { t = "bol" }
            elseif c == 36 then
                return { t = "eol" }
            elseif c == 92 then
                local l = p[at] and char(p[at]) or ""
                if l == "b" or l == "B" then
                    at = at + 1
                    return { t = "edge", negate = l == "B" }
                end
                if p[at] and p[at] >= 49 and p[at] <= 57 then
                    at = at + 1
                    return { t = "back", index = p[at - 1] - 48 }
                end
                local test, value = escape(false)
                if test then
                    return { t = "set", test = test }
                end
                return { t = "char", c = value }
            elseif c == 42 or c == 43 or c == 63 then
                error("regex: nothing to repeat", 0)
            end
            return { t = "char", c = c }
        end

        local function sequence()
            local list = { t = "seq" }
            while at <= n and p[at] ~= 124 and p[at] ~= 41 do
                local node = atom()
                local c = p[at]
                local low, high
                if c == 42 then
                    low, high, at = 0, math.huge, at + 1
                elseif c == 43 then
                    low, high, at = 1, math.huge, at + 1
                elseif c == 63 then
                    low, high, at = 0, 1, at + 1
                elseif c == 123 then
                    at = at + 1
                    low, high = count()
                    if not low then at = at - 1 end
                end
                if low then
                    local lazy = p[at] == 63
                    if lazy then at = at + 1 end
                    node = { t = "rep", node, low = low, high = high, lazy = lazy }
                end
                list[#list + 1] = node
            end
            return list
        end

        function alt()
            local list = { t = "alt", sequence() }
            while p[at] == 124 do
                at = at + 1
                list[#list + 1] = sequence()
            end
            return #list == 1 and list[1] or list
        end

        local tree = alt()
        if at <= n then error("regex: ) without (", 0) end
        return tree, groups
    end

    -- Turns the tree into one function of a position, each node calling what follows it.
    local function build(node, after, st)
        local t = node.t
        if t == "char" then
            local c = node.c
            if st.icase then
                c = fold(c)
                return function(i)
                    local x = st.s[i]
                    if x and fold(x) == c then return after(i + 1) end
                end
            end
            return function(i)
                if st.s[i] == c then return after(i + 1) end
            end
        elseif t == "any" then
            return function(i)
                local x = st.s[i]
                if x and (st.dotall or (x ~= 10 and x ~= 13)) then return after(i + 1) end
            end
        elseif t == "set" then
            local test = node.test
            return function(i)
                local x = st.s[i]
                if x and test(x, st.icase) then return after(i + 1) end
            end
        elseif t == "seq" then
            local m = after
            for j = #node, 1, -1 do
                m = build(node[j], m, st)
            end
            return m
        elseif t == "alt" then
            local ways = {}
            for j = 1, #node do
                ways[j] = build(node[j], after, st)
            end
            return function(i)
                for j = 1, #ways do
                    local r = ways[j](i)
                    if r then return r end
                end
            end
        elseif t == "group" then
            local index = node.index
            if not index then return build(node[1], after, st) end
            local inner = build(node[1], function(i)
                local from, to = st.from[index], st.to[index]
                st.from[index], st.to[index] = st.open[index], i
                local r = after(i)
                if not r then st.from[index], st.to[index] = from, to end
                return r
            end, st)
            return function(i)
                local old = st.open[index]
                st.open[index] = i
                local r = inner(i)
                st.open[index] = old
                return r
            end
        elseif t == "look" then
            local inner = build(node[1], function(i) return i end, st)
            local negate = node.negate
            return function(i)
                if (inner(i) ~= nil) ~= negate then return after(i) end
            end
        elseif t == "bol" then
            return function(i)
                if i == 1 or (st.multi and st.s[i - 1] == 10) then return after(i) end
            end
        elseif t == "eol" then
            return function(i)
                if i == st.n + 1 or (st.multi and st.s[i] == 10) then return after(i) end
            end
        elseif t == "edge" then
            local negate = node.negate
            return function(i)
                local a, b = st.s[i - 1], st.s[i]
                local edge = ((a and is_word(a)) or false) ~= ((b and is_word(b)) or false)
                if edge ~= negate then return after(i) end
            end
        elseif t == "back" then
            local index = node.index
            return function(i)
                local from, to = st.from[index], st.to[index]
                if not from then return after(i) end
                for k = 0, to - from - 1 do
                    if st.s[i + k] ~= st.s[from + k] then return nil end
                end
                return after(i + to - from)
            end
        elseif t == "rep" then
            local low, high, lazy = node.low, node.high, node.lazy
            local done, began = 0, -1 -- how many times, and where this time began
            local body, step
            local function again(i)
                if i == began and done >= low then return nil end -- a time that took nothing
                done = done + 1
                local r = step(i)
                done = done - 1
                return r
            end
            local function more(i)
                local kept = began
                began = i
                local r = body(i)
                began = kept
                return r
            end
            local function leave(i)
                -- What follows may come back to this same node (a count inside a count).
                local kept_done, kept_began = done, began
                done, began = 0, -1
                local r = after(i)
                done, began = kept_done, kept_began
                return r
            end
            function step(i)
                local n = done
                if n < low then return more(i) end
                if lazy then
                    local r = leave(i)
                    if r then return r end
                    if n < high then return more(i) end
                    return nil
                end
                if n < high then
                    local r = more(i)
                    if r then return r end
                end
                return leave(i)
            end
            body = build(node[1], again, st)
            return function(i)
                local kept_done, kept_began = done, began
                done, began = 0, -1
                local r = step(i)
                done, began = kept_done, kept_began
                return r
            end
        end
        error("regex: " .. tostring(t), 0)
    end

    local compiled = {}
    local function compile(pattern, flags)
        flags = flags or ""
        local key = flags .. "/" .. pattern
        local found = compiled[key]
        if found then return found end
        local p, n = points(pattern)
        local tree, groups = parse(p, n)
        local st = { from = {}, to = {}, open = {}, icase = flags:find("i", 1, true) ~= nil,
                     multi = flags:find("m", 1, true) ~= nil, dotall = flags:find("s", 1, true) ~= nil }
        found = { st = st, groups = groups, match = build(tree, function(i) return i end, st) }
        compiled[key] = found
        return found
    end

    local function begin(re, s)
        local st = re.st
        st.s, st.n = points(s)
        return st
    end

    local function clear(re)
        local st = re.st
        for i = 1, re.groups do
            st.from[i], st.to[i], st.open[i] = nil, nil, nil
        end
    end

    -- Checks a pattern: nothing, or what is wrong with it.
    function text.recheck(pattern, flags)
        local ok, why = pcall(compile, pattern, flags)
        if ok then return nil end
        return tostring(why)
    end

    -- Whether the pattern is found anywhere in `s`.
    function text.retest(s, pattern, flags)
        local re = compile(pattern, flags)
        local st = begin(re, s)
        for i = 1, st.n + 1 do
            clear(re)
            if re.match(i) then return true end
        end
        return false
    end

    -- Every match of the pattern in `s` replaced ($1 to $9, $&, $$ in the replacement).
    function text.resub(s, pattern, replacement, flags)
        local re = compile(pattern, flags)
        local st = begin(re, s)
        local src, n = st.s, st.n
        local rep, rn = points(replacement or "")
        local out, on = {}, 0
        local changed = false
        local i = 1
        while i <= n + 1 do
            clear(re)
            local e = re.match(i)
            if e then
                changed = true
                local k = 1
                while k <= rn do
                    local c = rep[k]
                    local d = rep[k + 1]
                    if c == 36 and d then
                        if d == 36 then
                            on = on + 1
                            out[on] = 36
                            k = k + 2
                        elseif d == 38 then
                            for j = i, e - 1 do
                                on = on + 1
                                out[on] = src[j]
                            end
                            k = k + 2
                        elseif d >= 48 and d <= 57 and d - 48 <= re.groups and d ~= 48 then
                            local from, to = st.from[d - 48], st.to[d - 48]
                            if from then
                                for j = from, to - 1 do
                                    on = on + 1
                                    out[on] = src[j]
                                end
                            end
                            k = k + 2
                        else
                            on = on + 1
                            out[on] = c
                            k = k + 1
                        end
                    else
                        on = on + 1
                        out[on] = c
                        k = k + 1
                    end
                end
                if e == i then
                    if i <= n then
                        on = on + 1
                        out[on] = src[i]
                    end
                    i = i + 1
                else
                    i = e
                end
            else
                if i <= n then
                    on = on + 1
                    out[on] = src[i]
                end
                i = i + 1
            end
        end
        if not changed then return s end
        local parts = {}
        for from = 1, on, 200 do
            parts[#parts + 1] = char(unpack(out, from, math.min(on, from + 199)))
        end
        return concat(parts)
    end
end
)lua";

/// reader.hooks: runs a hook description.
const char HookRunnerPrelude[] = R"lua(
do
    local find, sub, concat = string.find, string.sub, table.concat

    -- Every `what` in `s` replaced by `with`, both as they are written.
    local function swap(s, what, with)
        if what == "" or not find(s, what, 1, true) then return s end
        local out, at = {}, 1
        while true do
            local from, to = find(s, what, at, true)
            if not from then break end
            out[#out + 1] = sub(s, at, from - 1)
            out[#out + 1] = with
            at = to + 1
        end
        out[#out + 1] = sub(s, at)
        return concat(out)
    end

    local function ascii(s)
        return not find(s, "[\128-\255]")
    end

    -- A filter as a function of the text: the text as it goes on, or nothing to drop it.
    local makers = {
        cut = function(f) return function(s) return swap(s, f[2], "") end end,
        swap = function(f) return function(s) return swap(s, f[2], f[3] or "") end end,
        re = function(f)
            local why = text.recheck(f[2], f[4])
            if why then error(why, 0) end
            return function(s) return text.resub(s, f[2], f[3] or "", f[4]) end
        end,
        -- The text up to the first `what`.
        upto = function(f)
            return function(s)
                local from = find(s, f[2], 1, true)
                return from and sub(s, 1, from - 1) or s
            end
        end,
        trim = function() return function(s) return text.resub(s, "^\\s+|\\s+$", "") end end,
        -- Dropped when it is what came last (once), or among the last few (once, n).
        once = function(f)
            local keep, last = f[2] or 1, {}
            return function(s)
                for i = 1, #last do
                    if last[i] == s then
                        table.insert(last, 1, table.remove(last, i))
                        return nil
                    end
                end
                table.insert(last, 1, s)
                last[keep + 1] = nil
                return s
            end
        end,
        -- Dropped when all of it is ASCII (a game's file names and labels).
        japanese = function() return function(s) if ascii(s) then return nil end return s end end,
        -- Dropped when it is exactly this.
        skip = function(f) return function(s) if s == f[2] then return nil end return s end end,
        -- Dropped unless the pattern is found (need), or when it is (deny).
        need = function(f)
            local why = text.recheck(f[2], f[3])
            if why then error(why, 0) end
            return function(s) if text.retest(s, f[2], f[3]) then return s end return nil end
        end,
        deny = function(f)
            local why = text.recheck(f[2], f[3])
            if why then error(why, 0) end
            return function(s) if text.retest(s, f[2], f[3]) then return nil end return s end
        end,
    }

    -- How a hook's text is read: the bytes at an address, and how they become text. `raw` are
    -- filters of the bytes themselves (cutb, swapb: as LunaHook filters its buffer, before
    -- the text is decoded).
    local function readers(hook, raw)
        local kind = hook.text or "sjis"
        local function bytes_then(read, decode)
            return function(at)
                local b = read(at)
                for i = 1, #raw do b = raw[i](b) end
                return decode(b)
            end
        end
        if kind == "sjis" then
            return bytes_then(function(at) return mem.cstr(at, 2048) end, text.sjis),
                   function(v)
                       if v < 0x100 then return text.sjis(string.char(v)) end
                       return text.sjis(string.char((v >> 8) & 0xFF, v & 0xFF))
                   end
        elseif kind == "utf8" then
            return bytes_then(function(at) return mem.cstr(at, 4096) end, function(s)
                if not utf8.len(s) then return "" end
                return s
            end), function(v)
                local bytes = {}
                while v ~= 0 and #bytes < 4 do
                    bytes[#bytes + 1] = string.char(v & 0xFF)
                    v = v >> 8
                end
                local s = concat(bytes)
                return utf8.len(s) and s or ""
            end
        elseif kind == "utf16" then
            if #raw > 0 then error("byte filters are for sjis and utf8 text", 0) end
            return function(at) return text.utf16le(mem.wstr(at, 4096)) end,
                   function(v)
                       v = v & 0xFFFF
                       if v == 0 or (v >= 0xD800 and v <= 0xDFFF) then return "" end
                       return utf8.char(v)
                   end
        end
        error("text is sjis, utf8 or utf16, not " .. tostring(kind), 0)
    end

    local running -- the description in use: its hooks, by name

    --- Runs a hook description: a list of hooks, each
    ---   { name =, at = ADDRESS (a hook) or read = ADDRESS (looked at every frame),
    ---     base = "main" (the address counts from the game's executable),
    ---     from = REGISTER, wide = true (a 64-bit pointer), follow = OFFSET, add = OFFSET,
    ---     text = "sjis" | "utf8" | "utf16", char = true, whole = true, wait = FRAMES,
    ---     channel =, filters = { {"cut", …}, {"re", …}, … } } (cutb and swapb: of the bytes)
    --- and, beside them, source = (whose knowledge it is), only = {names} (the hooks in use).
    function reader.hooks(description)
        if type(description) ~= "table" then error("hooks: a table of hooks", 2) end
        local chosen
        if type(description.only) == "table" and #description.only > 0 then
            chosen = {}
            for _, name in ipairs(description.only) do chosen[name] = true end
        end
        local base
        local list, polled = {}, {}
        for index, hook in ipairs(description) do
            local name = hook.name or ("hook " .. index)
            if chosen == nil or chosen[name] then
                local ok, why = pcall(function()
                    local filters, raw = {}, {}
                    for _, f in ipairs(hook.filters or {}) do
                        if f[1] == "cutb" or f[1] == "swapb" then
                            raw[#raw + 1] = makers[f[1] == "cutb" and "cut" or "swap"](f)
                        else
                            local make = makers[f[1]]
                            if not make then error("no filter called " .. tostring(f[1]), 0) end
                            filters[#filters + 1] = make(f)
                        end
                    end
                    local read_string, read_char = readers(hook, raw)
                    local address = hook.at or hook.read
                    if math.type(address) ~= "integer" then error("at or read: an address", 0) end
                    if hook.base then
                        if base == nil then
                            local module = reader.module(hook.base)
                            base = module and module.base or false
                        end
                        if not base then error("the game's executable is not known", 0) end
                        address = address + base
                    end
                    local state = { name = name, pieces = {}, size = 0, pushed = -1000, last = nil, line = nil,
                                    whole = (hook.whole or hook.read) and true or false, char = hook.char and true or false, wait = hook.wait or 6,
                                    channel = hook.channel or "dialogue", calls = 0, lines = 0 }
                    local function push(s)
                        if s == nil or s == "" then return end
                        for i = 1, #filters do
                            s = filters[i](s)
                            if s == nil or s == "" then return end
                        end
                        -- A string said again as it was (a hook a frame on the same text) is one.
                        if not state.char and s == state.last then return end
                        state.last = s
                        state.pieces[#state.pieces + 1] = s
                        state.size = state.size + #s
                        state.pushed = reader.frame()
                    end
                    state.push = push
                    if hook.read then
                        local seen, steady = nil, 0
                        polled[#polled + 1] = function()
                            local s = read_string(address + (hook.add or 0))
                            -- Memory that holds no text yet (before the game's own is there)
                            -- does not read as text: nothing is said of it.
                            if find(s, "\239\191\189", 1, true) then s = "" end
                            if s ~= seen then
                                seen, steady = s, 0
                                return
                            end
                            steady = steady + 1
                            if steady == 3 then push(s) end -- still for three frames: typed out
                        end
                    else
                        local from = hook.from or error("from: the register the text is in", 0)
                        local follow, add, as_char, wide = hook.follow, hook.add or 0, hook.char, hook.wide
                        reader.hook(address, function(cpu)
                            state.calls = state.calls + 1
                            local value = cpu[from]
                            if math.type(value) ~= "integer" then return end
                            if not wide then value = value & 0xFFFFFFFF end
                            if follow then
                                value = wide and mem.u64(value + follow) or mem.u32(value + follow)
                                if not value then return end
                            end
                            if as_char then
                                push(read_char(value))
                            elseif value ~= 0 then
                                push(read_string(value + add))
                            end
                        end)
                    end
                    list[#list + 1] = state
                end)
                if not ok then
                    reader.warn("hook " .. name .. " is left out: " .. tostring(why))
                end
            end
        end
        running = list
        if #list == 0 then
            reader.warn("no hook of this description can be used")
            return 0
        end
        reader.on_frame(function()
            for i = 1, #polled do polled[i]() end
            local now = reader.frame()
            for i = 1, #list do
                local state = list[i]
                local count = #state.pieces
                if count > 0 and (now - state.pushed >= state.wait or state.size > 6000) then
                    local line = concat(state.pieces, state.whole and "\n" or "")
                    state.pieces, state.size = {}, 0
                    if line ~= state.line and find(line, "%S") then
                        state.line = line
                        state.lines = state.lines + 1
                        reader.emit{ text = line, channel = state.channel, meta = { hook = state.name } }
                    end
                end
            end
        end)
        if reader.on_state_loaded then
            reader.on_state_loaded(function()
                for i = 1, #list do
                    list[i].pieces, list[i].size, list[i].last, list[i].line = {}, 0, nil, nil
                end
            end)
        end
        return #list
    end

    --- What each hook of the running description has given so far, for a tool that lets the
    --- player choose among them: "name\tcalls\tlines\tlast line" a hook, one a line.
    function reader.hooks_seen()
        local out = {}
        for i, state in ipairs(running or {}) do
            out[i] = concat({ state.name, state.calls, state.lines, ((state.line or ""):gsub("[\r\n\t]", " ")) }, "\t")
        end
        return concat(out, "\n")
    end
end
)lua";
