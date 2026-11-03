-- reader: title Core check (a test of the Vita core's hooks: see main.c)
-- The offset of say() in the app as built: arm-vita-eabi-nm on it; odd, as say() is Thumb code.
local SAY = 0x548 + 1
reader.log(string.format("base %X build %s", reader.base or 0, tostring(reader.build_id)))
local last
reader.hook(reader.base + SAY, function(cpu)
    local line = mem.cstr(cpu.r0)
    local seen = string.format("%s buttons %X touch %d,%d", line, cpu.r1, cpu.r2, cpu.r3)
    if seen ~= last then
        last = seen
        reader.log("hook: " .. seen)
        reader.emit{ text = line, window = "test", id = line }
    end
end)
local frames = 0
reader.on_frame(function()
    frames = frames + 1
    if frames % 300 == 0 then reader.log("frames " .. frames) end
end)
