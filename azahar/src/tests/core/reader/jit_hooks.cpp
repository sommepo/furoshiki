// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Runs small ARM and Thumb programs through the real JIT with the reader's translation hooks and
// checks that a hooked instruction calls back exactly once each time it executes, with the
// registers it is about to see.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include "common/common_types.h"
#include "core/arm/dynarmic/arm_dynarmic_reader.h"
#include "reader/core/engine.h"
#include "tests/core/reader/reader_test_util.h"

using Reader::LogLevel;

namespace {

constexpr u32 StackTop = 0x8000;

/// A guest with code at address 0 and a small stack, wired to the reader's hooks the way the
/// emulator's CPU core is.
class GuestMachine final : public Dynarmic::A32::UserCallbacks {
public:
    explicit GuestMachine(std::vector<u32> program) : code{std::move(program)} {}

    /// A program of 16-bit Thumb instructions.
    static GuestMachine Thumb(std::initializer_list<u16> instructions) {
        std::vector<u32> words;
        std::size_t index = 0;
        for (const u16 instruction : instructions) {
            if (index % 2 == 0) {
                words.push_back(instruction);
            } else {
                words.back() |= static_cast<u32>(instruction) << 16;
            }
            ++index;
        }
        GuestMachine machine{std::move(words)};
        machine.thumb = true;
        return machine;
    }

    std::optional<std::uint32_t> MemoryReadCode(Dynarmic::A32::VAddr vaddr) override {
        const std::size_t index = vaddr / 4;
        return index < code.size() ? std::optional<u32>{code[index]} : std::nullopt;
    }

    bool PreCodeReadHook(bool, Dynarmic::A32::VAddr pc, Dynarmic::A32::IREmitter& ir) override {
        return Core::ReaderHooks::BeforeRead(pc, ir);
    }
    void PreCodeTranslationHook(bool is_thumb, Dynarmic::A32::VAddr pc,
                                Dynarmic::A32::IREmitter& ir) override {
        if (Core::ReaderHooks::IsHooked(pc)) {
            Core::ReaderHooks::EmitHook(pc, is_thumb ? std::nullopt : MemoryReadCode(pc), ir);
        }
    }

    // Only the stack is readable and writable data.
    std::uint8_t MemoryRead8(Dynarmic::A32::VAddr) override {
        return 0;
    }
    std::uint16_t MemoryRead16(Dynarmic::A32::VAddr) override {
        return 0;
    }
    std::uint32_t MemoryRead32(Dynarmic::A32::VAddr vaddr) override {
        const auto slot = Slot(vaddr);
        return slot ? stack[*slot] : 0;
    }
    std::uint64_t MemoryRead64(Dynarmic::A32::VAddr vaddr) override {
        return MemoryRead32(vaddr) | (static_cast<u64>(MemoryRead32(vaddr + 4)) << 32);
    }
    void MemoryWrite8(Dynarmic::A32::VAddr, std::uint8_t) override {}
    void MemoryWrite16(Dynarmic::A32::VAddr, std::uint16_t) override {}
    void MemoryWrite32(Dynarmic::A32::VAddr vaddr, std::uint32_t value) override {
        if (const auto slot = Slot(vaddr)) {
            stack[*slot] = value;
        }
    }
    void MemoryWrite64(Dynarmic::A32::VAddr vaddr, std::uint64_t value) override {
        MemoryWrite32(vaddr, static_cast<u32>(value));
        MemoryWrite32(vaddr + 4, static_cast<u32>(value >> 32));
    }

    // Nothing here may throw: these are called from JIT-compiled code.
    void InterpreterFallback(Dynarmic::A32::VAddr, std::size_t) override {
        Problem("interpreter fallback");
    }
    void CallSVC(std::uint32_t) override {
        Problem("unexpected SVC");
    }
    void ExceptionRaised(Dynarmic::A32::VAddr, Dynarmic::A32::Exception) override {
        Problem("guest exception");
    }

