// SPDX-FileCopyrightText: Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "reader/vita/lookup_layer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#ifdef __vita__
#include <cstdlib>
#include <psp2/sqlite.h>
#include <psp2/sysmodule.h>
#endif

namespace VitaReader {

namespace {

constexpr int MaxDefinitions = 4;
constexpr int MaxResults = 3;
/// How far a front touch may move and still be a tap, in screen pixels.
constexpr float TapSlop = 18;
/// How far a rear touch may move (in the pad's own units) and how long it may last to be a tap.
constexpr float PadTapSlop = 30;
constexpr std::uint64_t PadTapUs = 250'000;
/// A tap of two fingers (the pointer off or on) may last a little longer: fingers land apart.
constexpr std::uint64_t TwoFingerTapUs = 450'000;
/// A double tap on the rear pad: how soon after the first the second begins, and how near it.
constexpr std::uint64_t DoubleTapUs = 400'000;
constexpr float DoubleTapSlop = 200;
/// How long the pointer stays shown after it last moved, and how long it fades.
constexpr std::uint64_t PointerShownUs = 2'500'000;
constexpr std::uint64_t PointerFadeUs = 500'000;
/// The left stick: how far it must lean before it scrolls, and lines a second at full lean.
constexpr float StickDead = 0.3f;
constexpr float ScrollLinesPerSecond = 14;
/// The plain view's grid where the game's text has no place: the largest size of cell the line
/// fits in, and how far the text keeps from the screen's edges (the box is in that margin). A cell is square but for the row's gap,
/// which is a part of it, so nothing between two rows misses. The last size holds 608 characters.
constexpr float PlainCells[] = {64, 52, 44, 36, 30, 26, 22};
constexpr float PlainRowFactor = 1.25f;
constexpr float PlainMarginX = 56;
constexpr float PlainMarginY = 52;
/// The box's padding round the text.
constexpr float PlainPadding = 24;
/// Over the game's text: the box's padding, and the widest it goes for a short line of the game's.
constexpr float PlainOverPadding = 10;
constexpr float PlainOverWidth = 620;
/// How many lines the backlog keeps; how far the stick leans to take a step in it.
constexpr std::size_t HistoryLines = 200;
constexpr float StickStep = 0.6f;
constexpr std::uint64_t StatusUs = 2'000'000;
/// What is under a tap beyond the grid's edge, in pixels.
constexpr float PlainSlop = 14;
/// The plain view's name where a window's would be.
constexpr const char* PlainWindow = "\x01plain";

std::u32string Codepoints(const std::string& utf8) {
    std::u32string out;
    const std::u16string utf16 = ReaderLookup::Utf16(utf8);
    for (std::size_t i = 0; i < utf16.size(); ++i) {
        char32_t c = utf16[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < utf16.size()) {
            c = 0x10000 + ((c - 0xD800) << 10) + (utf16[++i] - 0xDC00);
        }
        out.push_back(c);
    }
    return out;
}

std::string Utf8Of(const std::u32string& text) {
    std::u16string utf16;
    for (char32_t c : text) {
        if (c >= 0x10000) {
            c -= 0x10000;
            utf16.push_back(static_cast<char16_t>(0xD800 + (c >> 10)));
            utf16.push_back(static_cast<char16_t>(0xDC00 + (c & 0x3FF)));
        } else {
            utf16.push_back(static_cast<char16_t>(c));
        }
    }
    return ReaderLookup::Utf8(utf16);
}

/// How many code points from `index` make `units` UTF-16 units.
std::size_t CodepointsFor(const std::u32string& text, std::size_t index, int units) {
    std::size_t count = 0;
    while (units > 0 && index + count < text.size()) {
        units -= text[index + count] >= 0x10000 ? 2 : 1;
        ++count;
    }
    return count;
}

/// The popup's entries for what was found: each word, its reading, its first definitions.
std::vector<std::pair<std::uint16_t, std::string>> Entries(const std::vector<ReaderLookup::LookupResult>& results) {
    std::vector<std::pair<std::uint16_t, std::string>> entries;
    for (const ReaderLookup::LookupResult& result : results) {
        std::string head = result.expression;
        if (!result.DisplayReading().empty()) {
            head += "  【" + result.DisplayReading() + "】";
        }
        entries.emplace_back(READER_ENTRY_HEAD, head);
        if (!result.reasons.empty()) {
            std::string reasons = "« ";
            for (std::size_t i = 0; i < result.reasons.size(); ++i) {
                reasons += (i ? " « " : "") + result.reasons[i];
            }
            entries.emplace_back(READER_ENTRY_REASONS, reasons);
        }
        int shown = 0;
        for (const ReaderLookup::TermEntry& entry : result.entries) {
            for (const ReaderLookup::Compact& definition : ReaderLookup::CompactDefinitions(entry.glossary_json)) {
                if (shown == MaxDefinitions) {
                    break;
                }
                ++shown;
                std::string text = std::to_string(shown) + ". " + definition.glosses;
                if (!definition.notes.empty()) {
                    text += " (" + definition.notes + ")";
                }
                entries.emplace_back(READER_ENTRY_DEFINITION, text);
            }
        }
    }
    return entries;
}

std::string EscapedHtml(const std::string& text) {
    std::string out;
    for (const char c : text) {
        out += c == '&' ? std::string("&amp;")
               : c == '<' ? std::string("&lt;")
               : c == '>' ? std::string("&gt;")
               : c == '"' ? std::string("&quot;")
                          : std::string(1, c);
    }
    return out;
}

/// The first word as a card has it. The desktop's {glossary} in its form, with the compact
/// definitions the Vita has for its body.
CardRequest CardOf(const ReaderLookup::LookupResult& result) {
    CardRequest card;
    card.expression = result.expression;
    card.reading = result.DisplayReading();
    card.glossary = "<div style=\"text-align: left;\" class=\"yomitan-glossary\"><ol>";
    for (const ReaderLookup::TermEntry& entry : result.entries) {
        std::string body;
        for (const ReaderLookup::Compact& definition : ReaderLookup::CompactDefinitions(entry.glossary_json)) {
            body += (body.empty() ? "" : "; ") + EscapedHtml(definition.glosses);
        }
        if (!body.empty()) {
            card.glossary += "<li data-dictionary=\"" + EscapedHtml(entry.dictionary) + "\"><i>(" +
                             EscapedHtml(entry.dictionary) + ")</i> " + body + "</li>";
        }
    }
    card.glossary += "</ol></div>";
    return card;
}

/// The line as HTML, the code points from `first` on, `length` of them, in bold.
std::string SentenceHtml(const std::u32string& text, std::size_t first, std::size_t length) {
    const auto piece = [&](std::size_t from, std::size_t size) {
        std::string out = EscapedHtml(Utf8Of(text.substr(from, size)));
        for (std::size_t at = out.find('\n'); at != std::string::npos; at = out.find('\n', at + 4)) {
            out.replace(at, 1, "<br>");
        }
        return out;
    };
    const std::size_t end = std::min(first + length, text.size());
    return piece(0, first) + "<b>" + piece(first, end - first) + "</b>" + piece(end, text.size() - end);
}

#ifdef __vita__
/// The system's SQLite (SceSqlite: SQLite's interface with a file layer for the Vita), with
/// this program's allocator. Once.
void PrepareSystemSqlite() {
    static const bool done = [] {
        sceSysmoduleLoadModule(SCE_SYSMODULE_SQLITE);
        static SceSqliteMallocMethods allocator{
            [](int size) { return std::malloc(static_cast<std::size_t>(size)); },
            [](void* block, int size) { return std::realloc(block, static_cast<std::size_t>(size)); },
            std::free};
        sceSqliteConfigMallocMethods(&allocator);
        return true;
    }();
    (void)done;
}
#endif

} // namespace

LookupLayer::LookupLayer(PadArea rear) : rear_area(rear) {
    worker = std::thread{[this] { Work(); }};
}

LookupLayer::~LookupLayer() {
    {
        std::scoped_lock lock{mutex};
        stopping = true;
    }
    wake.notify_all();
    worker.join();
}

bool LookupLayer::OpenDictionary(const std::string& path, std::string& error, int cache_kib) {
#ifdef __vita__
    PrepareSystemSqlite();
#endif
    std::scoped_lock lock{mutex};
    return dictionary.Open(path, error, cache_kib);
}

void LookupLayer::OnText(const Reader::TextEvent& event) {
    std::scoped_lock lock{mutex};
    Window& window = windows[event.window];
    window.text = Codepoints(event.text);
    window.sequence = event.sequence;
    if (!event.text.empty()) {
        latest = event.text;
        latest_window = event.window;
        if (history.empty() || history.back() != event.text) {
            history.push_back(event.text);
            if (history.size() > HistoryLines) {
                history.pop_front();
            }
            if (plain && history_back > 0) {
                ++history_back; // the view stays on its line
            }
        }
    }
}

void LookupLayer::OnLayout(const Reader::TextLayout& layout) {
    std::scoped_lock lock{mutex};
    Window& window = windows[layout.window];
    window.layout = layout;
    // A layout withdrawn, or one of a newer text: the word marked there is gone.
    if (answer && answer->request.window == layout.window &&
        (layout.closed || layout.sequence != answer->request.sequence)) {
        answer.reset();
        asked.reset();
    }
}

void LookupLayer::SetPicture(const PictureRect& rect) {
    std::scoped_lock lock{mutex};
    picture = rect;
}

const std::u32string& LookupLayer::TextOf(const std::string& window) {
    return plain && window == PlainWindow ? plain->text : windows[window].text;
}

std::optional<LookupLayer::Hit> LookupLayer::HitTest(float x, float y) const {
    if (plain) {
        const int wide = std::min(plain->columns, static_cast<int>(plain->text.size()));
        const float right = plain->left + wide * plain->cell;
        const int rows = (static_cast<int>(plain->text.size()) + plain->columns - 1) / plain->columns;
        const float bottom = plain->top + rows * plain->row;
        if (x < plain->left - PlainSlop || x >= right + PlainSlop || y < plain->top - PlainSlop ||
            y >= bottom + PlainSlop) {
            return std::nullopt;
        }
        const int column = std::clamp(static_cast<int>((x - plain->left) / plain->cell), 0, plain->columns - 1);
        const int row = std::clamp(static_cast<int>((y - plain->top) / plain->row), 0, rows - 1);
        const std::size_t index = static_cast<std::size_t>(row) * plain->columns + column;
        if (index >= plain->text.size()) {
            return std::nullopt;
        }
        return Hit{PlainWindow, index};
    }
    const auto inside = [&](const Reader::TextLayout& layout, const Reader::ScreenRect& r, float slop) {
        const float sx = picture.width / layout.space_width;
        const float sy = picture.height / layout.space_height;
        return x >= picture.x + r.x * sx - slop && x < picture.x + (r.x + r.width) * sx + slop &&
               y >= picture.y + r.y * sy - slop && y < picture.y + (r.y + r.height) * sy + slop;
    };
    if (picture.width <= 0 || picture.height <= 0) {
        return std::nullopt;
    }
    // The character under the point; with a finger's width, the nearest within a few pixels.
    for (const float slop : {0.0f, 6.0f}) {
        for (const auto& [name, window] : windows) {
            const Reader::TextLayout& layout = window.layout;
            if (layout.closed || layout.sequence != window.sequence || layout.space_width <= 0 ||
                layout.space_height <= 0) {
                continue;
            }
            for (std::size_t i = 0; i < layout.glyphs.size() && i < window.text.size(); ++i) {
                if (layout.glyphs[i] && window.text[i] != U'\n' && inside(layout, *layout.glyphs[i], slop)) {
                    return Hit{name, i};
                }
            }
        }
    }
    return std::nullopt;
}

void LookupLayer::LookUp(const Hit& hit) {
    const std::u32string& text = TextOf(hit.window);
    // From the character to the end of its line, at most the scan's length.
    std::u32string from;
    for (std::size_t i = hit.index; i < text.size() && text[i] != U'\n' &&
                                    from.size() < static_cast<std::size_t>(ReaderLookup::Dictionary::MaxScanLength);
         ++i) {
        from.push_back(text[i]);
    }
    pending = Request{hit.window, plain ? 0 : windows[hit.window].sequence, hit.index, Utf8Of(from)};
    asked = hit;
    wake.notify_all();
}

void LookupLayer::Close() {
    answer.reset();
    asked.reset();
    pending.reset();
}

void LookupLayer::Work() {
    std::unique_lock lock{mutex};
    while (true) {
        wake.wait(lock, [this] { return stopping || pending.has_value(); });
        if (stopping) {
            return;
        }
        const Request request = *pending;
        pending.reset();
        if (!dictionary.IsOpen()) {
            continue;
        }
        // The dictionary is this thread's alone while it looks up; the rest waits for nothing.
        working = true;
        lock.unlock();
        const auto start = std::chrono::steady_clock::now();
        const std::vector<ReaderLookup::LookupResult> results = dictionary.Lookup(request.text, MaxResults);
        auto entries = Entries(results);
        const int ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - start)
                                            .count());
        lock.lock();
        working = false;
        // A newer request, or the popup closed meanwhile: this answer is for nobody.
        if (pending || !asked || asked->window != request.window || asked->index != request.index) {
            continue;
        }
        note = "lookup " + request.text + ": " +
               (results.empty() ? std::string("nothing") : results.front().expression) + " (" +
               std::to_string(results.size()) + " words, " + std::to_string(ms) + " ms)";
        if (results.empty()) {
            answer.reset();
            continue;
        }
        scroll = 0;
        scroll_limit = 0;
        answer = Answer{request, CodepointsFor(TextOf(request.window), request.index, results.front().matched_length),
                        std::move(entries), CardOf(results.front())};
        word_shown = results.front().expression;
    }
}

