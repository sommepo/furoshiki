// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <algorithm>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <map>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fmt/format.h>
#include "common/common_types.h"
#include "reader/core/engine.h"
#include "reader_test_paths.h"

namespace ReaderTest {

/// Sparse guest memory. A read stops at the first byte that was never written, like a read that
/// runs into an unmapped page.
class FakeMemory {
public:
    void Write(u32 address, std::span<const u8> data) {
        for (std::size_t i = 0; i < data.size(); ++i) {
            bytes[address + static_cast<u32>(i)] = data[i];
        }
    }
    void Write(u32 address, std::string_view data) {
        Write(address, std::span<const u8>{reinterpret_cast<const u8*>(data.data()), data.size()});
    }
    void Write32(u32 address, u32 value) {
        const u8 raw[4]{static_cast<u8>(value), static_cast<u8>(value >> 8),
                        static_cast<u8>(value >> 16), static_cast<u8>(value >> 24)};
        Write(address, raw);
    }
    void Unmap(u32 address, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes.erase(address + static_cast<u32>(i));
        }
    }
    [[nodiscard]] std::string Read(u32 address, std::size_t size) const {
        std::string out;
        auto it = bytes.find(address);
        while (out.size() < size && it != bytes.end() &&
               it->first == address + static_cast<u32>(out.size())) {
            out.push_back(static_cast<char>(it->second));
            ++it;
        }
        return out;
    }

private:
    std::map<u32, u8> bytes;
};

/// The engine running without an emulated system, on FakeMemory, with everything it reports
/// collected. Restores the engine when it goes out of scope.
struct DetachedEngine {
    explicit DetachedEngine(u64 title_id = 0) : engine{Reader::Engine::Instance()} {
        engine.StartDetached(
            [this](u64 address, std::size_t size) {
                return address > 0xFFFFFFFFULL ? std::string{}
                                               : memory.Read(static_cast<u32>(address), size);
            },
            // The engine names a game as the reader does: its title ID in sixteen capitals.
            title_id != 0 ? fmt::format("{:016X}", title_id) : std::string{});
        engine.SetEventCallback(
            [this](const Reader::TextEvent& event) { events.push_back(event); });
        engine.SetLayoutCallback(
            [this](const Reader::TextLayout& layout) { layouts.push_back(layout.ToJson()); });
        engine.SetLogCallback([this](Reader::LogLevel level, const std::string& message) {
            logs.emplace_back(level, message);
        });
    }
    ~DetachedEngine() {
        engine.StopDetached();
        engine.SetEventCallback(nullptr);
        engine.SetLayoutCallback(nullptr);
        engine.SetLogCallback(nullptr);
        engine.SetDataProvider(nullptr);
    }
    DetachedEngine(const DetachedEngine&) = delete;
    DetachedEngine& operator=(const DetachedEngine&) = delete;

    /// Runs the handlers of a hooked address, as the JIT would. `floats` are S registers.
    void Fire(u32 pc, std::initializer_list<std::pair<int, u32>> registers = {},
              std::initializer_list<std::pair<int, float>> floats = {}) {
        Reader::GuestRegisters regs;
        for (const auto& [index, value] : registers) {
            regs.r[static_cast<std::size_t>(index)] = value;
        }
        for (const auto& [index, value] : floats) {
            std::memcpy(&regs.s[static_cast<std::size_t>(index)], &value, sizeof(value));
        }
        regs.r[15] = pc;
        if (engine.IsHooked(pc)) {
            engine.Dispatch(regs);
        }
    }

    /// Runs Lua in the profile's state and returns what it printed, or the error.
    std::string Lua(const std::string& code) {
        std::string output;
        const bool ok = engine.Execute(code, output);
        return ok ? output : "ERROR: " + output;
    }

    [[nodiscard]] std::vector<Reader::TextEvent> TakeEvents() {
        return std::exchange(events, {});
    }
    [[nodiscard]] std::vector<std::string> TakeLayouts() {
        return std::exchange(layouts, {});
    }

    /// Number of log messages of a level that contain `needle`.
    [[nodiscard]] std::size_t CountLogs(Reader::LogLevel level, std::string_view needle) const {
        return static_cast<std::size_t>(
            std::count_if(logs.begin(), logs.end(), [&](const auto& entry) {
                return entry.first == level && entry.second.find(needle) != std::string::npos;
            }));
    }

    Reader::Engine& engine;
    FakeMemory memory;
    std::vector<Reader::TextEvent> events;
    std::vector<std::string> layouts; ///< As JSON, in the order they were reported
    std::vector<std::pair<Reader::LogLevel, std::string>> logs;
};

/// Source of a profile bundled with the Android app.
inline std::string BundledProfile(const std::string& title_id) {
    std::ifstream file{std::string{READER_BUNDLED_PROFILE_DIR} + "/" + title_id + ".lua",
                       std::ios::binary};
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

} // namespace ReaderTest
