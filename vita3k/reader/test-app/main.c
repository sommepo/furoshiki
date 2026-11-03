// SPDX-FileCopyrightText: Vita3K Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Core check: what a game does that this fork's core must carry, with nothing of the firmware's.
// Draws with the GPU (a bar for each button, a mark where the screen is touched), plays a tone on
// one sound port and another at a rate of its own, and calls say() (Thumb code, as a game's) when
// something changes, so that a script's hook sees text in memory and the input in registers:
// reader/test-app/RDRV00090.lua.

#include <math.h>
#include <psp2/audioout.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/touch.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vita2d.h>

static volatile int running = 1;
volatile unsigned said_count;

__attribute__((noinline)) void say(const char *line, unsigned buttons, int touch_x, int touch_y) {
    said_count += 1 + (buttons & 0) + (unsigned)(touch_x & 0) + (unsigned)(touch_y & 0) + (line[0] & 0);
}

static int tone_thread(SceSize args, void *argument) {
    const int rate = *(const int *)argument, frames = 1024;
    const float pitch = rate == 48000 ? 440.0f : 660.0f;
    static int16_t buffers[2][1024 * 2];
    int16_t *buffer = buffers[rate == 48000 ? 0 : 1];
    const int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, frames, rate, SCE_AUDIO_OUT_MODE_STEREO);
    float phase = 0;
    while (running) {
        for (int i = 0; i < frames; ++i) {
            const int16_t sample = (int16_t)(sinf(phase) * 6000);
            buffer[i * 2] = buffer[i * 2 + 1] = sample;
            phase += 2 * 3.14159265f * pitch / rate;
        }
        sceAudioOutOutput(port, buffer);
    }
    sceAudioOutReleasePort(port);
    return 0;
}

static void start_tone(int *rate, const char *name) {
    const SceUID thread = sceKernelCreateThread(name, tone_thread, 0x10000100, 0x4000, 0, 0, NULL);
    sceKernelStartThread(thread, sizeof(*rate), rate);
}

int main(void) {
    static int rate_a = 48000, rate_b = 44100;
    char line[128];
    vita2d_init();
    vita2d_set_clear_color(RGBA8(24, 20, 60, 255));
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG_WIDE);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    start_tone(&rate_a, "tone a");
    start_tone(&rate_b, "tone b");

    unsigned frame = 0, last_buttons = ~0u;
    int last_x = -1, last_y = -1;
    while (1) {
        SceCtrlData pad;
        SceTouchData touch;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        const int x = touch.reportNum > 0 ? touch.report[0].x / 2 : -1;
        const int y = touch.reportNum > 0 ? touch.report[0].y / 2 : -1;

        vita2d_start_drawing();
        vita2d_clear_screen();
        for (int bit = 0; bit < 16; ++bit) {
            const int on = (pad.buttons >> bit) & 1;
            vita2d_draw_rectangle(40 + bit * 55, 40, 45, 120, on ? RGBA8(255, 80, 200, 255) : RGBA8(90, 90, 150, 255));
        }
        vita2d_draw_rectangle(40 + (frame % 240) * 3, 240, 150, 40, RGBA8(180, 170, 255, 255));
        vita2d_draw_rectangle(pad.lx * 3.6f, 330 + pad.ly * 0.5f, 40, 40, RGBA8(120, 255, 180, 255));
        if (x >= 0)
            vita2d_draw_rectangle(x - 20, y - 20, 40, 40, RGBA8(255, 255, 255, 255));
        vita2d_end_drawing();
        vita2d_swap_buffers();
        vita2d_wait_rendering_done();

        if (pad.buttons != last_buttons || x != last_x || y != last_y || frame % 120 == 0) {
            snprintf(line, sizeof(line), "日本語の文を読む。第%u行", frame / 120);
            say(line, pad.buttons, x, y);
            last_buttons = pad.buttons;
            last_x = x;
            last_y = y;
        }
        if (pad.buttons & SCE_CTRL_START)
            break;
        ++frame;
    }
    running = 0;
    sceKernelDelayThread(100000);
    sceKernelExitProcess(0);
    return 0;
}