void LookupLayer::Toggle(std::uint64_t now_us) {
    pointer_on = !pointer_on;
    rear_down = false;
    pointer_seen_until_us = pointer_on ? now_us + PointerShownUs : 0;
    note = pointer_on ? "pointer on" : "pointer off";
}

bool LookupLayer::Over(const std::u32string& text, Plain& view) const {
    const auto found = windows.find(latest_window);
    if (found == windows.end() || picture.width <= 0) {
        return false;
    }
    const Reader::TextLayout& layout = found->second.layout;
    if (layout.closed || layout.space_width <= 0 || layout.space_height <= 0) {
        return false;
    }
    // Where the game has the line: its box if the script knows it, else what its characters cover.
    const float sx = picture.width / layout.space_width;
    const float sy = picture.height / layout.space_height;
    float left = 960, top = 544, right = 0, bottom = 0, tallest = 0;
    const auto take = [&](const Reader::ScreenRect& r) {
        left = std::min(left, picture.x + r.x * sx);
        top = std::min(top, picture.y + r.y * sy);
        right = std::max(right, picture.x + (r.x + r.width) * sx);
        bottom = std::max(bottom, picture.y + (r.y + r.height) * sy);
    };
    for (const auto& glyph : layout.glyphs) {
        if (glyph) {
            take(*glyph);
            tallest = std::max(tallest, glyph->height * sy);
        }
    }
    if (layout.box) {
        take(*layout.box);
    }
    if (right <= left) {
        return false;
    }
    // A size up from the game's, in as many rows as that takes, from where its text begins.
    view.cell = std::clamp(std::floor(tallest * 1.25f), 26.0f, 44.0f);
    while (true) {
        view.row = view.cell * PlainRowFactor;
        const float room = std::max(right - left, PlainOverWidth);
        view.columns = static_cast<int>(room / view.cell);
        const int rows = (static_cast<int>(text.size()) + view.columns - 1) / view.columns;
        if (rows * view.row + 2 * PlainOverPadding <= 544 - 16) {
            const float wide = std::min(view.columns, static_cast<int>(text.size())) * view.cell;
            const float box_left = std::clamp(left - PlainOverPadding, 8.0f, 952.0f - wide - 2 * PlainOverPadding);
            const float box_top =
                std::clamp(top - PlainOverPadding, 8.0f, 536.0f - rows * view.row - 2 * PlainOverPadding);
            view.left = box_left + PlainOverPadding;
            view.top = box_top + PlainOverPadding;
            // All of the game's own line is under it, however little of it there is here.
            const float box_right = std::max(view.left + wide, right) + PlainOverPadding;
            const float box_bottom = std::max(view.top + rows * view.row, bottom) + PlainOverPadding;
            view.box = {box_left, box_top, box_right - box_left, box_bottom - box_top};
            return true;
        }
        if (view.cell <= 22) {
            return false;
        }
        view.cell -= 4;
    }
}

