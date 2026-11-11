// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Furoshiki's host for LunaHook (docs/hooks.md): LunaTranslator's hook engine has no command
// line host of its own, only a DLL (LunaHost64.dll) with C exports. This small program loads
// that DLL from where LunaTranslator's files were fetched (it is never shipped with Furoshiki),
// puts the hook into one process and writes what it finds as lines of UTF-8 JSON, which
// Furoshiki reads (src/hook_engine.h, ReadLunaLine):
//   {"attached":1234}  {"gone":1234}  {"said":"…"}  {"fault":"…"}
//   {"thread":"new","name":"…","code":"…","pid":1234,"addr":"1F","ctx":"2A","ctx2":"0"}
//   {"text":"…", and the same}
// It ends when its input is closed or the process goes. A process of its own, so that a fault
// in someone else's DLL is not Furoshiki's.
//
//   furoshiki-luna-host <LunaTranslator's files folder> <pid> <32|64> [hook code...]
//
// The exports and their forms are LunaTranslator's src/NativeImpl/LunaHook/LunaHost/
// LunaHostDll.cpp as read on 2026-11-10 (v12.1.3.9). Windows only. **Compiled, never run.**

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

namespace {

struct ThreadParam {
    DWORD processId;
    uint64_t addr;
    uint64_t ctx;
    uint64_t ctx2;
};
enum class HOSTINFO { Console, Warning, EmuWarning, EmuConnected, EngineType };

using ProcessEvent = void (*)(DWORD pid);
using ThreadEventMaybeEmbed = void (*)(const wchar_t* hookcode, const char* hookname, ThreadParam, bool embedable);
using ThreadEvent = void (*)(const wchar_t* hookcode, const char* hookname, ThreadParam);
using OutputCallback = void (*)(const wchar_t* hookcode, const char* hookname, ThreadParam, const wchar_t* text);
using HostInfoHandler = void (*)(HOSTINFO type, const wchar_t* log);
using HookInsertHandler = void (*)(DWORD pid, uint64_t address, const wchar_t* hookcode);
using EmbedCallback = void (*)(const wchar_t* text, ThreadParam);
using I18NQueryCallback = wchar_t* (*)(const wchar_t* text);
using EmuGameInfoCallback = void (*)(const wchar_t* id, const wchar_t* title, const wchar_t* version);

using LunaStart = void (*)(ProcessEvent, ProcessEvent, ThreadEventMaybeEmbed, ThreadEvent, OutputCallback, HostInfoHandler, HookInsertHandler, EmbedCallback,
                           I18NQueryCallback, EmuGameInfoCallback);
using LunaSettings = void (*)(int flush_delay, int codepage, int max_buffer, int max_history, bool pc_hooks);
using LunaPid = void (*)(DWORD pid);
using LunaNeedsInject = bool (*)(DWORD pid);
using LunaInsertHookCode = void (*)(DWORD pid, LPCWSTR code);
using LunaAllocString = void* (*)(const wchar_t* text);

CRITICAL_SECTION out_lock;
LunaInsertHookCode insert_hook_code = nullptr;
LunaAllocString alloc_string = nullptr;
std::vector<std::wstring> codes;
DWORD target = 0;

std::string Utf8(const wchar_t* text) {
    if (text == nullptr) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size > 0 ? size - 1 : 0, '\0');
    if (size > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    }
    return out;
}

