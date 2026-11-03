// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <optional>
#include <fmt/format.h>
#include "common/logging/log.h"
#include "common/settings.h"
#include "core/arm/arm_interface.h"
#include "core/core.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/memory.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/input_filter.h"

namespace Reader::Emu {

namespace {

/// The engine's way into the running game: the game's own process.
class ThreeDsHost final : public Host {
public:
    Core::System* system{};
    u32 process_id{};

    std::string ReadMemory(u64 address, std::size_t size) override {
        const auto process = AppProcess();
        if (!process || size == 0 || address > 0xFFFFFFFFULL) {
            return {};
        }
        auto& memory = system->Memory();
        std::size_t valid = 0;
        u64 cursor = address;
        while (valid < size && cursor <= 0xFFFFFFFFULL) {
            if (!memory.IsValidVirtualAddress(*process, static_cast<u32>(cursor))) {
                break;
            }
            const std::size_t in_page =
                Memory::CITRA_PAGE_SIZE - (cursor & Memory::CITRA_PAGE_MASK);
            const std::size_t take = std::min(size - valid, in_page);
            valid += take;
            cursor += take;
        }
        std::string out(valid, '\0');
        if (valid != 0) {
            memory.ReadBlock(*process, static_cast<u32>(address), out.data(), valid);
        }
        return out;
    }

    /// All or nothing: false where any of it is not mapped.
    bool WriteMemory(u64 address, const std::string& bytes) override {
        const auto process = AppProcess();
        if (!process || address + bytes.size() > 0x100000000ULL) {
            return false;
        }
        auto& memory = system->Memory();
        for (u64 page = address & ~static_cast<u64>(Memory::CITRA_PAGE_MASK);
             page < address + bytes.size(); page += Memory::CITRA_PAGE_SIZE) {
            if (!memory.IsValidVirtualAddress(*process,
                                              static_cast<u32>(std::max(page, address)))) {
                return false;
            }
        }
        memory.WriteBlock(*process, static_cast<u32>(address), bytes.data(), bytes.size());
        return true;
    }

    void InvalidateCode() override {
        if (!system || !system->IsPoweredOn()) {
            return;
        }
        // Each core keeps one JIT per page table and only exposes range invalidation for the
        // active one, so drop everything. This only happens when a profile is (un)loaded.
        for (u32 i = 0; i < system->GetNumCores(); ++i) {
            system->GetCore(i).ClearInstructionCache();
        }
    }

    std::vector<MemoryRegion> MemoryRegions() override {
        // Not looked up here: the harness's searches go by the console's fixed address ranges.
        return {};
    }

    bool RunningGameCode() override {
        if (!system) {
            return false;
        }
        // Other processes (applets, system modules) reuse the same virtual addresses.
        const auto current = system->Kernel().GetCurrentProcess();
        return current && current->process_id == process_id;
    }