void LookupLayer::TogglePlain(std::uint64_t now_us) {
    Close();
    if (plain) {
        plain.reset();
        note = "plain view closed";
        return;
    }
    history_back = 0;
    ShowPlain(latest, now_us);
}

void LookupLayer::ShowPlain(const std::string& line, std::uint64_t now_us) {
    // One run of text: a word the game's own lines broke in two is one word here.
    std::u32string text;
    for (const char32_t c : Codepoints(line)) {
        if (c != U'\n') {
            text.push_back(c);
        }
    }
    if (text.empty()) {
        note = "no line to show yet";
        return;
    }
    Plain view;
    if (!Over(text, view)) {
        for (const float cell : PlainCells) {
            view.cell = cell;
            view.row = cell * PlainRowFactor;
            view.columns = static_cast<int>((960 - 2 * PlainMarginX) / view.cell);
            const int fit = static_cast<int>((544 - 2 * PlainMarginY) / view.row);
            if (text.size() <= static_cast<std::size_t>(view.columns) * fit) {
                break;
            }
        }
        const std::size_t room =
            static_cast<std::size_t>(view.columns) * static_cast<int>((544 - 2 * PlainMarginY) / view.row);
        text.resize(std::min(text.size(), room));
        // In the middle of the screen, however little there is of it.
        const int rows = (static_cast<int>(text.size()) + view.columns - 1) / view.columns;
        const int wide = std::min(view.columns, static_cast<int>(text.size()));
        view.left = (960 - wide * view.cell) / 2;
        view.top = (544 - rows * view.row) / 2;
        view.box = {view.left - PlainPadding, view.top - PlainPadding, wide * view.cell + 2 * PlainPadding,
                    rows * view.row + 2 * PlainPadding};
    }
    view.text = std::move(text);
    plain = std::move(view);
    pointer_seen_until_us = now_us + PointerShownUs;
    note = "plain view: " + line;
}

