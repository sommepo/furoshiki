// Copyright Azahar Reader fork contributors
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

// Headless development harness for the reader fork.
//
// Boots a game with the software renderer and no audio or window, then executes simple text
// commands (run frames, press buttons, take screenshots, read memory, load a text profile).
// It exists so that text profiles can be developed and tested on a machine without a display,
// and so the text-hook engine has an end-to-end test against a real game.
//
//   azahar-reader-harness --user-dir DIR --rom FILE [--socket PATH] [--script FILE]
//
// Commands arrive one per line from --script, from a Unix socket (one command per connection)
// or from standard input. Every reply ends with a line reading "OK" or "ERR <message>".

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <fmt/format.h>
#include <lodepng.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "audio_core/sink_details.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/param_package.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/image_interface.h"
#include "core/frontend/input.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/service/service.h"
#include "core/loader/loader.h"
#include "reader/core/audio_tap.h"
#include "reader/core/engine.h"
#include "video_core/gpu.h"
#include "video_core/renderer_software/renderer_software.h"

namespace {

// ---------------------------------------------------------------------------------------------
// Window and input
// ---------------------------------------------------------------------------------------------

constexpr unsigned TopWidth = 400;
constexpr unsigned BottomWidth = 320;
constexpr unsigned ScreenHeight = 240;
constexpr unsigned BottomLeft = (TopWidth - BottomWidth) / 2;

class HeadlessWindow final : public Frontend::EmuWindow {
public:
    HeadlessWindow() {
        // The default layout at native size: top screen above the bottom screen.
        UpdateCurrentFramebufferLayout(TopWidth, ScreenHeight * 2, false);
    }

    /// The software renderer calls this once per emulated frame.
    void PollEvents() override {
        ++frames;
    }

    u64 frames = 0;
};

struct InputState {
    std::array<std::atomic<bool>, Settings::NativeButton::NumButtons> buttons{};
    std::atomic<float> circle_x{0.0f};
    std::atomic<float> circle_y{0.0f};
};

InputState g_input;

class HarnessButton final : public Input::ButtonDevice {
public:
    explicit HarnessButton(int index_) : index{index_} {}
    bool GetStatus() const override {
        return index >= 0 && index < static_cast<int>(g_input.buttons.size()) &&
               g_input.buttons[index].load();
    }

private:
    int index;
};

class HarnessButtonFactory final : public Input::Factory<Input::ButtonDevice> {
public:
    std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<HarnessButton>(params.Get("button", -1));
    }
};

class HarnessAnalog final : public Input::AnalogDevice {
public:
    explicit HarnessAnalog(int axis_) : axis{axis_} {}
    std::tuple<float, float> GetStatus() const override {
        if (axis == 0) {
            return {g_input.circle_x.load(), g_input.circle_y.load()};
        }
        return {0.0f, 0.0f};
    }

private:
    int axis;
};

class HarnessAnalogFactory final : public Input::Factory<Input::AnalogDevice> {
public:
    std::unique_ptr<Input::AnalogDevice> Create(const Common::ParamPackage& params) override {
        return std::make_unique<HarnessAnalog>(params.Get("axis", 0));
    }
};

const std::array<std::pair<const char*, int>, 15> ButtonNames{{
    {"a", Settings::NativeButton::A},
    {"b", Settings::NativeButton::B},
    {"x", Settings::NativeButton::X},
    {"y", Settings::NativeButton::Y},
    {"up", Settings::NativeButton::Up},
    {"down", Settings::NativeButton::Down},
    {"left", Settings::NativeButton::Left},
    {"right", Settings::NativeButton::Right},
    {"l", Settings::NativeButton::L},
    {"r", Settings::NativeButton::R},
    {"start", Settings::NativeButton::Start},
    {"select", Settings::NativeButton::Select},
    {"zl", Settings::NativeButton::ZL},
    {"zr", Settings::NativeButton::ZR},
    {"home", Settings::NativeButton::Home},
}};