    void Log(LogLevel level, const std::string& message) override {
        switch (level) {
        case LogLevel::Info:
            LOG_INFO(Core, "[reader] {}", message);
            break;
        case LogLevel::Warning:
            LOG_WARNING(Core, "[reader] {}", message);
            break;
        case LogLevel::Error:
            LOG_ERROR(Core, "[reader] {}", message);
            break;
        }
    }

private:
    [[nodiscard]] std::shared_ptr<Kernel::Process> AppProcess() const {
        if (!system || !system->IsPoweredOn()) {
            return nullptr;
        }
        return system->Kernel().GetProcessById(process_id);
    }
};

} // Anonymous namespace

Host& GameHost(Core::System& system, u32 process_id) {
    static ThreeDsHost host;
    host.system = &system;
    host.process_id = process_id;
    return host;
}

namespace {

/// The PadState bit of each of the emulator's NativeButtons the HID service reads into it, in
/// NativeButton order (A, B, X, Y, Up, Down, Left, Right, L, R, Start, Select, Debug, Gpio14).
constexpr std::array<u32, 14> PadBits{0, 1, 10, 11, 6, 7, 5, 4, 9, 8, 3, 2, 12, 13};

/// What the reader presses for the player, as PadState bits.
std::atomic<u32> g_pressed{};
/// The buttons as last read, and those whose press the reader's input filter took (their
/// release is its too).
u32 g_last_pad{};
u32 g_taken{};
/// The controller the filter remembers a passed-on press by.
const char g_pad_device = 0;

/// The PadState bit of one of the Android app's button numbers (console.h), or nothing.
std::optional<u32> BitOfButton(int button) {
    switch (button) {
    case 700:
        return 0; // A
    case 701:
        return 1; // B
    case 702:
        return 10; // X
    case 703:
        return 11; // Y
    case 704:
        return 3; // Start
    case 705:
        return 2; // Select
    case 773:
        return 9; // L
    case 774:
        return 8; // R
    case 709:
        return 6; // up
    case 710:
        return 7; // down
    case 711:
        return 5; // left
    case 712:
        return 4; // right
    default:
        return std::nullopt;
    }
}

} // Anonymous namespace

void PressButton(int button, bool down) {
    if (const auto bit = BitOfButton(button)) {
        if (down) {
            g_pressed.fetch_or(1U << *bit, std::memory_order_relaxed);
        } else {
            g_pressed.fetch_and(~(1U << *bit), std::memory_order_relaxed);
        }
    }
}

void RestSticks() {
    // The circle pad is read anew a few milliseconds from now, through FilterStick.
}

u32 FilterPad(u32 pad) {
    auto& filter = InputFilter::Instance();
    for (std::size_t index = 0; index < PadBits.size(); ++index) {
        const u32 bit = 1U << PadBits[index];
        const bool now = (pad & bit) != 0;
        if (now == ((g_last_pad & bit) != 0)) {
            continue;
        }
        const bool taken = filter.OnButton(&g_pad_device, index, now);
        if (now && taken) {
            g_taken |= bit;
        } else if (!now) {
            g_taken &= ~bit;
        }
    }
    g_last_pad = pad;
    return (pad & ~g_taken) | g_pressed.load(std::memory_order_relaxed);
}

namespace {
/// ZL and ZR as last handed over (bit 0, bit 1), and those whose press the filter took.
u32 g_last_extra{};
std::atomic<u32> g_extra_taken{};
} // namespace

void FilterExtraButtons(bool zl, bool zr) {
    auto& filter = InputFilter::Instance();
    const bool held[2]{zl, zr};
    for (u32 which = 0; which < 2; ++which) {
        const u32 bit = 1U << which;
        if (held[which] == ((g_last_extra & bit) != 0)) {
            continue;
        }
        const bool taken =
            filter.OnButton(&g_pad_device, Settings::NativeButton::ZL + which, held[which]);
        if (held[which] && taken) {
            g_extra_taken.fetch_or(bit, std::memory_order_relaxed);
        } else if (!held[which]) {
            g_extra_taken.fetch_and(~bit, std::memory_order_relaxed);
        }
    }
    g_last_extra = (zl ? 1U : 0U) | (zr ? 2U : 0U);
}

bool ExtraButtonTaken(int which) {
    return which >= 0 && which < 2 &&
           ((g_extra_taken.load(std::memory_order_relaxed) >> which) & 1U) != 0;
}

bool FilterStick(int stick, float x, float y) {
    return InputFilter::Instance().OnStick(static_cast<std::size_t>(stick), x, y);
}

std::string GameId(u64 title_id) {
    return fmt::format("{:016X}", title_id);
}

u64 TitleId(std::string_view game_id) {
    u64 value = 0;
    if (game_id.size() != 16) {
        return 0;
    }
    const auto [end, error] =
        std::from_chars(game_id.data(), game_id.data() + game_id.size(), value, 16);
    return error == std::errc{} && end == game_id.data() + game_id.size() ? value : 0;
}

} // namespace Reader::Emu
