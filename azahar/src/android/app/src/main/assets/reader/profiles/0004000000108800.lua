-- reader: title 逆転裁判123 成歩堂セレクション (Phoenix Wright: Ace Attorney Trilogy)
-- reader: reads Dialogue
-- reader: version The Japanese cartridge release (CTR-P-BHDJ)
-- 逆転裁判123 成歩堂セレクション / Phoenix Wright: Ace Attorney Trilogy (Japan), CTR-P-BHDJ,
-- title 0004000000108800. Verified against the cartridge release whose decompressed ExeFS code has
-- SHA-256 52d942e8794d7341c3865ff2b4fe8b85048944611b757f32ee6b923fcac3957d.
--
-- The Nintendo 3DS release of the trilogy runs Capcom's CTR NW4C engine with original bytecode
-- scripts. Script state and message text window structures are managed dynamically in memory:
--   - TextRunner / Message State: 0x003B21AC (control status at +0x00, active bit 0x800, wait bit 0x1)
--   - Game index: 0x002B45A0 (0 = GS1, 1 = GS2, 2 = GS3)
--   - Character table pointers for each game: 0x002B85C8 (array of null-terminated UTF-16LE glyphs)
--   - Active message line slots: 0x003B21AC + 0x0E0 (line 0), +0x2E0 (line 1), +0x4E0 (line 2)
--     Each 16-byte slot holds the token code (with active bit 0x8000) and relative X position.
--
-- The message window is shown on the top screen (400x240) with up to three lines of dialogue.
-- Box dimensions: { 40, 180, 320, 60 }, starting at x=52, y=191, pitch=20.
-- Not covered: menus, court record evidence descriptions, and graphics drawn directly as textures.

local EXPECTED_CODE = {
    [0x00100000] = 0xEB000007,
    [0x0015DAE0] = 0xE92D5FF0,
    [0x0015DCE4] = 0xE1D100B0,
    [0x00152CF8] = 0xE92D41F0,
}
for address, word in pairs(EXPECTED_CODE) do
    if mem.u32(address) ~= word then
        error(string.format("this profile is for another build of the game (code at %08X differs)", address), 0)
    end
end

local BOX = { 40, 180, 320, 60 }
local GLYPH_W, GLYPH_H = 16, 16
local X_START, Y_START = 52, 191
local LINE_PITCH = 20

local STATE_ADDR = 0x003B21AC
local GAME_ADDR = 0x002B45A0
local TABLES_ADDR = 0x002B85C8

local last_text = nil
local last_layout_text = nil
local last_layout_cells = nil
local shown = false

local function same_cells(a, b)
    if a == b then return true end
    if not a or not b or #a ~= #b then return false end
    for i = 1, #a do
        local ca, cb = a[i], b[i]
        if ca ~= cb then
            if not ca or not cb then return false end
            if ca[1] ~= cb[1] or ca[2] ~= cb[2] or ca[3] ~= cb[3] or ca[4] ~= cb[4] then
                return false
            end
        end
    end
    return true
end

local function decode_char(tok)
    if tok < 0x80 then return "" end
    local game = mem.u8(GAME_ADDR) or 0
    local table_base = mem.u32(TABLES_ADDR + game * 4)
    if not table_base or table_base == 0 then return "" end
    local ptr = mem.u32(table_base + (tok - 0x80) * 4)
    if not ptr or ptr == 0 then return "" end
    local raw = mem.wstr(ptr, 8)
    return raw and text.utf16le(raw) or ""
end

local function read_current_page()
    local status = mem.u32(STATE_ADDR) or 0
    if (status & 0x800) == 0 then
        return nil, nil
    end

    local lines = {}
    local cells = {}

    for line_idx = 0, 2 do
        local line_base = STATE_ADDR + 0x0E0 + line_idx * 0x200
        local line_chars = {}
        for c_idx = 0, 31 do
            local slot = line_base + c_idx * 16
            local code = mem.u16(slot) or 0
            if (code & 0x8000) == 0 then break end
            local tok = code & 0x7FFF
            local ch = decode_char(tok + 0x80)
            local x_rel = mem.u16(slot + 6) or (c_idx * 17)
            local x = X_START + x_rel
            local y = Y_START + line_idx * LINE_PITCH
            cells[#cells + 1] = { x, y, GLYPH_W, GLYPH_H }
            line_chars[#line_chars + 1] = ch
        end
        if #line_chars > 0 then
            lines[#lines + 1] = table.concat(line_chars, "")
            cells[#cells + 1] = false
        end
    end

    if #lines == 0 then return nil, nil end
    if cells[#cells] == false then
        cells[#cells] = nil
    end
    return table.concat(lines, "\n"), cells
end

reader.on_frame(function()
    local whole, cells = read_current_page()
    if not whole or #whole == 0 then
        if shown then
            reader.layout("message", nil)
            shown = false
            last_layout_text = nil
            last_layout_cells = nil
        end
        return
    end

    local status = mem.u32(STATE_ADDR) or 0
    local waiting = (status & 1) ~= 0

    if waiting and whole ~= last_text then
        last_text = whole
        reader.emit{
            text = whole,
            id = text.hash(whole),
            window = "message",
        }
    end

    if whole ~= last_layout_text or not same_cells(cells, last_layout_cells) then
        reader.layout("message", {
            screen = "top",
            box = BOX,
            glyphs = cells,
        })
        last_layout_text = whole
        last_layout_cells = cells
        shown = true
    end
end)

reader.on_state_loaded(function()
    last_text = nil
    last_layout_text = nil
    last_layout_cells = nil
    if shown then
        reader.layout("message", nil)
        shown = false
    end
end)