    void AddTicks(std::uint64_t ticks) override {
        ticks_left = ticks > ticks_left ? 0 : ticks_left - ticks;
    }
    std::uint64_t GetTicksRemaining() override {
        return ticks_left;
    }

    /// Runs the program from address 0 for a bounded number of instructions and returns the
    /// registers it ends with.
    std::array<u32, 16> Run(u64 ticks = 400) {
        Dynarmic::A32::UserConfig config;
        config.callbacks = this;
        Dynarmic::A32::Jit jit{config};
        running = &jit;
        jit.Regs() = {};
        jit.Regs()[13] = StackTop;
        jit.SetCpsr(thumb ? 0x000001F0 : 0x000001D0); // user mode, interrupts masked
        ticks_left = ticks;
        jit.Run();
        running = nullptr;
        REQUIRE(problems.empty());
        return jit.Regs();
    }

private:
    std::optional<std::size_t> Slot(u32 vaddr) {
        if (vaddr > StackTop || vaddr < StackTop - 4 * 64 || vaddr % 4 != 0) {
            Problem("memory access outside the stack");
            return std::nullopt;
        }
        return (StackTop - vaddr) / 4;
    }
    void Problem(std::string what) {
        problems.push_back(std::move(what));
        ticks_left = 0;
        if (running) {
            running->HaltExecution();
        }
    }

    std::vector<u32> code;
    bool thumb{};
    std::array<u32, 65> stack{};
    u64 ticks_left{};
    Dynarmic::A32::Jit* running{};
    std::vector<std::string> problems;
};

/// Collects what each hooked address saw, in order.
struct Recorder {
    void Hook(u32 address) {
        detached.engine.AddNativeHook(address, [this](const Reader::GuestRegisters& regs) {
            // These programs fire a hook a handful of times. If compiled code ever loops through
            // one without end, stop the whole process here: it cannot be unwound from inside the
            // JIT, and letting it run would only eat memory.
            if (hits.size() >= 1000) {
                std::fputs("reader JIT test: hook fired 1000 times, giving up\n", stderr);
                std::abort();
            }
            hits.push_back(regs);
        });
    }
    [[nodiscard]] std::size_t Unusable(u32 address) const {
        char needle[48];
        std::snprintf(needle, sizeof(needle), "hook at %08X has no effect", address);
        return detached.CountLogs(LogLevel::Error, needle);
    }
    ReaderTest::DetachedEngine detached;
    std::vector<Reader::GuestRegisters> hits;
};

} // Anonymous namespace

TEST_CASE("A hooked function entry sees the caller's arguments once", "[reader][jit]") {
    //  0x00  mov r0, #5
    //  0x04  mov r1, #7
    //  0x08  bl  0x14
    //  0x0c  mov r5, #9
    //  0x10  b   0x10
    //  0x14  push {r4, lr}        <- hooked
    //  0x18  add r6, r0, r1
    //  0x1c  pop {r4, pc}
    GuestMachine machine{{0xE3A00005, 0xE3A01007, 0xEB000001, 0xE3A05009, 0xEAFFFFFE, 0xE92D4010,
                          0xE0806001, 0xE8BD8010}};
    Recorder recorder;
    recorder.Hook(0x14);
    const auto regs = machine.Run();

    REQUIRE(recorder.hits.size() == 1);
    const auto& hit = recorder.hits[0];
    REQUIRE(hit.r[0] == 5);
    REQUIRE(hit.r[1] == 7);
    REQUIRE(hit.r[13] == StackTop); // before the push
    REQUIRE(hit.r[14] == 0x0C);     // return address
    REQUIRE(hit.r[15] == 0x14);     // the hooked instruction itself
    // The program is undisturbed.
    REQUIRE(regs[6] == 12);
    REQUIRE(regs[5] == 9);
    REQUIRE(regs[13] == StackTop);
}

