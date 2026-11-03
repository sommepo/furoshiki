// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The Vita apps' reading layer (lookup_layer.h) on a computer, with a real reader_dict.db:
// a tap on the front screen, the rear pad's pointer and its two-finger tap, its double tap's plain
// view, and the scene.
// Run by lookup_layer_test.sh DICT_DB.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include "reader/vita/lookup_layer.h"
#include "reader/vita/settings.h"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

void WaitIdle(const VitaReader::LookupLayer& layer) {
    for (int i = 0; i < 400 && layer.Busy(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

std::string FirstHead(const ReaderScene& scene) {
    for (int i = 0; i < scene.entry_count; ++i) {
        if (scene.entries[i].kind == READER_ENTRY_HEAD) {
            return scene.text + scene.entries[i].offset;
        }
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: lookup_layer_test DICT_DB\n");
        return 2;
    }
    VitaReader::LookupLayer layer{VitaReader::PadArea{0, 0, 1919, 889}};
    std::string error;
    Check(layer.OpenDictionary(argv[1], error, 1024), "the dictionary opens");

    // The PSP's screen (480x272) drawn at twice its size over the whole of the Vita's.
    layer.SetPicture({0, 0, 960, 544});
    Reader::TextEvent event;
    event.window = "message";
    event.sequence = 7;
    event.text = "大丈夫ですか";
    layer.OnText(event);
    Reader::TextLayout layout;
    layout.window = "message";
    layout.sequence = 7;
    layout.space_width = 480;
    layout.space_height = 272;
    for (int i = 0; i < 6; ++i) {
        layout.glyphs.push_back(Reader::ScreenRect{40.0f + 20.0f * i, 200, 20, 20});
    }
    layer.OnLayout(layout);

    // A tap on the front screen on 大 (its cell is 80..120, 400..440 on the Vita's screen).
    std::uint64_t now = 1'000'000;
    layer.Input({1, 100, 420}, {}, now);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    const auto asked = layer.Asked();
    Check(asked && asked->first == "message" && asked->second == 0, "a tap on 大 asks for it");
    WaitIdle(layer);
    Check(layer.TakeWordShown() == "大丈夫" && !layer.TakeWordShown(), "a new popup's word, once");
    ReaderScene scene;
    layer.Scene(scene, now);
    Check(scene.popup_shown == 1, "the popup shows");
    Check(FirstHead(scene).rfind("大丈夫", 0) == 0, ("its first word is 大丈夫: " + FirstHead(scene)).c_str());
    Check(scene.mark_count == 3, "three characters are marked");
    Check(scene.marks[0].x == 80 && scene.marks[0].y == 400 && scene.marks[0].width == 40,
          "the first mark is 大's cell on the Vita's screen");
    Check(scene.pointer_shown == 0, "no pointer before the rear pad is touched");

    // A tap beside the text closes the popup.
    layer.Input({1, 900, 50}, {}, now += 16'000);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.popup_shown == 0 && !layer.Asked(), "a tap beside the text closes the popup");

    // The rear pad: the pointer starts in the middle (480, 272) and moves by as much as the
    // finger, wherever it lands. Gain at the default speed: 2 * 960 / 1919 a pad unit.
    const float gain = 2.0f * 960.0f / 1919.0f;
    // To で (index 3, cell 200..240, 400..440): from 480,272 by -260,+148.
    layer.Input({}, {1, 1000, 300}, now += 16'000);
    layer.Input({}, {1, 1000 - 260 / gain, 300 + 148 / gain}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.pointer_shown == 1 && scene.pointer_x > 219 && scene.pointer_x < 221, "the pointer moves with the finger");
    const auto pointed = layer.Asked();
    Check(pointed && pointed->second == 3, "the word under the pointer is asked for (で)");
    layer.Input({}, {}, now += 16'000);
    WaitIdle(layer);
    layer.Scene(scene, now);
    Check(scene.popup_shown == 1 && FirstHead(scene).rfind("で", 0) == 0, ("the popup has で: " + FirstHead(scene)).c_str());

    // It fades and goes 2.5 s after it last moved; the popup stays.
    layer.Scene(scene, now + 2'300'000);
    Check(scene.pointer_shown == 1 && scene.pointer_alpha < 0.5f, "the pointer fades");
    layer.Scene(scene, now + 2'600'000);
    Check(scene.pointer_shown == 0 && scene.popup_shown == 1, "then goes, the popup staying");

    // A short tap of two fingers switches it off: a finger on the pad then moves nothing.
    now += 3'000'000;
    layer.Input({}, {1, 500, 500}, now);
    layer.Input({}, {2, 500, 500}, now += 30'000);
    layer.Input({}, {}, now += 60'000);
    Check(layer.TakeNote() == "pointer off", "a two-finger tap switches the pointer off");
    layer.Input({}, {1, 500, 500}, now += 16'000);
    layer.Input({}, {1, 900, 500}, now += 16'000);
    layer.Input({}, {}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.pointer_shown == 0, "the pointer stays off");
    layer.Input({}, {1, 500, 500}, now += 16'000);
    layer.Input({}, {2, 500, 500}, now += 30'000);
    layer.Input({}, {}, now += 60'000);
    layer.Scene(scene, now);
    Check(layer.TakeNote() == "pointer on" && scene.pointer_shown == 1, "and on again");
    Check(scene.pointer_x > 219 && scene.pointer_x < 221, "where it was: a finger moved nothing while it was off");

    // While the popup is up the controller is its: the stick scrolls as far as the drawing
    // allows, △ asks for a card, ○ closes it, and ○ stays the popup's until it is let go.
    layer.Input({1, 100, 420}, {}, now += 16'000);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    WaitIdle(layer);
    Check(layer.Modal(), "the popup is modal");
    layer.SetScrollLimit(3);
    layer.Pad({}, now);
    for (int i = 0; i < 30; ++i) {
        layer.Pad({1.0f}, now += 16'000);
    }
    layer.Scene(scene, now);
    Check(scene.scroll == 3, "the stick scrolls down to the drawing's limit");
    for (int i = 0; i < 6; ++i) {
        layer.Pad({-1.0f}, now += 16'000);
    }
    layer.Scene(scene, now);
    Check(scene.scroll == 1, "and back up");
    layer.Pad({0.1f}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.scroll == 1, "a stick barely leaning does nothing");
    (void)layer.TakeNote();
    layer.Pad({0, false, true, true}, now += 16'000);
    Check(layer.TakeNote() == "add: 大丈夫  【だいじょうぶ】", "△ asks for a card of the first word");
    const auto asked_card = layer.TakeCard();
    Check(asked_card && asked_card->expression == "大丈夫" && asked_card->reading == "だいじょうぶ", "the card has the word");
    Check(asked_card && asked_card->sentence == "<b>大丈夫</b>ですか", "and the line, the word in bold");
    Check(asked_card && asked_card->glossary.find("<ol><li data-dictionary=") != std::string::npos, "and its definitions");
    Check(!layer.TakeCard(), "once");
    layer.SetInDeck("別の語", true);
    layer.Scene(scene, now);
    Check(scene.in_deck == 0, "no tick for a word not in a deck, and another word's answer says nothing of this one");
    layer.SetInDeck("大丈夫", true);
    layer.Scene(scene, now);
    Check(scene.in_deck == 1, "a tick for a word a deck has");
    layer.SetInDeck("大丈夫", false);
    layer.SetSessionLine("1,234字 · <1m");
    layer.Scene(scene, now);
    Check(std::string{scene.session} == "1,234字 · <1m", "the reading so far, for the corner");
    layer.SetSessionLine("");
    layer.SetStatus("Card added", now + 1000);
    layer.Scene(scene, now);
    Check(std::string{scene.status} == "Card added" && layer.Showing(now), "a word for the player shows");
    layer.Scene(scene, now + 2000);
    Check(scene.status[0] == 0, "and goes");
    layer.Pad({0, false, false, false}, now += 16'000);
    layer.Pad({0, true, false, true}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.popup_shown == 0 && layer.Modal(), "○ closes the popup, and is kept from the game while held");
    layer.Pad({0, false, false, false}, now += 16'000);
    Check(!layer.Modal(), "let go, the controller is the game's again");

    // A double tap on the rear pad: the line alone on a grid, whatever its layout says. Here the
    // layout has gone, as a hook out of place might have it: no glyphs at all.
    const auto double_tap = [&] {
        layer.Input({}, {1, 700, 400}, now += 16'000);
        layer.Input({}, {}, now += 60'000);
        layer.Input({}, {1, 710, 405}, now += 120'000);
        layer.Input({}, {}, now += 60'000);
    };
    now += 3'000'000;
    event.window = "message";
    event.sequence = 8;
    event.text = "大丈\n夫です";
    layer.OnText(event);
    layout.sequence = 8;
    layout.glyphs.clear();
    layer.OnLayout(layout);
    layer.Input({1, 100, 420}, {}, now += 16'000);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    Check(!layer.Asked(), "with no layout, a tap in the picture finds nothing");
    (void)layer.TakeNote();
    double_tap();
    layer.Scene(scene, now);
    Check(scene.plain_shown == 1, "a double tap on the rear pad shows the plain view");
    Check(std::string{scene.plain_text} == "大丈夫です", "it has the line, in one run");
    Check(scene.plain_cell == 64, "a short line stands large");
    Check(scene.plain_left == (960 - 5 * 64) / 2.0f, "and in the middle of the screen");
    Check(scene.plain_box.x == scene.plain_left - 24 && scene.plain_box.width == 5 * 64 + 48, "in its box");
    Check(layer.Modal() && layer.Showing(now), "it is modal, and to be drawn");

    // A tap on 夫, the third cell: the word the game's line broke in two is found whole.
    const float row_y = scene.plain_top + scene.plain_row / 2;
    layer.Input({1, scene.plain_left + 2.5f * scene.plain_cell, row_y}, {}, now += 16'000);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    const auto plain_asked = layer.Asked();
    Check(plain_asked && plain_asked->second == 2, "a tap finds the character in its cell");
    WaitIdle(layer);
    layer.Input({1, scene.plain_left + 0.5f * scene.plain_cell, row_y}, {}, now += 16'000);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    WaitIdle(layer);
    layer.Scene(scene, now);
    Check(scene.popup_shown == 1 && FirstHead(scene).rfind("大丈夫", 0) == 0,
          ("大丈夫 is found across the game's line break: " + FirstHead(scene)).c_str());
    Check(scene.mark_count == 3 && scene.marks[0].x == scene.plain_left && scene.marks[0].width == scene.plain_cell,
          "its three cells are marked");

    // The game goes on: the view keeps its line and the popup.
    event.sequence = 9;
    event.text = "別の行";
    layer.OnText(event);
    layout.sequence = 9;
    layer.OnLayout(layout);
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "大丈夫です" && scene.popup_shown == 1, "a new line changes neither");

    // ○ closes the popup first, then the view; a double tap closes it too.
    layer.Pad({0, true, false, true}, now += 16'000);
    layer.Pad({0, false, false, false}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.popup_shown == 0 && scene.plain_shown == 1 && layer.Modal(), "○ closes the popup, the view stays");

    // The pointer works in it, though switched off.
    layer.TogglePointer(now);
    layer.Input({}, {1, 100, 100}, now += 16'000);
    layer.Input({}, {1, 110, 100}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.pointer_shown == 1, "the rear pad's pointer works in the view, even switched off");
    layer.TogglePointer(now);
    layer.Input({}, {}, now += 500'000);
    now += 1'000'000;

    layer.Pad({0, true, false, true}, now += 16'000);
    layer.Pad({0, false, false, false}, now += 16'000);
    layer.Scene(scene, now);
    Check(scene.plain_shown == 0 && !layer.Modal(), "and ○ closes the view, the controller the game's again");
    double_tap();
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "別の行", "the next time it has the newest line");
    double_tap();
    layer.Scene(scene, now);
    Check(scene.plain_shown == 0, "a second double tap closes it");

    // Taps too far apart in time or place are no double tap.
    now += 1'000'000;
    layer.Input({}, {1, 700, 400}, now += 16'000);
    layer.Input({}, {}, now += 60'000);
    layer.Input({}, {1, 700, 400}, now += 500'000);
    layer.Input({}, {}, now += 60'000);
    now += 1'000'000;
    layer.Input({}, {1, 200, 100}, now += 16'000);
    layer.Input({}, {}, now += 60'000);
    layer.Input({}, {1, 1500, 700}, now += 100'000);
    layer.Input({}, {}, now += 60'000);
    layer.Scene(scene, now);
    Check(scene.plain_shown == 0, "two taps apart in time or place do nothing");

    // A long line takes smaller cells, and everything that fits the grid is kept.
    event.sequence = 10;
    event.text.clear();
    for (int i = 0; i < 400; ++i) {
        event.text += "あ";
    }
    layer.OnText(event);
    double_tap();
    layer.Scene(scene, now);
    Check(scene.plain_cell == 26 && std::strlen(scene.plain_text) == 1200, "a long line takes smaller cells");
    double_tap();
    event.sequence = 11;
    event.text.clear();
    for (int i = 0; i < 2000; ++i) {
        event.text += "あ";
    }
    layer.OnText(event);
    double_tap();
    layer.Scene(scene, now);
    Check(scene.plain_cell == 22 && std::strlen(scene.plain_text) == 608 * 3, "the longest keeps what the grid holds");
    double_tap();

    // With a place for the text, the box goes over the game's own, a size larger.
    event.sequence = 12;
    event.text = "大丈夫です";
    layer.OnText(event);
    layout.sequence = 12;
    layout.glyphs.clear();
    for (int i = 0; i < 5; ++i) {
        layout.glyphs.push_back(Reader::ScreenRect{40.0f + 20.0f * i, 200, 20, 20});
    }
    layer.OnLayout(layout);
    double_tap();
    layer.Scene(scene, now);
    Check(scene.plain_shown == 1 && scene.plain_left == 80 && scene.plain_top == 400, "the text begins where the game's does");
    Check(scene.plain_cell == 44 && scene.plain_box.x == 70 && scene.plain_box.y == 390, "a size larger, the box round it");
    Check(scene.plain_box.x + scene.plain_box.width >= 280 && scene.plain_box.y + scene.plain_box.height >= 440,
          "and all of the game's line is under the box");
    layer.Input({1, 80 + 2.5f * 44, 420}, {}, now += 16'000);
    layer.Input({0, 0, 0}, {}, now += 16'000);
    const auto over_asked = layer.Asked();
    Check(over_asked && over_asked->second == 2, "a tap finds the character on the grid, not the game's");
    double_tap();

    // Up and down go through the lines before.
    const auto say = [&](std::uint64_t sequence, const std::string& text) {
        event.sequence = sequence;
        event.text = text;
        layer.OnText(event);
    };
    say(20, "一行目");
    say(21, "二行目");
    say(22, "三行目");
    layout.glyphs.clear();
    layout.sequence = 22;
    layer.OnLayout(layout);
    double_tap();
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "三行目", "the newest line first");
    layer.Pad({0, false, false, true, true, false}, now += 16'000);
    layer.Pad({}, now += 16'000);
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "二行目" && std::string{scene.status} == "1 back", "up, the line before");
    layer.Pad({-1.0f}, now += 16'000);
    layer.Pad({}, now += 16'000);
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "一行目", "the stick does the same");
    say(23, "四行目");
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "一行目", "a new line leaves the view where it is");
    layer.Pad({0, false, false, true, false, true}, now += 16'000);
    layer.Pad({}, now += 16'000);
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "二行目", "down, the line after");
    for (int i = 0; i < 300; ++i) {
        layer.Pad({0, false, false, true, true, false}, now += 16'000);
        layer.Pad({}, now += 16'000);
    }
    layer.Scene(scene, now);
    Check(scene.plain_shown == 1, "the far end stops there");
    double_tap();
    for (int i = 0; i < 260; ++i) {
        say(100 + i, "行" + std::to_string(i));
    }
    double_tap();
    for (int i = 0; i < 300; ++i) {
        layer.Pad({0, false, false, true, true, false}, now += 16'000);
        layer.Pad({}, now += 16'000);
    }
    layer.Scene(scene, now);
    Check(std::string{scene.plain_text} == "行60", "the last 200 lines are kept");
    double_tap();

    // The pointer's speed.
    VitaReader::LookupLayer slow{VitaReader::PadArea{0, 0, 1919, 889}};
    slow.SetPicture({0, 0, 960, 544});
    slow.SetPointerSpeed(1.0f);
    slow.Input({}, {1, 0, 400}, 1'000'000);
    slow.Input({}, {1, 479.75f, 400}, 1'016'000);
    slow.Scene(scene, 1'016'000);
    Check(scene.pointer_x > 719 && scene.pointer_x < 721, "at speed 1 a quarter of the pad moves it a quarter of the screen (from 480 to 720)");

    std::string notes;
    const auto settings = VitaReader::Settings::Parse("# the pointer\npointer_speed = 3.5 # fast\ncolour = red\n", &notes);
    Check(settings.pointer_speed == 3.5f, "the settings file sets the pointer's speed");
    Check(notes == "settings: not understood: colour = red", "and says what it did not understand");
    Check(VitaReader::Settings::Parse("pointer_speed = fast").pointer_speed == 2.0f, "a bad value leaves the default");
    Check(!VitaReader::Settings::Parse("").show_line, "the line's band is off at first");
    Check(VitaReader::Settings::Parse("show_line = on").show_line, "show_line = on brings the band back");
    Check(!VitaReader::Settings::Parse("show_line = yes").show_line, "show_line takes on or off");

    // A newer line in the window: the marked word is gone.
    event.sequence = 8;
    event.text = "行きます";
    layer.OnText(event);
    layout.sequence = 8;
    layer.OnLayout(layout);
    layer.Scene(scene, now);
    Check(scene.popup_shown == 0 && scene.mark_count == 0, "a newer line takes the popup away");

    std::printf(failures ? "%d failed\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