int ButtonIndex(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& [key, index] : ButtonNames) {
        if (name == key) {
            return index;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------------------------

struct Reply {
    std::string body;
    bool ok = true;
    std::string error;

    void Fail(std::string message) {
        ok = false;
        error = std::move(message);
    }
    std::string Format() const {
        std::string out = body;
        if (!out.empty() && out.back() != '\n') {
            out.push_back('\n');
        }
        out += ok ? "OK\n" : "ERR " + error + "\n";
        return out;
    }
};

class Harness {
public:
    Harness() : system{Core::System::GetInstance()} {}

    bool Boot(const std::string& rom_path) {
        Frontend::RegisterDefaultApplets(system);
        system.RegisterImageInterface(std::make_shared<Frontend::ImageInterface>());

        Input::RegisterFactory<Input::ButtonDevice>("harness",
                                                    std::make_shared<HarnessButtonFactory>());
        Input::RegisterFactory<Input::AnalogDevice>("harness",
                                                    std::make_shared<HarnessAnalogFactory>());

        window = std::make_unique<HeadlessWindow>();

        auto& engine = Reader::Engine::Instance();
        engine.SetEventCallback(
            [this](const Reader::TextEvent& event) { events.push_back(event.ToJson()); });
        engine.SetLayoutCallback(
            [this](const Reader::TextLayout& layout) { layouts.push_back(layout.ToJson()); });
        engine.SetLogCallback([this](Reader::LogLevel level, const std::string& message) {
            const char* tag = level == Reader::LogLevel::Error     ? "error"
                              : level == Reader::LogLevel::Warning ? "warn"
                                                                   : "info";
            logs.push_back(fmt::format("[{}] {}", tag, message));
        });
        engine.SetDataProvider([this](const std::string& name) -> std::optional<std::string> {
            if (profile_dir.empty() || name.find("..") != std::string::npos ||
                name.find('/') == 0) {
                return std::nullopt;
            }
            std::ifstream file{profile_dir + "/" + name, std::ios::binary};
            if (!file) {
                return std::nullopt;
            }
            std::ostringstream contents;
            contents << file.rdbuf();
            return contents.str();
        });

        FileUtil::SetCurrentRomPath(rom_path);
        auto loader = Loader::GetLoader(rom_path);
        if (!loader) {
            std::cerr << "no loader for " << rom_path << "\n";
            return false;
        }
        system.RegisterAppLoaderEarly(loader);
        system.ApplySettings();

        const auto result = system.Load(*window, rom_path);
        if (result != Core::System::ResultStatus::Success) {
            std::cerr << "load failed: " << static_cast<u32>(result) << " "
                      << system.GetStatusDetails() << "\n";
            return false;
        }
        system.RegisterCoreLoopThreadId();
        return true;
    }

    void Shutdown() {
        if (system.IsPoweredOn()) {
            system.Shutdown();
        }
    }

    Reply Execute(const std::string& line) {
        Reply reply;
        std::istringstream in{line};
        std::string command;
        in >> command;
        const auto rest = [&in] {
            std::string text;
            std::getline(in >> std::ws, text);
            return text;
        };

        try {
            if (command.empty() || command[0] == '#') {
                // blank line or comment
            } else if (command == "run") {
                u64 count = 1;
                in >> count;
                Run(count, reply);
            } else if (command == "press") {
                // press <button> [hold frames=4] [frames to run afterwards=4]
                std::string name;
                u64 hold = 4;
                u64 after = 4;
                in >> name >> hold >> after;
                const int index = ButtonIndex(name);
                if (index < 0) {
                    reply.Fail("unknown button " + name);
                } else {
                    g_input.buttons[index] = true;
                    Run(hold, reply);
                    g_input.buttons[index] = false;
                    Run(after, reply);
                }
            } else if (command == "hold" || command == "release") {
                std::string name;
                in >> name;
                const int index = ButtonIndex(name);
                if (index < 0) {
                    reply.Fail("unknown button " + name);
                } else {
                    g_input.buttons[index] = command == "hold";
                }
            } else if (command == "touch") {
                // touch <x> <y> [hold frames=4] [frames afterwards=4]; bottom screen pixels
                unsigned x = 0;
                unsigned y = 0;
                u64 hold = 4;
                u64 after = 4;
                in >> x >> y >> hold >> after;
                window->TouchPressed(BottomLeft + std::min(x, BottomWidth - 1),
                                     ScreenHeight + std::min(y, ScreenHeight - 1));
                Run(hold, reply);
                window->TouchReleased();
                Run(after, reply);
            } else if (command == "circle") {
                float x = 0.0f;
                float y = 0.0f;
                in >> x >> y;
                g_input.circle_x = std::clamp(x, -1.0f, 1.0f);
                g_input.circle_y = std::clamp(y, -1.0f, 1.0f);
            } else if (command == "shot") {
                Screenshot(rest(), reply);
            } else if (command == "events") {
                for (const auto& event : events) {
                    reply.body += event + "\n";
                }
                events.clear();
            } else if (command == "layouts") {
                for (const auto& layout : layouts) {
                    reply.body += layout + "\n";
                }
                layouts.clear();
            } else if (command == "profile") {
                LoadProfile(rest(), reply);
            } else if (command == "unload") {
                Reader::Engine::Instance().UnloadProfile();
            } else if (command == "lua") {
                std::string output;
                const bool ok = Reader::Engine::Instance().Execute(rest(), output);
                reply.body += output;
                if (!ok) {
                    reply.Fail("lua error");
                }
            } else if (command == "mem") {
                // mem <hex address> <length>
                u32 address = 0;
                std::size_t length = 64;
                in >> std::hex >> address >> std::dec >> length;
                HexDump(address, std::min<std::size_t>(length, 1 << 16), reply);
            } else if (command == "memfile") {
                // memfile <hex address> <length> <path>
                u32 address = 0;
                std::size_t length = 0;
                in >> std::hex >> address >> std::dec >> length;
                const std::string path = rest();
                const std::string data = Reader::Engine::Instance().ReadMemory(address, length);
                std::ofstream file{path, std::ios::binary};
                file.write(data.data(), static_cast<std::streamsize>(data.size()));
                reply.body += fmt::format("wrote {} bytes\n", data.size());
            } else if (command == "find") {
                // find <hex bytes> [hex start] [hex end]
                std::string hex;
                u64 start = 0x00100000;
                u64 end = 0x40000000;
                in >> hex >> std::hex >> start >> end;
                Find(hex, start, end, reply);
            } else if (command == "audio") {
                // audio on [seconds] | audio off | audio pos
                std::string mode;
                u32 seconds = Reader::AudioTap::DefaultSeconds;
                in >> mode >> seconds;
                if (mode == "on") {
                    Reader::AudioTap::Instance().SetEnabled(true, seconds);
                } else if (mode == "off") {
                    Reader::AudioTap::Instance().SetEnabled(false);
                }
                reply.body +=
                    fmt::format("audio position={}\n", Reader::AudioTap::Instance().Position());
            } else if (command == "wav") {
                // wav <start position> <end position, 0 = now> <path>
                u64 start = 0;
                u64 end = 0;
                in >> start >> end;
                WriteWav(rest(), start, end, reply);
            } else if (command == "savestate") {
                SaveState(rest(), reply);
            } else if (command == "loadstate") {
                LoadState(rest(), reply);
            } else if (command == "status") {
                const std::string title = Reader::Engine::Instance().GameId();
                reply.body += fmt::format("frame={} title={} profile={} hooks={}\n",
                                          window->frames,
                                          title.empty() ? "0000000000000000" : title,
                                          Reader::Engine::Instance().HasProfile() ? "yes" : "no",
                                          Reader::Engine::Instance().HookCount());
            } else if (command == "quit") {
                quit = true;
            } else {
                reply.Fail("unknown command " + command);
            }
        } catch (const std::exception& e) {
            reply.Fail(e.what());
        }

        for (const auto& log : logs) {
            reply.body += log + "\n";
        }
        logs.clear();
        return reply;
    }

    bool quit = false;

private:
    void Run(u64 count, Reply& reply) {
        const u64 target = window->frames + count;
        const auto started = std::chrono::steady_clock::now();
        while (window->frames < target) {
            const auto status = system.RunLoop();
            if (status != Core::System::ResultStatus::Success) {
                reply.Fail(fmt::format("emulation stopped with status {}: {}",
                                       static_cast<u32>(status), system.GetStatusDetails()));
                break;
            }
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        reply.body += fmt::format("frame={} ({:.0f} fps)\n", window->frames,
                                  seconds > 0 ? static_cast<double>(count) / seconds : 0.0);
    }

    void Screenshot(const std::string& path, Reply& reply) {
        if (path.empty()) {
            reply.Fail("shot needs a path");
            return;
        }
        auto* renderer = dynamic_cast<SwRenderer::RendererSoftware*>(&system.GPU().Renderer());
        if (!renderer) {
            reply.Fail("software renderer is not active");
            return;
        }
        std::vector<u8> image(TopWidth * ScreenHeight * 2 * 4, 0);
        for (std::size_t i = 3; i < image.size(); i += 4) {
            image[i] = 0xFF;
        }
        // The renderer stores each screen as `width` rows of `height` pixels: an upright image
        // that is `height` wide and `width` tall.
        const auto blit = [&](VideoCore::ScreenId id, unsigned left, unsigned top) {
            const auto& screen = renderer->Screen(id);
            const unsigned image_width = screen.height;
            const unsigned image_height = screen.width;
            if (screen.pixels.size() < static_cast<std::size_t>(image_width) * image_height * 4) {
                return;
            }
            for (unsigned y = 0; y < std::min(image_height, ScreenHeight); ++y) {
                const std::size_t row = std::min(image_width, TopWidth - left);
                std::memcpy(&image[((top + y) * TopWidth + left) * 4],
                            &screen.pixels[static_cast<std::size_t>(y) * image_width * 4], row * 4);
            }
        };
        blit(VideoCore::ScreenId::TopLeft, 0, 0);
        blit(VideoCore::ScreenId::Bottom, BottomLeft, ScreenHeight);
        for (std::size_t i = 3; i < image.size(); i += 4) {
            image[i] = 0xFF;
        }
        const unsigned error = lodepng::encode(path, image, TopWidth, ScreenHeight * 2);
        if (error) {
            reply.Fail(fmt::format("PNG encode failed: {}", lodepng_error_text(error)));
        } else {
            reply.body += fmt::format("wrote {} at frame {}\n", path, window->frames);
        }
    }

    void LoadProfile(const std::string& path, Reply& reply) {
        std::ifstream file{path, std::ios::binary};
        if (!file) {
            reply.Fail("cannot read " + path);
            return;
        }
        std::ostringstream source;
        source << file.rdbuf();
        const auto slash = path.find_last_of('/');
        profile_dir = slash == std::string::npos ? "." : path.substr(0, slash);
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        auto& engine = Reader::Engine::Instance();
        if (!engine.LoadProfile(name, source.str())) {
            reply.Fail(engine.LastError());
        } else {
            reply.body += fmt::format("hooks={}\n", engine.HookCount());
        }
    }

    void HexDump(u32 address, std::size_t length, Reply& reply) {
        const std::string data = Reader::Engine::Instance().ReadMemory(address, length);
        for (std::size_t i = 0; i < data.size(); i += 16) {
            reply.body += fmt::format("{:08x}:", address + i);
            std::string ascii;
            for (std::size_t k = i; k < std::min(i + 16, data.size()); ++k) {
                const u8 byte = static_cast<u8>(data[k]);
                reply.body += fmt::format(" {:02x}", byte);
                ascii.push_back(byte >= 0x20 && byte < 0x7F ? static_cast<char>(byte) : '.');
            }
            reply.body += "  " + ascii + "\n";
        }
        if (data.size() < length) {
            reply.body += fmt::format("(unmapped after {} bytes)\n", data.size());
        }
    }

    void Find(const std::string& hex, u64 start, u64 end, Reply& reply) {
        std::string needle;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
            needle.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
        }
        if (needle.empty()) {
            reply.Fail("find needs hex bytes");
            return;
        }
        auto& engine = Reader::Engine::Instance();
        constexpr std::size_t Chunk = 1 << 20;
        std::size_t matches = 0;
        u64 address = start & ~0xFFFULL;
        while (address < end && matches < 64) {
            // Overlap consecutive chunks so matches across the boundary are found.
            const std::string data =
                engine.ReadMemory(static_cast<u32>(address), Chunk + needle.size() - 1);
            if (data.empty()) {
                address += 0x1000;
                continue;
            }
            std::size_t pos = 0;
            while ((pos = data.find(needle, pos)) != std::string::npos && matches < 64) {
                if (pos < Chunk) {
                    reply.body += fmt::format("{:08x}\n", address + pos);
                    ++matches;
                }
                ++pos;
            }
            address += std::max<std::size_t>(0x1000, std::min(data.size(), Chunk) & ~0xFFFULL);
        }
        reply.body += fmt::format("{} match(es)\n", matches);
    }

    void WriteWav(const std::string& path, u64 start, u64 end, Reply& reply) {
        auto& tap = Reader::AudioTap::Instance();
        u64 actual_start = 0;
        const std::vector<s16> samples =
            tap.Read(start, end == 0 ? tap.Position() : end, actual_start);
        std::ofstream file{path, std::ios::binary};
        if (!file) {
            reply.Fail("cannot write " + path);
            return;
        }
        const u32 data_bytes = static_cast<u32>(samples.size() * sizeof(s16));
        const auto put32 = [&file](u32 value) {
            file.write(reinterpret_cast<const char*>(&value), 4);
        };
        const auto put16 = [&file](u16 value) {
            file.write(reinterpret_cast<const char*>(&value), 2);
        };
        file.write("RIFF", 4);
        put32(36 + data_bytes);
        file.write("WAVEfmt ", 8);
        put32(16);
        put16(1); // PCM
        put16(2); // stereo
        put32(Reader::AudioTap::SampleRate);
        put32(Reader::AudioTap::SampleRate * 4);
        put16(4);
        put16(16);
        file.write("data", 4);
        put32(data_bytes);
        file.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
        reply.body += fmt::format(
            "wrote {} frames from position {} ({:.2f} s)\n", samples.size() / 2, actual_start,
            static_cast<double>(samples.size() / 2) / Reader::AudioTap::SampleRate);
    }

    void SaveState(const std::string& path, Reply& reply) {
        const std::vector<u8> buffer = system.SaveStateBuffer();
        std::ofstream file{path, std::ios::binary};
        file.write(reinterpret_cast<const char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        if (!file) {
            reply.Fail("cannot write " + path);
            return;
        }
        std::ofstream meta{path + ".frame"};
        meta << window->frames << "\n";
        reply.body += fmt::format("saved {} bytes at frame {}\n", buffer.size(), window->frames);
    }

    void LoadState(const std::string& path, Reply& reply) {
        std::ifstream file{path, std::ios::binary};
        if (!file) {
            reply.Fail("cannot read " + path);
            return;
        }
        std::vector<u8> buffer{std::istreambuf_iterator<char>(file),
                               std::istreambuf_iterator<char>()};
        // States are stamped with the git revision and refused by other builds. The harness
        // is rebuilt constantly while its states stay useful, so restamp the header
        // (magic[4], program id[8], revision[20]) with the running build's revision.
        constexpr std::size_t RevisionOffset = 12;
        constexpr std::size_t RevisionSize = 20;
        const std::string revision = Common::g_scm_rev;
        if (buffer.size() >= RevisionOffset + RevisionSize && revision.size() >= RevisionSize * 2) {
            for (std::size_t i = 0; i < RevisionSize; ++i) {
                buffer[RevisionOffset + i] =
                    static_cast<u8>(std::stoul(revision.substr(i * 2, 2), nullptr, 16));
            }
        }
        if (!system.LoadStateBuffer(std::move(buffer))) {
            reply.Fail("state rejected (different build or game?)");
            return;
        }
        reply.body += fmt::format("loaded state at harness frame {}\n", window->frames);
    }

    Core::System& system;
    std::unique_ptr<HeadlessWindow> window;
    std::vector<std::string> events;
    std::vector<std::string> layouts;
    std::vector<std::string> logs;
    std::string profile_dir;
};

void ConfigureSettings() {
    using namespace Settings;
    values.graphics_api.SetValue(GraphicsAPI::Software);
    values.use_cpu_jit = true;
    values.frame_limit.SetValue(0); // unthrottled
    values.output_type = AudioCore::SinkType::Null;
    values.enable_audio_stretching.SetValue(false);
    values.volume.SetValue(0.0f);
    values.init_clock = InitClock::FixedTime; // repeatable runs
    values.init_time = 1'700'000'000ULL;
    values.lle_applets.SetValue(false); // no system files needed
    values.use_virtual_sd = true;
    values.log_filter = "*:Warning";

    auto& profile = values.current_input_profile;
    for (int i = 0; i < NativeButton::NumButtons; ++i) {
        profile.buttons[i] = fmt::format("engine:harness,button:{}", i);
    }
    profile.analogs[NativeAnalog::CirclePad] = "engine:harness,axis:0";
    profile.analogs[NativeAnalog::CStick] = "engine:harness,axis:1";
    profile.touch_device = "engine:emu_window";
    profile.motion_device = "engine:harness";
    profile.use_touch_from_button = false;

    for (const auto& service_module : Service::service_module_map) {
        values.lle_modules.emplace(service_module.name, false);
    }
}

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
        // One request per connection: lines until the peer shuts down its write side.
        std::string request;
        char buffer[4096];
        ssize_t n;
        while ((n = ::read(client, buffer, sizeof(buffer))) > 0) {
            request.append(buffer, static_cast<std::size_t>(n));
        }
        std::istringstream lines{request};
        std::string line;
        std::string response;
        while (!harness.quit && std::getline(lines, line)) {
            response += harness.Execute(line).Format();
        }
        WriteAll(client, response);
        ::close(client);
    }
    ::close(server);
    ::unlink(path.c_str());
}

} // Anonymous namespace