TEST_CASE("A hook in the middle of straight-line code sees the registers as of that point",
          "[reader][jit]") {
    //  0x00  mov r0, #5
    //  0x04  mov r0, #6           (overwrites r0 within the same stretch of code)
    //  0x08  add r2, r0, #1       <- hooked: must see r0 == 6, r2 == 0
    //  0x0c  mov r0, #9
    //  0x10  b   0x10
    GuestMachine machine{{0xE3A00005, 0xE3A00006, 0xE2802001, 0xE3A00009, 0xEAFFFFFE}};
    Recorder recorder;
    recorder.Hook(0x08);
    const auto regs = machine.Run();

    REQUIRE(recorder.hits.size() == 1);
    REQUIRE(recorder.hits[0].r[0] == 6);
    REQUIRE(recorder.hits[0].r[2] == 0);
    REQUIRE(recorder.hits[0].r[15] == 0x08);
    REQUIRE(regs[2] == 7);
    REQUIRE(regs[0] == 9);
}

TEST_CASE("A hooked instruction in a loop fires on every iteration", "[reader][jit]") {
    //  0x00  mov r0, #0
    //  0x04  add r0, r0, #1       <- hooked
    //  0x08  cmp r0, #10
    //  0x0c  bne 0x04
    //  0x10  b   0x10
    GuestMachine machine{{0xE3A00000, 0xE2800001, 0xE350000A, 0x1AFFFFFC, 0xEAFFFFFE}};
    Recorder recorder;
    recorder.Hook(0x04);
    const auto regs = machine.Run();

    REQUIRE(recorder.hits.size() == 10);
    for (u32 i = 0; i < 10; ++i) {
        REQUIRE(recorder.hits[i].r[0] == i);
    }
    REQUIRE(regs[0] == 10);
}

TEST_CASE("A conditional instruction is not hooked, and code around it still is", "[reader][jit]") {
    //  0x00  mov r2, #12
    //  0x04  cmp r2, #12
    //  0x08  addeq r3, r2, #1     <- hooked: conditional (and executes)
    //  0x0c  addne r4, r2, #2     <- hooked: conditional (and is skipped)
    //  0x10  mov r5, #1           <- hooked
    //  0x14  beq 0x1c             <- hooked: conditional (and is taken)
    //  0x18  mov r6, #1
    //  0x1c  mov r7, #1           <- hooked
    //  0x20  b   0x20
    GuestMachine machine{{0xE3A0200C, 0xE352000C, 0x02823001, 0x12824002, 0xE3A05001, 0x0A000000,
                          0xE3A06001, 0xE3A07001, 0xEAFFFFFE}};
    Recorder recorder;
    for (const u32 address : {0x08, 0x0C, 0x10, 0x14, 0x1C}) {
        recorder.Hook(address);
    }
    const auto regs = machine.Run();

    REQUIRE(recorder.hits.size() == 2);
    REQUIRE(recorder.hits[0].r[15] == 0x10);
    REQUIRE(recorder.hits[0].r[3] == 13);
    REQUIRE(recorder.hits[0].r[5] == 0);
    REQUIRE(recorder.hits[1].r[15] == 0x1C);
    REQUIRE(recorder.hits[1].r[5] == 1);
    // Each refused address is reported, once.
    REQUIRE(recorder.Unusable(0x08) == 1);
    REQUIRE(recorder.Unusable(0x0C) == 1);
    REQUIRE(recorder.Unusable(0x14) == 1);
    REQUIRE(recorder.Unusable(0x10) == 0);
    // The program ran as written.
    REQUIRE(regs[3] == 13);
    REQUIRE(regs[4] == 0);
    REQUIRE(regs[5] == 1);
    REQUIRE(regs[6] == 0);
    REQUIRE(regs[7] == 1);

    // Compiling the same code again does not repeat the report.
    machine.Run();
    REQUIRE(recorder.Unusable(0x08) == 1);
    REQUIRE(recorder.hits.size() == 4);
}