void LookupLayer::SetPointerSpeed(float screens_per_pad) {
    std::scoped_lock lock{mutex};
    pointer_speed = std::clamp(screens_per_pad, 0.25f, 8.0f);
}

void LookupLayer::TogglePointer(std::uint64_t now_us) {
    std::scoped_lock lock{mutex};
    Toggle(now_us);
}

void LookupLayer::Pad(const PadInput& pad, std::uint64_t now_us) {
    std::scoped_lock lock{mutex};
    const float seconds = pad_us == 0 ? 0.0f : static_cast<float>(now_us - pad_us) / 1e6f;
    pad_us = now_us;
    const bool up = pad.up || pad.stick_y < -StickStep;
    const bool down = pad.down || pad.stick_y > StickStep;
    const bool up_edge = up && !up_down;
    const bool down_edge = down && !down_down;
    up_down = up;
    down_down = down;
    const bool close_edge = pad.close && !close_down;
    const bool add_edge = pad.add && !add_down;
    close_down = pad.close;
    add_down = pad.add;
    if (answer) {
        const float lean = std::abs(pad.stick_y) < StickDead ? 0.0f : pad.stick_y;
        scroll = std::clamp(scroll + lean * ScrollLinesPerSecond * std::min(seconds, 0.1f), 0.0f,
                            static_cast<float>(scroll_limit));
        if (add_edge && !answer->entries.empty()) {
            note = "add: " + answer->entries.front().second;
            card_asked = answer->card;
            card_asked->sentence = SentenceHtml(TextOf(answer->request.window), answer->request.index, answer->length);
        }
    }
    if (plain && !answer && !history.empty() && (up_edge || down_edge)) {
        const std::size_t before = history_back;
        if (up_edge && history_back + 1 < history.size()) {
            ++history_back;
        } else if (down_edge && history_back > 0) {
            --history_back;
        }
        if (history_back != before) {
            ShowPlain(history[history.size() - 1 - history_back], now_us);
            status = history_back == 0 ? std::string() : std::to_string(history_back) + " back";
            status_until_us = now_us + StatusUs;
        }
    }
    if (answer || plain) {
        holding = true;
        if (close_edge) {
            if (answer) {
                Close();
            } else {
                plain.reset();
                note = "plain view closed";
            }
        }
    } else if (!pad.any) {
        holding = false;
    }
}