std::string Quoted(const std::string& text) {
    std::string out = "\"";
    for (const unsigned char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char escaped[8];
                snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                out += escaped;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

void Write(const std::string& line) {
    EnterCriticalSection(&out_lock);
    fwrite(line.data(), 1, line.size(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
    LeaveCriticalSection(&out_lock);
}

void Say(const char* key, const std::string& text) {
    Write(std::string("{\"") + key + "\":" + Quoted(text) + "}");
}

std::string Hex(uint64_t value) {
    char text[24];
    snprintf(text, sizeof(text), "%llX", static_cast<unsigned long long>(value));
    return text;
}

std::string ThreadFields(const wchar_t* code, const char* name, const ThreadParam& thread) {
    return "\"name\":" + Quoted(name != nullptr ? name : "") + ",\"code\":" + Quoted(Utf8(code)) + ",\"pid\":" + std::to_string(thread.processId) + ",\"addr\":\"" +
           Hex(thread.addr) + "\",\"ctx\":\"" + Hex(thread.ctx) + "\",\"ctx2\":\"" + Hex(thread.ctx2) + "\"";
}

void OnConnect(DWORD pid) {
    Write("{\"attached\":" + std::to_string(pid) + "}");
    for (const std::wstring& code : codes) {
        insert_hook_code(pid, code.c_str());
    }
}

void OnDisconnect(DWORD pid) {
    Write("{\"gone\":" + std::to_string(pid) + "}");
}

void OnThread(const wchar_t* code, const char* name, ThreadParam thread, bool) {
    Write("{\"thread\":\"new\"," + ThreadFields(code, name, thread) + "}");
}

void OnThreadGone(const wchar_t*, const char*, ThreadParam) {}

void OnOutput(const wchar_t* code, const char* name, ThreadParam thread, const wchar_t* text) {
    Write("{\"text\":" + Quoted(Utf8(text)) + "," + ThreadFields(code, name, thread) + "}");
}

void OnHostInfo(HOSTINFO type, const wchar_t* log) {
    Say(type == HOSTINFO::Warning || type == HOSTINFO::EmuWarning ? "fault" : "said", Utf8(log));
}

void OnHookInserted(DWORD, uint64_t, const wchar_t* code) {
    Say("said", "hook " + Utf8(code));
}

void OnEmbed(const wchar_t*, ThreadParam) {}

wchar_t* OnI18N(const wchar_t* text) {
    // The DLL frees what it is handed: its own words, in its own memory.
    return alloc_string != nullptr ? static_cast<wchar_t*>(alloc_string(text)) : nullptr;
}

void OnEmuGame(const wchar_t* id, const wchar_t* title, const wchar_t* version) {
    Say("said", "emulated game " + Utf8(id) + " " + Utf8(title) + " " + Utf8(version));
}

/// LunaTranslator's own injector, of the game's size: LunaSubprocessNN.exe dllinject <pid> <dll>.
bool Inject(const std::wstring& folder, DWORD pid, bool is32) {
    const std::wstring size = is32 ? L"32" : L"64";
    const std::wstring program = folder + L"\\LunaSubprocess" + size + L".exe";
    std::wstring command = L"\"" + program + L"\" dllinject " + std::to_wstring(pid) + L" \"" + folder + L"\\LunaHook\\LunaHook" + size + L".dll\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION made{};
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &made)) {
        Say("fault", "LunaSubprocess" + Utf8(size.c_str()) + ".exe could not be started (" + std::to_string(GetLastError()) + ")");
        return false;
    }
    WaitForSingleObject(made.hProcess, 15000);
    DWORD code = 0;
    GetExitCodeProcess(made.hProcess, &code);
    CloseHandle(made.hProcess);
    CloseHandle(made.hThread);
    if (code != 1) { // its dllinject says 1 when the DLL is in
        Say("fault", "LunaHook could not be put into the process (the injector said " + std::to_string(code) + "): a game run as administrator wants Furoshiki run so too");
        return false;
    }
    return true;
}

DWORD WINAPI WaitForInputEnd(void*) {
    char byte;
    while (fread(&byte, 1, 1, stdin) == 1) {
    }
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    InitializeCriticalSection(&out_lock);
    _setmode(_fileno(stdout), _O_BINARY);
    if (argc < 4) {
        Say("fault", "furoshiki-luna-host <LunaTranslator's files folder> <pid> <32|64> [hook code...]");
        return 2;
    }
    const std::wstring folder = argv[1];
    target = static_cast<DWORD>(_wtoi(argv[2]));
    const bool is32 = std::wstring(argv[3]) == L"32";
    for (int i = 4; i < argc; ++i) {
        codes.emplace_back(argv[i]);
    }
    const std::wstring hook_folder = folder + L"\\LunaHook";
    SetDllDirectoryW(hook_folder.c_str());
    const HMODULE host = LoadLibraryW((hook_folder + L"\\LunaHost64.dll").c_str());
    if (host == nullptr) {
        Say("fault", "LunaHost64.dll could not be loaded from " + Utf8(hook_folder.c_str()) + " (" + std::to_string(GetLastError()) + ")");
        return 3;
    }
    const auto start = reinterpret_cast<LunaStart>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_Start")));
    const auto settings = reinterpret_cast<LunaSettings>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_Settings")));
    const auto connect = reinterpret_cast<LunaPid>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_ConnectProcess")));
    const auto detach = reinterpret_cast<LunaPid>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_DetachProcess")));
    const auto needs_inject = reinterpret_cast<LunaNeedsInject>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_CheckIfNeedInject")));
    insert_hook_code = reinterpret_cast<LunaInsertHookCode>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_InsertHookCode")));
    alloc_string = reinterpret_cast<LunaAllocString>(reinterpret_cast<void*>(GetProcAddress(host, "Luna_AllocString")));
    if (start == nullptr || settings == nullptr || connect == nullptr || detach == nullptr || needs_inject == nullptr || insert_hook_code == nullptr) {
        Say("fault", "This LunaHost64.dll is not the one Furoshiki's host was written for: an export is missing");
        return 4;
    }
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, target);
    if (process == nullptr) {
        Say("fault", "No process " + std::to_string(target) + " can be opened (" + std::to_string(GetLastError()) + ")");
        return 5;
    }
    start(OnConnect, OnDisconnect, OnThread, OnThreadGone, OnOutput, OnHostInfo, OnHookInserted, OnEmbed, OnI18N, OnEmuGame);
    // A sentence is gathered for 200 ms; Shift-JIS where a hook does not say; no hooks on
    // Windows' own text functions unless a hook code asks (as Kimochi sets it).
    settings(200, 932, 3000, 100000, false);
    connect(target);
    if (needs_inject(target) && !Inject(folder, target, is32)) {
        return 6;
    }
    const HANDLE input = CreateThread(nullptr, 0, WaitForInputEnd, nullptr, 0, nullptr);
    const HANDLE either[] = {process, input};
    const DWORD ended = WaitForMultipleObjects(2, either, FALSE, INFINITE);
    if (ended != WAIT_OBJECT_0) {
        detach(target); // Furoshiki let go: the hook leaves the game
        Sleep(300);
    } else {
        Write("{\"gone\":" + std::to_string(target) + "}");
    }
    fflush(stdout);
    // The DLL's threads are not waited for.
    ExitProcess(0);
}
