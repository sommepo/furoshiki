// SPDX-FileCopyrightText: melonDS Reader fork contributors (from the PPSSPP and Flycast Reader forks)
// SPDX-License-Identifier: GPL-2.0-or-later

// The headless harness: runs a DS game on a machine with no display, GPU or sound device and
// takes text commands on a socket. It is how text profiles are written and how the text engine
// is tried against a real game (docs/reader/harness.md). It is a small libretro frontend of its
// own around melonDS's libretro build (the same melonDS as the core's), with the reader's engine
// in place of Furoshiki's.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "libretro.h"
#include "ARM.h"
#include "NDS.h"
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#include "reader/core/screens.h"
#include "reader/core/text_codec.h"

namespace {

// ---- The libretro frontend ----

std::string g_system_dir, g_saves_dir;
std::map<std::string, std::string> g_options;
std::vector<uint32_t> g_picture;
unsigned g_picture_width = 0, g_picture_height = 0;
uint32_t g_buttons = 0; ///< libretro's joypad bits held now
uint64_t g_frames = 0;
std::vector<int16_t> g_audio;   ///< kept while `audio on`
bool g_audio_on = false;
uint64_t g_audio_frames = 0;    ///< stereo frames since the start

void CoreLog(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_WARN && !std::getenv("READER_HARNESS_LOG")) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);
}

bool Environment(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *static_cast<const char**>(data) = g_system_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *static_cast<const char**>(data) = g_saves_dir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* var = static_cast<retro_variable*>(data);
        const auto found = g_options.find(var->key);
        var->value = found != g_options.end() ? found->second.c_str() : nullptr;
        return var->value != nullptr;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *static_cast<bool*>(data) = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        return *static_cast<const retro_pixel_format*>(data) == RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        static_cast<retro_log_callback*>(data)->log = &CoreLog;
        return true;
    default:
        return false;
    }
}

void Video(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (data == nullptr) {
        return;
    }
    g_picture_width = width;
    g_picture_height = height;
    g_picture.resize(static_cast<std::size_t>(width) * height);
    for (unsigned y = 0; y < height; ++y) {
        std::memcpy(g_picture.data() + static_cast<std::size_t>(y) * width,
                    static_cast<const char*>(data) + y * pitch, width * 4);
    }
}

size_t AudioBatch(const int16_t* data, size_t frames) {
    g_audio_frames += frames;
    if (g_audio_on) {
        g_audio.insert(g_audio.end(), data, data + frames * 2);
    }
    return frames;
}

void InputPoll() {}

int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) {
        return 0;
    }
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        return static_cast<int16_t>(g_buttons);
    }
    return (g_buttons >> id) & 1;
}

// ---- Small helpers ----

std::string ReadFile(const std::filesystem::path& path, bool& ok) {
    std::ifstream file(path, std::ios::binary);
    ok = static_cast<bool>(file);
    return ok ? std::string(std::istreambuf_iterator<char>(file), {}) : std::string();
}

std::string BytesFromHex(const std::string& hex) {
    std::string bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

std::string Hex(const std::string& bytes) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : bytes) {
        out += digits[static_cast<u8>(c) >> 4];
        out += digits[static_cast<u8>(c) & 15];
    }
    return out;
}

/// UTF-8 to Shift-JIS by the decoder's table turned round; empty where a character has none.
std::string ToSjis(const std::string& utf8) {
    static const std::map<std::string, std::string> table = [] {
        std::map<std::string, std::string> map;
        for (u32 lead = 0x81; lead <= 0xFC; ++lead) {
            if (lead >= 0xA0 && lead <= 0xDF) {
                continue;
            }
            for (u32 trail = 0x40; trail <= 0xFC; ++trail) {
                const u8 pair[2]{static_cast<u8>(lead), static_cast<u8>(trail)};
                const std::string decoded = Reader::SjisToUtf8(pair);
                if (decoded != "\xEF\xBF\xBD" && Reader::CountCodePoints(decoded) == 1) {
                    map.emplace(decoded, std::string(reinterpret_cast<const char*>(pair), 2));
                }
            }
        }
        return map;
    }();
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const u8 lead = static_cast<u8>(utf8[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        const std::string one = utf8.substr(i, length);
        i += length;
        if (length == 1) {
            out += one;
            continue;
        }
        const auto it = table.find(one);
        if (it == table.end()) {
            return {};
        }
        out += it->second;
    }
    return out;
}

std::string ToUtf16(const std::string& utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size();) {
        const u8 lead = static_cast<u8>(utf8[i]);
        const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
        u32 cp = length == 1 ? lead : lead & (0xFF >> (length + 1));
        for (std::size_t k = 1; k < length && i + k < utf8.size(); ++k) {
            cp = (cp << 6) | (static_cast<u8>(utf8[i + k]) & 0x3F);
        }
        i += length;
        out.push_back(static_cast<char>(cp & 0xFF));
        out.push_back(static_cast<char>((cp >> 8) & 0xFF));
    }
    return out;
}