bool LookupLayer::Modal() const {
    std::scoped_lock lock{mutex};
    return answer.has_value() || plain.has_value() || holding;
}

std::optional<std::string> LookupLayer::TakeWordShown() {
    std::scoped_lock lock{mutex};
    std::optional<std::string> out;
    out.swap(word_shown);
    return out;
}

void LookupLayer::SetInDeck(const std::string& expression, bool in_deck) {
    std::scoped_lock lock{mutex};
    if (answer && answer->card.expression == expression) {
        answer->in_deck = in_deck;
    }
}

std::optional<CardRequest> LookupLayer::TakeCard() {
    std::scoped_lock lock{mutex};
    std::optional<CardRequest> out;
    out.swap(card_asked);
    return out;
}

void LookupLayer::SetSessionLine(const std::string& text) {
    std::scoped_lock lock{mutex};
    session_line = text;
}

void LookupLayer::SetStatus(const std::string& text, std::uint64_t until_us) {
    std::scoped_lock lock{mutex};
    status = text;
    status_until_us = until_us;
}

bool LookupLayer::TextOnScreen() const {
    std::scoped_lock lock{mutex};
    for (const auto& [name, window] : windows) {
        if (!window.layout.closed && window.layout.sequence == window.sequence && !window.layout.glyphs.empty()) {
            return true;
        }
    }
    return false;
}