int main(int argc, char** argv) {
    std::string user_dir;
    std::string rom;
    std::string socket_path;
    std::string script;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--user-dir") {
            user_dir = value();
        } else if (arg == "--rom") {
            rom = value();
        } else if (arg == "--socket") {
            socket_path = value();
        } else if (arg == "--script") {
            script = value();
        } else {
            std::cerr << "unknown argument " << arg << "\n";
            return 2;
        }
    }
    if (user_dir.empty() || rom.empty()) {
        std::cerr << "usage: azahar-reader-harness --user-dir DIR --rom FILE "
                     "[--socket PATH] [--script FILE]\n";
        return 2;
    }
    if (user_dir.back() != '/') {
        user_dir.push_back('/');
    }

    std::signal(SIGPIPE, SIG_IGN);
    FileUtil::CreateFullPath(user_dir);
    FileUtil::SetUserPath(user_dir);
    ConfigureSettings();

    Common::Log::Initialize();
    Common::Log::SetColorConsoleBackendEnabled(false);
    Common::Log::Start();
    Common::Log::Filter filter;
    filter.ParseFilterString(Settings::values.log_filter.GetValue());
    Common::Log::SetGlobalFilter(filter);

    Harness harness;
    if (!harness.Boot(rom)) {
        return 1;
    }
    std::cerr << "booted " << rom << " (" << Common::g_build_fullname << ")" << std::endl;

    if (!script.empty()) {
        std::ifstream file{script};
        std::string line;
        while (!harness.quit && std::getline(file, line)) {
            std::cout << "> " << line << "\n" << harness.Execute(line).Format() << std::flush;
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
    Common::Log::Stop();
    return 0;
}