// A PNG without a compression library: deflate's stored blocks.
u32 Crc(const u8* data, std::size_t size, u32 crc = 0xFFFFFFFFu) {
    static u32 table[256];
    static bool made = false;
    if (!made) {
        for (u32 n = 0; n < 256; ++n) {
            u32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        made = true;
    }
    for (std::size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

bool WritePng(const std::filesystem::path& path, const std::vector<uint32_t>& pixels, unsigned width,
              unsigned height) {
    std::string raw;
    raw.reserve((width * 3 + 1) * height);
    for (unsigned y = 0; y < height; ++y) {
        raw.push_back(0);
        for (unsigned x = 0; x < width; ++x) {
            const u32 c = pixels[static_cast<std::size_t>(y) * width + x];
            raw.push_back(static_cast<char>((c >> 16) & 0xFF));
            raw.push_back(static_cast<char>((c >> 8) & 0xFF));
            raw.push_back(static_cast<char>(c & 0xFF));
        }
    }
    std::string z = "\x78\x01";
    u32 a = 1, b = 0;
    for (const char c : raw) {
        a = (a + static_cast<u8>(c)) % 65521;
        b = (b + a) % 65521;
    }
    for (std::size_t at = 0; at < raw.size() || at == 0;) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(static_cast<char>(n & 0xFF));
        z.push_back(static_cast<char>(n >> 8));
        z.push_back(static_cast<char>(~n & 0xFF));
        z.push_back(static_cast<char>((~n >> 8) & 0xFF));
        z.append(raw, at, n);
        at += n;
        if (last) {
            break;
        }
    }
    const u32 adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) {
        z.push_back(static_cast<char>((adler >> s) & 0xFF));
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out.write("\x89PNG\r\n\x1a\n", 8);
    const auto chunk = [&out](const char* type, const std::string& data) {
        const u32 size = static_cast<u32>(data.size());
        const char be[4] = {static_cast<char>(size >> 24), static_cast<char>(size >> 16), static_cast<char>(size >> 8),
                            static_cast<char>(size)};
        out.write(be, 4);
        std::string body = std::string(type, 4) + data;
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        const u32 crc = Crc(reinterpret_cast<const u8*>(body.data()), body.size()) ^ 0xFFFFFFFFu;
        const char crc_be[4] = {static_cast<char>(crc >> 24), static_cast<char>(crc >> 16), static_cast<char>(crc >> 8),
                                static_cast<char>(crc)};
        out.write(crc_be, 4);
    };
    std::string header;
    for (const u32 v : {width, height}) {
        header += static_cast<char>(v >> 24);
        header += static_cast<char>(v >> 16);
        header += static_cast<char>(v >> 8);
        header += static_cast<char>(v);
    }
    header += std::string("\x08\x02\x00\x00\x00", 5);
    chunk("IHDR", header);
    chunk("IDAT", z);
    chunk("IEND", "");
    return static_cast<bool>(out);
}

bool WriteWav(const std::filesystem::path& path, const int16_t* samples, std::size_t frames) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    const auto u32le = [&out](u32 v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16le = [&out](u16 v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const u32 bytes = static_cast<u32>(frames * 4);
    out.write("RIFF", 4);
    u32le(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32le(16);
    u16le(1);
    u16le(2);
    u32le(32768);
    u32le(32768 * 4);
    u16le(4);
    u16le(16);
    out.write("data", 4);
    u32le(bytes);
    out.write(reinterpret_cast<const char*>(samples), bytes);
    return static_cast<bool>(out);
}

const std::map<std::string, unsigned> ButtonNames = {
    {"a", RETRO_DEVICE_ID_JOYPAD_A},         {"b", RETRO_DEVICE_ID_JOYPAD_B},
    {"x", RETRO_DEVICE_ID_JOYPAD_X},         {"y", RETRO_DEVICE_ID_JOYPAD_Y},
    {"l", RETRO_DEVICE_ID_JOYPAD_L},         {"r", RETRO_DEVICE_ID_JOYPAD_R},
    {"start", RETRO_DEVICE_ID_JOYPAD_START}, {"select", RETRO_DEVICE_ID_JOYPAD_SELECT},
    {"up", RETRO_DEVICE_ID_JOYPAD_UP},       {"down", RETRO_DEVICE_ID_JOYPAD_DOWN},
    {"left", RETRO_DEVICE_ID_JOYPAD_LEFT},   {"right", RETRO_DEVICE_ID_JOYPAD_RIGHT},
    {"lid", RETRO_DEVICE_ID_JOYPAD_L3},      {"mic", RETRO_DEVICE_ID_JOYPAD_L2},
};

// ---- The engine's view of the DS ----

class DsHost final : public Reader::Host {
public:
    std::string ReadMemory(u64 address, std::size_t size) override {
        std::string bytes(size, '\0');
        bytes.resize(Reader::Emu::ReadMemory(address, bytes.data(), size));
        return bytes;
    }
    bool WriteMemory(u64 address, const std::string& bytes) override {
        return Reader::Emu::WriteMemory(address, bytes.data(), bytes.size());
    }
    void InvalidateCode() override {
        Reader::Emu::HooksChanged(Reader::Engine::Instance().HookCount() > 0);
    }
    std::vector<Reader::MemoryRegion> MemoryRegions() override {
        std::vector<Reader::MemoryRegion> out;
        for (const auto& r : Reader::Emu::MemoryRegions()) {
            out.push_back({r.base, r.size, true, r.writable, r.executable, r.state});
        }
        return out;
    }
};

DsHost g_host;

const Reader::Emu::Sink g_sink{
    [](u32 pc) { return Reader::Engine::Instance().IsHooked(pc); },
    [](const u32 registers[16]) {
        Reader::GuestRegisters regs;
        std::copy(registers, registers + 16, regs.r.begin());
        Reader::Engine::Instance().Dispatch(regs);
    },
    [] {
        Reader::Engine::Instance().OnPicture(256, 192);
        Reader::Engine::Instance().OnFrame();
    },
};

struct Reply {
    bool ok = true;
    std::string text;
    std::string Format() const {
        std::string out = text;
        if (!out.empty() && out.back() != '\n') {
            out += '\n';
        }
        return out + (ok ? "OK\n" : "ERR " + error + "\n");
    }
    std::string error;
    static Reply Error(std::string why) {
        Reply reply;
        reply.ok = false;
        reply.error = std::move(why);
        return reply;
    }
};

class Harness {
public:
    std::atomic<bool> quit{false};

    bool Boot(const std::filesystem::path& rom, const std::filesystem::path& home) {
        std::filesystem::create_directories(home / "system");
        std::filesystem::create_directories(home / "saves");
        g_system_dir = (home / "system").string();
        g_saves_dir = (home / "saves").string();
        g_options = {
            {"melonds_console_mode", "DS"},         {"melonds_boot_directly", "enabled"},
            {"melonds_screen_layout", "Top/Bottom"}, {"melonds_screen_gap", "0"},
            {"melonds_touch_mode", "Touch"},        {"melonds_threaded_renderer", "disabled"},
            {"melonds_language", "Japanese"},       {"melonds_use_fw_settings", "disabled"},
            {"melonds_audio_interpolation", "None"}, {"melonds_reader_rotation", "none"},
            {"melonds_swapscreen_mode", "Hold"},
        };
        if (const char* rotation = std::getenv("READER_HARNESS_ROTATION")) {
            g_options["melonds_reader_rotation"] = rotation;
        }

        auto& engine = Reader::Engine::Instance();
        engine.SetEventCallback([this](const Reader::TextEvent& event) {
            std::scoped_lock lock{output_mutex};
            events.push_back(event.ToJson());
        });
        engine.SetLayoutCallback([this](const Reader::TextLayout& layout) {
            std::scoped_lock lock{output_mutex};
            layouts.push_back(layout.ToJson());
        });
        engine.SetLogCallback([this](Reader::LogLevel level, const std::string& message) {
            std::scoped_lock lock{output_mutex};
            const char* prefix = level == Reader::LogLevel::Error     ? "error: "
                                 : level == Reader::LogLevel::Warning ? "warning: "
                                                                      : "";
            logs.push_back(std::string("[reader] ") + prefix + message);
        });
        engine.SetDataProvider([this](const std::string& name) -> std::optional<std::string> {
            bool ok = false;
            std::string data = ReadFile(profile_dir / (Reader::Engine::Instance().GameId() + "." + name), ok);
            return ok ? std::optional<std::string>(std::move(data)) : std::nullopt;
        });

        retro_set_environment(&Environment);
        retro_set_video_refresh(&Video);
        retro_set_audio_sample_batch(&AudioBatch);
        retro_set_input_poll(&InputPoll);
        retro_set_input_state(&InputState);
        retro_init();

        bool ok = false;
        rom_data = ReadFile(rom, ok);
        if (!ok) {
            std::cerr << "cannot read " << rom << "\n";
            return false;
        }
        rom_path = rom.string();
        retro_game_info game{};
        game.path = rom_path.c_str();
        game.data = rom_data.data();
        game.size = rom_data.size();
        engine.OnGameLoading(g_host);
        if (!retro_load_game(&game)) {
            std::cerr << "the game did not load: " << rom << "\n";
            return false;
        }
        Reader::Emu::SetSink(&g_sink);
        engine.OnGameLoaded(Reader::Emu::GameId());
        std::cerr << "game " << Reader::Emu::GameId() << " " << Reader::Emu::GameTitle() << std::endl;
        return true;
    }

    void Shutdown() {
        Reader::Engine::Instance().OnShutdown();
        retro_unload_game();
        retro_deinit();
    }

    void RunFrames(uint64_t n, double timeout = 0) {
        const auto start = std::chrono::steady_clock::now();
        for (uint64_t i = 0; i < n && !quit; ++i) {
            if (touching) {
                NDS::TouchScreen(touch_x, touch_y);
            }
            retro_run();
            ++g_frames;
            if (timeout > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() > timeout) {
                break;
            }
        }
    }

    Reply Execute(const std::string& line) {
        std::istringstream in{line};
        std::string cmd;
        in >> cmd;
        if (cmd.empty() || cmd[0] == '#') {
            return {};
        }
        try {
            return Command(cmd, in, line);
        } catch (const std::exception& e) {
            return Reply::Error(e.what());
        }
    }

    std::string TakeLogs() {
        std::scoped_lock lock{output_mutex};
        std::string out;
        for (const auto& l : logs) {
            out += l + "\n";
        }
        logs.clear();
        return out;
    }

private:
    Reply Command(const std::string& cmd, std::istringstream& in, const std::string& line) {
        auto& engine = Reader::Engine::Instance();
        const auto rest = [&]() {
            std::string text;
            std::getline(in, text);
            const auto start = text.find_first_not_of(' ');
            return start == std::string::npos ? std::string() : text.substr(start);
        };
        Reply reply;
        if (cmd == "run") {
            uint64_t n = 1;
            double timeout = 0;
            in >> n >> timeout;
            RunFrames(n, timeout);
            reply.text = TakeLogs();
        } else if (cmd == "wait") {
            double seconds = 1;
            in >> seconds;
            RunFrames(static_cast<uint64_t>(seconds * 59.8261));
            reply.text = TakeLogs();
        } else if (cmd == "press") {
            std::string name;
            uint64_t hold = 4, after = 4;
            in >> name;
            std::string word;
            while (in >> word) {
                if (word.rfind("hold=", 0) == 0) hold = std::stoull(word.substr(5));
                else if (word.rfind("after=", 0) == 0) after = std::stoull(word.substr(6));
            }
            const auto found = ButtonNames.find(name);
            if (found == ButtonNames.end()) {
                return Reply::Error("no button " + name);
            }
            g_buttons |= 1u << found->second;
            RunFrames(hold);
            g_buttons &= ~(1u << found->second);
            RunFrames(after);
            reply.text = TakeLogs();
        } else if (cmd == "hold" || cmd == "release") {
            std::string name;
            in >> name;
            const auto found = ButtonNames.find(name);
            if (found == ButtonNames.end()) {
                return Reply::Error("no button " + name);
            }
            if (cmd == "hold") g_buttons |= 1u << found->second;
            else g_buttons &= ~(1u << found->second);
        } else if (cmd == "touch") {
            // A point of the lower screen, in its own 256x192, whatever the picture's turn.
            int x = 0, y = 0;
            uint64_t hold = 4, after = 4;
            in >> x >> y;
            std::string word;
            while (in >> word) {
                if (word.rfind("hold=", 0) == 0) hold = std::stoull(word.substr(5));
                else if (word.rfind("after=", 0) == 0) after = std::stoull(word.substr(6));
            }
            touching = true;
            touch_x = static_cast<u16>(std::clamp(x, 0, 255));
            touch_y = static_cast<u16>(std::clamp(y, 0, 191));
            RunFrames(hold);
            touching = false;
            NDS::ReleaseScreen();
            RunFrames(after);
            reply.text = TakeLogs();
        } else if (cmd == "drag") {
            // A finger drawn across the lower screen: drag X1 Y1 X2 Y2 [frames=20] [after=4].
            int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            uint64_t frames = 20, after = 4;
            in >> x1 >> y1 >> x2 >> y2;
            std::string word;
            while (in >> word) {
                if (word.rfind("frames=", 0) == 0) frames = std::max<uint64_t>(1, std::stoull(word.substr(7)));
                else if (word.rfind("after=", 0) == 0) after = std::stoull(word.substr(6));
            }
            touching = true;
            for (uint64_t i = 0; i <= frames; ++i) {
                const double t = static_cast<double>(i) / static_cast<double>(frames);
                touch_x = static_cast<u16>(std::clamp(static_cast<int>(x1 + (x2 - x1) * t), 0, 255));
                touch_y = static_cast<u16>(std::clamp(static_cast<int>(y1 + (y2 - y1) * t), 0, 191));
                RunFrames(1);
            }
            touching = false;
            NDS::ReleaseScreen();
            RunFrames(after);
            reply.text = TakeLogs();
        } else if (cmd == "shot") {
            std::string path;
            in >> path;
            if (g_picture.empty()) {
                return Reply::Error("no picture yet");
            }
            if (!WritePng(path, g_picture, g_picture_width, g_picture_height)) {
                return Reply::Error("cannot write " + path);
            }
            reply.text = std::to_string(g_picture_width) + "x" + std::to_string(g_picture_height);
        } else if (cmd == "events" || cmd == "layouts" || cmd == "logs") {
            std::scoped_lock lock{output_mutex};
            auto& list = cmd == "events" ? events : cmd == "layouts" ? layouts : logs;
            for (const auto& item : list) {
                reply.text += item + "\n";
            }
            list.clear();
        } else if (cmd == "profile") {
            const std::filesystem::path path = rest();
            bool ok = false;
            const std::string source = ReadFile(path, ok);
            if (!ok) {
                return Reply::Error("cannot read " + path.string());
            }
            {
                std::scoped_lock lock{output_mutex};
                profile_dir = path.parent_path();
            }
            if (!engine.LoadProfile(path.filename().string(), source)) {
                return Reply::Error(engine.LastError());
            }
            reply.text = TakeLogs();
        } else if (cmd == "unload") {
            engine.UnloadProfile();
        } else if (cmd == "actions") {
            int i = 0;
            for (const auto& label : engine.Actions()) {
                reply.text += std::to_string(i++) + " " + label + "\n";
            }
        } else if (cmd == "action") {
            std::size_t index = 0;
            in >> index;
            if (const auto said = engine.RunAction(index)) {
                reply.text = *said;
            }
        } else if (cmd == "lua") {
            std::string output;
            const bool ok = engine.Execute(rest(), output);
            if (!ok) {
                return Reply::Error(output);
            }
            reply.text = output + "\n" + TakeLogs();
        } else if (cmd == "mem" || cmd == "memfile") {
            std::string address_text;
            std::size_t size = 256;
            std::string path;
            in >> address_text >> size >> path;
            const u64 address = std::stoull(address_text, nullptr, 16);
            std::string bytes(size, '\0');
            bytes.resize(Reader::Emu::ReadMemory(address, bytes.data(), size));
            if (cmd == "memfile") {
                std::ofstream out(path, std::ios::binary);
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                reply.text = std::to_string(bytes.size()) + " bytes";
            } else {
                char text[16];
                for (std::size_t i = 0; i < bytes.size(); i += 16) {
                    std::snprintf(text, sizeof(text), "%08llX ", static_cast<unsigned long long>(address + i));
                    reply.text += text + Hex(bytes.substr(i, 16)) + "\n";
                }
            }
        } else if (cmd == "find" || cmd == "findtext" || cmd == "findptr") {
            std::vector<std::pair<std::string, std::string>> needles; // what, as which
            u64 start = 0, end = 0xFFFFFFFFull;
            if (cmd == "find") {
                std::string hex, a, b;
                in >> hex >> a >> b;
                needles.push_back({BytesFromHex(hex), "hex"});
                if (!a.empty()) start = std::stoull(a, nullptr, 16);
                if (!b.empty()) end = std::stoull(b, nullptr, 16);
            } else if (cmd == "findtext") {
                const std::string text = rest();
                needles.push_back({text, "utf8"});
                needles.push_back({ToUtf16(text), "utf16"});
                if (const std::string sjis = ToSjis(text); !sjis.empty() && sjis != text) {
                    needles.push_back({sjis, "sjis"});
                }
            } else {
                std::string a, b;
                in >> a >> b;
                const u64 low = std::stoull(a, nullptr, 16), high = std::stoull(b, nullptr, 16);
                std::size_t found = 0;
                for (const auto& region : Reader::Emu::MemoryRegions()) {
                    std::string bytes(region.size, '\0');
                    bytes.resize(Reader::Emu::ReadMemory(region.base, bytes.data(), region.size));
                    for (std::size_t i = 0; i + 4 <= bytes.size(); i += 4) {
                        u32 value;
                        std::memcpy(&value, bytes.data() + i, 4);
                        if (value >= low && value < high && found++ < 400) {
                            char text[48];
                            std::snprintf(text, sizeof(text), "%08llX -> %08X\n",
                                          static_cast<unsigned long long>(region.base + i), value);
                            reply.text += text;
                        }
                    }
                }
                reply.text += std::to_string(found) + " found";
                return reply;
            }
            std::size_t found = 0;
            for (const auto& region : Reader::Emu::MemoryRegions()) {
                if (region.base + region.size <= start || region.base >= end) {
                    continue;
                }
                std::string bytes(region.size, '\0');
                bytes.resize(Reader::Emu::ReadMemory(region.base, bytes.data(), region.size));
                for (const auto& [needle, kind] : needles) {
                    if (needle.empty()) {
                        continue;
                    }
                    for (std::size_t at = bytes.find(needle); at != std::string::npos; at = bytes.find(needle, at + 1)) {
                        const u64 address = region.base + at;
                        if (address < start || address >= end) {
                            continue;
                        }
                        if (found++ < 400) {
                            char text[48];
                            std::snprintf(text, sizeof(text), "%08llX %s\n", static_cast<unsigned long long>(address),
                                          kind.c_str());
                            reply.text += text;
                        }
                    }
                }
            }
            reply.text += std::to_string(found) + " found";
        } else if (cmd == "poke") {
            std::string address_text, hex;
            in >> address_text >> hex;
            const std::string bytes = BytesFromHex(hex);
            if (!Reader::Emu::WriteMemory(std::stoull(address_text, nullptr, 16), bytes.data(), bytes.size())) {
                return Reply::Error("not writable");
            }
        } else if (cmd == "maps") {
            for (const auto& r : Reader::Emu::MemoryRegions()) {
                char text[96];
                std::snprintf(text, sizeof(text), "%08llX %08llX %s%s state %u\n", static_cast<unsigned long long>(r.base),
                              static_cast<unsigned long long>(r.size), r.writable ? "w" : "-", r.executable ? "x" : "-",
                              r.state);
                reply.text += text;
            }
        } else if (cmd == "regs") {
            // The ARM9's registers now (between frames).
            char text[64];
            for (int i = 0; i < 16; ++i) {
                std::snprintf(text, sizeof(text), "r%d %08X\n", i, NDS::ARM9->R[i]);
                reply.text += text;
            }
        } else if (cmd == "audio") {
            std::string what;
            in >> what;
            if (what == "on") {
                g_audio_on = true;
                g_audio.clear();
                audio_start = g_audio_frames;
            } else if (what == "off") {
                g_audio_on = false;
            }
            reply.text = std::to_string(g_audio_frames);
        } else if (cmd == "wav") {
            uint64_t from = 0, to = 0;
            std::string path;
            in >> from >> to >> path;
            if (to == 0) to = g_audio_frames;
            if (!g_audio_on || from < audio_start || to < from || (to - audio_start) * 2 > g_audio.size()) {
                return Reply::Error("not kept (audio on first)");
            }
            if (!WriteWav(path, g_audio.data() + (from - audio_start) * 2, to - from)) {
                return Reply::Error("cannot write " + path);
            }
        } else if (cmd == "save" || cmd == "load") {
            // A savestate in a file.
            std::string path;
            in >> path;
            if (cmd == "save") {
                std::string state(retro_serialize_size(), '\0');
                if (!retro_serialize(state.data(), state.size())) {
                    return Reply::Error("no savestate");
                }
                std::ofstream out(path, std::ios::binary);
                out.write(state.data(), static_cast<std::streamsize>(state.size()));
            } else {
                bool ok = false;
                std::string state = ReadFile(path, ok);
                if (!ok || !retro_unserialize(state.data(), state.size())) {
                    return Reply::Error("cannot load " + path);
                }
                engine.OnStateLoaded();
            }
        } else if (cmd == "status") {
            reply.text = "frame " + std::to_string(g_frames) + "\ngame " + Reader::Emu::GameId() + " " +
                         Reader::Emu::GameTitle() + "\nprofile " + (engine.HasProfile() ? "yes" : "no") + "\nhooks " +
                         std::to_string(engine.HookCount()) + "\nrotation " +
                         std::to_string(Reader::Screens::Rotation()) + "\npicture " + std::to_string(g_picture_width) +
                         "x" + std::to_string(g_picture_height);
        } else if (cmd == "quit") {
            quit = true;
        } else {
            (void)line;
            return Reply::Error("unknown command " + cmd);
        }
        return reply;
    }

    std::mutex output_mutex;
    std::vector<std::string> events, layouts, logs;
    std::filesystem::path profile_dir;
    std::string rom_data, rom_path;
    bool touching = false;
    u16 touch_x = 0, touch_y = 0;
    uint64_t audio_start = 0;
};

bool WriteAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::write(fd, data.data() + sent, data.size() - sent);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void ServeSocket(Harness& harness, const std::string& path) {
    ::unlink(path.c_str());
    const int server = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (server < 0 || path.size() >= sizeof(address.sun_path)) {
        std::cerr << "cannot create socket " << path << "\n";
        return;
    }
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::chmod(path.c_str(), 0600) != 0 || ::listen(server, 4) != 0) {
        std::cerr << "cannot listen on " << path << ": " << std::strerror(errno) << "\n";
        ::close(server);
        return;
    }
    std::cerr << "listening on " << path << std::endl;
    while (!harness.quit) {
        const int client = ::accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        std::string request;
        char buffer[4096];
        ssize_t n;
        while ((n = ::read(client, buffer, sizeof(buffer))) > 0) {
            request.append(buffer, static_cast<std::size_t>(n));
        }
        std::istringstream lines{request};
        std::string line, response;
        while (!harness.quit && std::getline(lines, line)) {
            response += harness.Execute(line).Format();
        }
        WriteAll(client, response);
        ::close(client);
    }
    ::close(server);
    ::unlink(path.c_str());
}

int Usage() {
    std::cerr << "usage: melonds-reader-harness --rom FILE --home DIR [--socket PATH] [--script FILE]\n"
                 "           [--profile FILE]\n"
                 "Runs a DS game without a display and takes commands on a socket (or, without\n"
                 "--socket, on standard input). See docs/reader/harness.md.\n";
    return 64;
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::filesystem::path rom, home, script, profile;
    std::string socket_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--rom") rom = value();
        else if (arg == "--home") home = value();
        else if (arg == "--socket") socket_path = value();
        else if (arg == "--script") script = value();
        else if (arg == "--profile") profile = value();
        else return Usage();
    }
    if (rom.empty() || home.empty()) {
        return Usage();
    }
    Harness harness;
    if (!harness.Boot(std::filesystem::absolute(rom), std::filesystem::absolute(home))) {
        return 1;
    }
    if (!profile.empty()) {
        std::cout << harness.Execute("profile " + profile.string()).Format();
    }
    if (!script.empty()) {
        std::ifstream file{script};
        std::string line;
        while (!harness.quit && std::getline(file, line)) {
            std::cout << harness.Execute(line).Format() << std::flush;
        }
    }
    if (!socket_path.empty()) {
        ServeSocket(harness, socket_path);
    } else if (script.empty()) {
        std::string line;
        while (!harness.quit && std::getline(std::cin, line)) {
            std::cout << harness.Execute(line).Format() << std::flush;
        }
    }
    harness.Shutdown();
    return 0;
}