void LookupLayer::SetScrollLimit(int lines) {
    std::scoped_lock lock{mutex};
    scroll_limit = std::max(0, lines);
    scroll = std::min(scroll, static_cast<float>(scroll_limit));
}

void LookupLayer::Input(const Touch& front, const Touch& rear, std::uint64_t now_us) {
    std::scoped_lock lock{mutex};
    // The front screen: a tap (down and up nearly in one place) on a character, or beside the
    // text to close the popup.
    if (front.fingers > 0) {
        if (!front_down) {
            front_down = true;
            front_start_x = front.x;
            front_start_y = front.y;
        }
        front_x = front.x;
        front_y = front.y;
    } else if (front_down) {
        front_down = false;
        if (std::hypot(front_x - front_start_x, front_y - front_start_y) < TapSlop) {
            if (const auto hit = HitTest(front_start_x, front_start_y)) {
                LookUp(*hit);
            } else {
                note = "tap at " + std::to_string(static_cast<int>(front_start_x)) + "," +
                       std::to_string(static_cast<int>(front_start_y)) + ": no character there";
                Close();
            }
        }
    }

    // The rear pad: a trackpad. Where the finger lands does not matter, only how it moves.
    if (rear.fingers > 0) {
        if (!rear_down) {
            rear_down = true;
            rear_moved = false;
            rear_most_fingers = rear.fingers;
            rear_start_x = rear_x = rear.x;
            rear_start_y = rear_y = rear.y;
            rear_start_us = now_us;
            return;
        }
        rear_most_fingers = std::max(rear_most_fingers, rear.fingers);
        if (std::hypot(rear.x - rear_start_x, rear.y - rear_start_y) > PadTapSlop) {
            rear_moved = true;
        }
        const float dx = rear.x - rear_x;
        const float dy = rear.y - rear_y;
        rear_x = rear.x;
        rear_y = rear.y;
        // Two fingers are a tap to come, not a pointer to move.
        if (!PointerWorks() || rear_most_fingers > 1 || (dx == 0 && dy == 0)) {
            return;
        }
        // The same on both axes: a stroke across the pad's width takes the pointer across the
        // screen `pointer_speed` times.
        const float gain = pointer_speed * 960.0f / std::max(1.0f, rear_area.max_x - rear_area.min_x);
        pointer_x = std::clamp(pointer_x + dx * gain, 0.0f, 959.0f);
        pointer_y = std::clamp(pointer_y + dy * gain, 0.0f, 543.0f);
        pointer_seen_until_us = now_us + PointerShownUs;
        const auto hit = HitTest(pointer_x, pointer_y);
        if (hit && (!asked || asked->window != hit->window || asked->index != hit->index)) {
            LookUp(*hit);
        }
    } else if (rear_down) {
        rear_down = false;
        const std::uint64_t held = now_us - rear_start_us;
        if (rear_most_fingers > 1) {
            if (!rear_moved && held < TwoFingerTapUs) {
                Toggle(now_us);
            }
            return;
        }
        const bool tap = !rear_moved && held < PadTapUs;
        if (tap && rear_tap_us && rear_start_us - *rear_tap_us < DoubleTapUs &&
            std::hypot(rear_x - rear_tap_x, rear_y - rear_tap_y) < DoubleTapSlop) {
            rear_tap_us.reset();
            TogglePlain(now_us);
            return;
        }
        rear_tap_us = tap ? std::optional{now_us} : std::nullopt;
        rear_tap_x = rear_x;
        rear_tap_y = rear_y;
        // A tap while the pointer shows (a hand holding the Vita touches the pad by chance, and
        // that must not close the popup): the word where the pointer is, or nothing there: the
        // popup goes.
        if (PointerWorks() && tap && rear_start_us < pointer_seen_until_us) {
            pointer_seen_until_us = now_us + PointerShownUs;
            if (const auto hit = HitTest(pointer_x, pointer_y)) {
                LookUp(*hit);
            } else {
                Close();
            }
        }
    }
}

