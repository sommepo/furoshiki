// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A stand-in for a game, for the hook runner's test (tools/test-hooks.sh, docs/hooks.md): it has
// one function that is handed each line of its made-up story as the "game" writes it, and a
// second one handed a speaker's name, as a visual novel's engine has. A script hooks them.
// Nothing here is a game's.
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

#if defined(_WIN32)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default"), noinline))
#endif

static volatile unsigned long shown;

EXPORT void target_show_line(const char* utf8) {
    shown += strlen(utf8);
}

EXPORT void target_show_name(int slot, const char* utf8) {
    shown += strlen(utf8) + (unsigned long)slot;
}

int main(int argc, char** argv) {
#ifdef __linux__
    // A test program may be looked into by a process that is not its parent (Yama's rule 1).
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#endif
    static const char* const lines[] = {
        "山道は細く、曲がりくねっている。",
        "助手席の奈美は、まだ眠っていた。",
        "助手席の奈美は、まだ眠っていた。",
        "雨が降り始めた。ワイパーを動かす。",
    };
    const int rounds = argc > 1 ? 1000000 : 3;
    printf("hook-target %d\n", (int)getpid());
    fflush(stdout);
    for (int round = 0; round < rounds; ++round) {
        for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
            usleep(500 * 1000);
            target_show_name(1, "奈美");
            target_show_line(lines[i]);
        }
        // The reader reads: nothing new for four seconds (a test looks a word up meanwhile).
        usleep(4000 * 1000);
    }
    return (int)(shown & 1);
}