TEST_CASE("Two hooks on one address both run; unhooked code runs no handler", "[reader][jit]") {
    //  0x00  mov r0, #1
    //  0x04  mov r1, #2           <- hooked twice
    //  0x08  b   0x08
    GuestMachine machine{{0xE3A00001, 0xE3A01002, 0xEAFFFFFE}};
    {
        Recorder recorder;
        recorder.Hook(0x04);
        recorder.Hook(0x04);
        machine.Run();
        REQUIRE(recorder.hits.size() == 2);
    }
    {
        Recorder recorder;
        const auto regs = machine.Run();
        REQUIRE(recorder.hits.empty());
        REQUIRE(regs[1] == 2);
    }
}

TEST_CASE("A hook sees the floating-point registers", "[reader][jit]") {
    //  0x00  mov  r0, #5
    //  0x04  vmov s0, r0
    //  0x08  vcvt.f32.s32 s0, s0  (s0 = 5.0)
    //  0x0c  vmov s3, r0          (s3 = the integer 5, as raw bits)
    //  0x10  vmov s31, r0
    //  0x14  mov  r1, #7          <- hooked
    //  0x18  b    0x18
    GuestMachine machine{
        {0xE3A00005, 0xEE000A10, 0xEEB80AC0, 0xEE010A90, 0xEE0F0A90, 0xE3A01007, 0xEAFFFFFE}};
    Recorder recorder;
    recorder.Hook(0x14);
    machine.Run();

    REQUIRE(recorder.hits.size() == 1);
    REQUIRE(recorder.hits[0].s[0] == 0x40A00000); // 5.0f
    REQUIRE(recorder.hits[0].s[3] == 5);
    REQUIRE(recorder.hits[0].s[31] == 5);
    REQUIRE(recorder.hits[0].s[1] == 0);
    REQUIRE(recorder.hits[0].r[0] == 5);
    REQUIRE(recorder.hits[0].r[1] == 0);
}

TEST_CASE("Thumb code is hooked the same way", "[reader][jit]") {
    //  0x00  movs r0, #5
    //  0x02  movs r1, #7
    //  0x04  adds r2, r0, r1      <- hooked
    //  0x06  b    0x06
    GuestMachine machine = GuestMachine::Thumb({0x2005, 0x2107, 0x1842, 0xE7FE});
    Recorder recorder;
    recorder.Hook(0x04);
    const auto regs = machine.Run();

    REQUIRE(recorder.hits.size() == 1);
    REQUIRE(recorder.hits[0].r[0] == 5);
    REQUIRE(recorder.hits[0].r[1] == 7);
    REQUIRE(recorder.hits[0].r[2] == 0);
    REQUIRE(recorder.hits[0].r[15] == 0x04);
    REQUIRE(regs[2] == 12);
}

TEST_CASE("A Thumb instruction inside an IT block is not hooked", "[reader][jit]") {
    //  0x00  movs r0, #5
    //  0x02  cmp  r0, #5
    //  0x04  ite  eq
    //  0x06  moveq r3, #1         <- hooked: conditional through the IT block
    //  0x08  movne r4, #1
    //  0x0a  movs r5, #9          <- hooked
    //  0x0c  b    0x0c
    GuestMachine machine =
        GuestMachine::Thumb({0x2005, 0x2805, 0xBF0C, 0x2301, 0x2401, 0x2509, 0xE7FE, 0xBF00});
    Recorder recorder;
    recorder.Hook(0x06);
    recorder.Hook(0x0A);
    const auto regs = machine.Run();

    REQUIRE(recorder.hits.size() == 1);
    REQUIRE(recorder.hits[0].r[15] == 0x0A);
    REQUIRE(recorder.hits[0].r[3] == 1);
    REQUIRE(recorder.hits[0].r[4] == 0);
    REQUIRE(recorder.Unusable(0x06) == 1);
    REQUIRE(regs[3] == 1);
    REQUIRE(regs[4] == 0);
    REQUIRE(regs[5] == 9);
}