void LookupLayer::Scene(ReaderScene& scene, std::uint64_t now_us) const {
    std::scoped_lock lock{mutex};
    std::memset(&scene, 0, sizeof(scene));
    scene.struct_size = sizeof(scene);
    std::memcpy(scene.session, session_line.data(), std::min(session_line.size(), sizeof(scene.session) - 1));
    if (now_us < status_until_us) {
        std::memcpy(scene.status, status.data(), std::min(status.size(), sizeof(scene.status) - 1));
    }
    if (plain) {
        scene.plain_shown = 1;
        scene.plain_columns = plain->columns;
        scene.plain_left = plain->left;
        scene.plain_top = plain->top;
        scene.plain_cell = plain->cell;
        scene.plain_row = plain->row;
        scene.plain_box = plain->box;
        const std::string utf8 = Utf8Of(plain->text);
        std::memcpy(scene.plain_text, utf8.data(), std::min(utf8.size(), sizeof(scene.plain_text) - 1));
    }
    if (PointerWorks() && now_us < pointer_seen_until_us) {
        scene.pointer_shown = 1;
        scene.pointer_x = pointer_x;
        scene.pointer_y = pointer_y;
        scene.pointer_alpha =
            std::min(1.0f, static_cast<float>(pointer_seen_until_us - now_us) / static_cast<float>(PointerFadeUs));
    }
    if (!answer) {
        return;
    }
    // Where each character of the answer's text is on the screen: the plain view's grid, or the
    // window's layout over the picture.
    const Reader::TextLayout* layout = nullptr;
    if (answer->request.window != PlainWindow || !plain) {
        const auto found = windows.find(answer->request.window);
        if (found == windows.end() || picture.width <= 0) {
            return;
        }
        layout = &found->second.layout;
        if (layout->space_width <= 0 || layout->space_height <= 0) {
            return;
        }
    }
    const std::size_t count = layout ? layout->glyphs.size() : plain->text.size();
    const auto cell = [&](std::size_t i) -> std::optional<ReaderSceneRect> {
        if (!layout) {
            const std::size_t column = i % plain->columns, row = i / plain->columns;
            return ReaderSceneRect{plain->left + column * plain->cell, plain->top + row * plain->row, plain->cell,
                                   plain->row};
        }
        if (!layout->glyphs[i]) {
            return std::nullopt;
        }
        const Reader::ScreenRect& r = *layout->glyphs[i];
        const float sx = picture.width / layout->space_width;
        const float sy = picture.height / layout->space_height;
        return ReaderSceneRect{picture.x + r.x * sx, picture.y + r.y * sy, r.width * sx, r.height * sy};
    };
    float left = 960, top = 544, right = 0, bottom = 0;
    for (std::size_t i = answer->request.index;
         i < answer->request.index + answer->length && i < count && scene.mark_count < READER_SCENE_MARKS; ++i) {
        const auto rect = cell(i);
        if (!rect) {
            continue;
        }
        ReaderSceneRect& mark = scene.marks[scene.mark_count++];
        mark = *rect;
        left = std::min(left, mark.x);
        top = std::min(top, mark.y);
        right = std::max(right, mark.x + mark.width);
        bottom = std::max(bottom, mark.y + mark.height);
    }
    if (scene.mark_count == 0) {
        return;
    }
    scene.popup_shown = 1;
    scene.word = {left, top, right - left, bottom - top};
    scene.scroll = static_cast<int>(scroll);
    // All the text of the window, which the popup is kept off where there is room.
    float line_left = left, line_top = top, line_right = right, line_bottom = bottom;
    for (std::size_t i = 0; i < count; ++i) {
        if (const auto rect = cell(i)) {
            line_left = std::min(line_left, rect->x);
            line_top = std::min(line_top, rect->y);
            line_right = std::max(line_right, rect->x + rect->width);
            line_bottom = std::max(line_bottom, rect->y + rect->height);
        }
    }
    scene.line = {line_left, line_top, line_right - line_left, line_bottom - line_top};
    scene.in_deck = answer->in_deck ? 1 : 0;
    std::size_t used = 0;
    for (const auto& [kind, text] : answer->entries) {
        if (scene.entry_count == READER_SCENE_ENTRIES || used + text.size() + 1 > sizeof(scene.text)) {
            break;
        }
        scene.entries[scene.entry_count++] = {kind, static_cast<std::uint16_t>(used)};
        std::memcpy(scene.text + used, text.data(), text.size());
        used += text.size() + 1; // the NUL is the memset's
    }
}

bool LookupLayer::Showing(std::uint64_t now_us) const {
    std::scoped_lock lock{mutex};
    return (PointerWorks() && now_us < pointer_seen_until_us) || answer.has_value() || plain.has_value() ||
           now_us < status_until_us || !session_line.empty();
}

std::string LookupLayer::TakeNote() {
    std::scoped_lock lock{mutex};
    std::string out;
    out.swap(note);
    return out;
}

std::optional<std::pair<std::string, std::size_t>> LookupLayer::Asked() const {
    std::scoped_lock lock{mutex};
    if (!asked) {
        return std::nullopt;
    }
    return std::pair{asked->window, asked->index};
}

bool LookupLayer::Busy() const {
    std::scoped_lock lock{mutex};
    return pending.has_value() || working;
}

} // namespace VitaReader
