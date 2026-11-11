"""From what the OCR engine saw to one line of text, and whether it is a new line.

The engine gives lines with boxes (0-1 coordinates of the area). Readings printed small above
kanji (furigana) are dropped, the remaining lines are joined without spaces and only Japanese
is kept (a line with no kana or kanji goes, and so do letters, arrows and stray marks). The
result is compared with the previous line: the same line again is dropped, and a line that
carries on the previous one (text that was still typing itself out when it was read)
replaces it.
"""

from __future__ import annotations

import difflib
import re
import unicodedata
from dataclasses import dataclass

KANA = re.compile(r"^[぀-ヿーｰ-ﾟ・ー〜]+$")
JAPANESE = re.compile(r"[぀-ヿ㐀-䶿一-鿿豈-﫿ｦ-ﾟ]")
# Anything but kana, kanji, Japanese and full-width punctuation (、。「」！？…), and digits.
NOT_JAPANESE = re.compile(r"[^぀-ヿ㐀-䶿一-鿿豈-﫿ｦ-ﾟ、-〿！-／０-９：-＠［-｀｛-･0-9…‥―♪]")
SPACES = re.compile(r"\s+")
FULL_WIDTH_MARKS = str.maketrans({"!": "！", "?": "？"})
# Arrows, cursors and the like that games put at the end of a box.
TRAILING_MARKS = re.compile(r"[\s▼▽▲△►▶▸▹◆◇■□●○◀◁…・.。]*[▼▽▲△►▶▸▹◆◇■□●○◀◁]+[\s]*$")


@dataclass
class OcrLine:
    text: str
    left: float
    top: float
    right: float
    bottom: float
    vertical: bool = False
    # Each character with its box, [char, left, top, right, bottom], when the engine gave them.
    chars: list | None = None

    @property
    def height(self) -> float:
        return self.bottom - self.top

    @property
    def width(self) -> float:
        return self.right - self.left


def drop_doubles(lines: list[OcrLine]) -> list[OcrLine]:
    """Drops a line the engine gave twice, the second time starting further out (the user,
    2026-11-06: over a see-through box meikiocr gave ちょっとアタシが歌うから and, in the same
    place, ゃで「ちょっとアタシが歌うから, the grass before it read as text, and both were
    joined): of two lines on the same row, one inside the other (by 80 % of its width) and its
    text in the other's, the inner one is kept."""
    gone: set[int] = set()
    for i, inner in enumerate(lines):
        for j, outer in enumerate(lines):
            if i == j or j in gone or inner.vertical or outer.vertical or len(outer.text) < len(inner.text):
                continue
            rows = min(inner.bottom, outer.bottom) - max(inner.top, outer.top)
            across = min(inner.right, outer.right) - max(inner.left, outer.left)
            if (rows >= 0.5 * min(inner.height, outer.height) and across >= 0.8 * inner.width
                    and _key(normalise(inner.text)) in _key(normalise(outer.text))):
                gone.add(j)
                break
    return [line for n, line in enumerate(lines) if n not in gone]


def drop_furigana(lines: list[OcrLine]) -> list[OcrLine]:
    """Drops kana-only lines that are much smaller than the text and sit right against it
    (and a line read twice, `drop_doubles`)."""
    lines = drop_doubles(lines)
    if len(lines) < 2:
        return lines
    horizontal = [line for line in lines if not line.vertical]
    if not horizontal:
        return lines
    tallest = max(line.height for line in horizontal)
    kept = []
    for line in lines:
        small = not line.vertical and line.height < 0.65 * tallest
        if small and _kana_only(line.text) and _sits_on_text(line, lines):
            continue
        kept.append(line)
    return kept


def _kana_only(text: str) -> bool:
    """Kana and nothing else Japanese: the engine's stray marks, digits and letters aside (the
    user, 2026-11-05: Ocarina of Time Remake's いっしょ　えんそう over a see-through box read as
    いっしょう、えんそう, and a ruby with a comma in it was taken for a line). ー read as 一
    counts as kana, and from four characters one in four may be a kanji misread (がっき も as
    がっま打る4の)."""
    japanese = "".join(JAPANESE.findall(text)).replace("一", "ー")  # ー is often read as 一
    if not japanese:
        return False
    kana = sum(bool(KANA.match(char)) for char in japanese)
    return kana == len(japanese) or (len(japanese) >= 4 and kana >= 0.75 * len(japanese))


def _sits_on_text(ruby: OcrLine, lines: list[OcrLine]) -> bool:
    for line in lines:
        if line is ruby or line.height <= ruby.height:
            continue
        gap = line.top - ruby.bottom
        overlaps = min(line.right, ruby.right) - max(line.left, ruby.left) > 0
        if overlaps and -0.5 * ruby.height <= gap <= 1.5 * ruby.height:
            return True
    return False


def join(lines: list[OcrLine]) -> str:
    """The Japanese lines as one line of Japanese text: top to bottom (right to left when
    vertical). A line with no kana or kanji (English, a number, a button label) is left out."""
    return join_with_boxes(lines)[0]


Box = tuple[float, float, float, float]


def join_with_boxes(lines: list[OcrLine]) -> tuple[str, list[Box]]:
    """`join`, and where each character of the result is: (left, top, right, bottom) in the
    same 0-1 coordinates as the lines, one per character (code point) of the text."""
    if not lines:
        return "", []
    if all(line.vertical for line in lines):
        ordered = sorted(lines, key=lambda line: -line.right)
    else:
        ordered = sorted(lines, key=lambda line: (round(line.top / max(line.height, 1e-3)), line.left))
    pairs: list[tuple[str, Box]] = []
    for line in ordered:
        text = normalise(line.text)
        if JAPANESE.search(text):
            pairs += _normalised_pairs(line, text)
    out = "".join(char for char, _ in pairs)
    trailing = TRAILING_MARKS.search(out)
    if trailing:
        pairs = pairs[: trailing.start()]
    pairs = [(char, box) for char, box in pairs if not NOT_JAPANESE.match(char)]
    return "".join(char for char, _ in pairs), [box for _, box in pairs]


def _normalised_pairs(line: OcrLine, text: str) -> list[tuple[str, Box]]:
    """`normalise` done on the characters with their boxes. `text` is normalise(line.text);
    when the boxes cannot be followed through it, the line's box is shared out evenly."""
    if line.chars and "".join(c[0] for c in line.chars) == line.text:
        pairs = [(" " if c[0].isspace() else c[0], tuple(c[1:5])) for c in line.chars]
        collapsed: list[tuple[str, Box]] = []
        for char, box in pairs:
            if char == " " and collapsed and collapsed[-1][0] == " ":
                continue
            collapsed.append((char, box))
        while collapsed and collapsed[0][0] == " ":
            collapsed.pop(0)
        while collapsed and collapsed[-1][0] == " ":
            collapsed.pop()
        kept = []
        for index, (char, box) in enumerate(collapsed):
            if char == " ":
                before = collapsed[index - 1][0] if index else ""
                after = collapsed[index + 1][0] if index + 1 < len(collapsed) else ""
                if (before and ord(before) > 0x7f) or (after and ord(after) > 0x7f):
                    continue
            kept.append((char.translate(FULL_WIDTH_MARKS), box))
        if "".join(char for char, _ in kept) == text:
            return kept
    count = max(1, len(text))
    if line.vertical:
        step = line.height / count
        return [(char, (line.left, line.top + n * step, line.right, line.top + (n + 1) * step))
                for n, char in enumerate(text)]
    step = line.width / count
    return [(char, (line.left + n * step, line.top, line.left + (n + 1) * step, line.bottom))
            for n, char in enumerate(text)]


def normalise(text: str) -> str:
    text = unicodedata.normalize("NFKC", text) if not JAPANESE.search(text) else text
    text = SPACES.sub(" ", text).strip()
    if JAPANESE.search(text):
        # Japanese has no spaces between words; the engine's spaces are layout.
        text = re.sub(r"(?<=[^\x00-\x7f]) | (?=[^\x00-\x7f])", "", text)
        # Engines often give half-width marks; Japanese text uses the full-width ones.
        text = text.translate(FULL_WIDTH_MARKS)
    return text


def is_junk(text: str) -> bool:
    """Nothing Japanese in it: stray marks, numbers, letters."""
    return not JAPANESE.search(text)


def _key(text: str) -> str:
    return re.sub(r"[\s、。，．,.!！?？「」『』…・〜ー\-]", "", text)


@dataclass
class Verdict:
    action: str  # "new", "replace", "drop"
    reason: str = ""


def duplicates(texts: list[tuple[int, str]], similarity: float = 0.9, near_from: int = 10) -> list[int]:
    """Of lines (id, text) in order, the ids that repeat another: the same text (spaces and
    punctuation aside), the first kept; or, for lines of `near_from` characters or more, the
    same line read with a character or so different, the longer kept, unless what it has
    more of are stray characters round the line (`_round_it`)."""
    kept: list[tuple[int, str]] = []
    exact: set[str] = set()
    drop: list[int] = []
    for line_id, text in texts:
        key = _key(text)
        if not key:
            continue
        if key in exact:
            drop.append(line_id)
            continue
        twin = None
        if len(key) >= near_from:
            twin = next((i for i, (_, other) in enumerate(kept) if len(other) >= near_from
                         and difflib.SequenceMatcher(None, other, key, autojunk=False).ratio() >= similarity), None)
        strays = _round_it(kept[twin][1], key) if twin is not None else None
        if twin is None:
            kept.append((line_id, key))
            exact.add(key)
        elif strays and strays.action == "drop":
            drop.append(line_id)  # the same line with stray characters round it
        elif (strays and strays.action == "replace") or len(key) > len(kept[twin][1]):
            drop.append(kept[twin][0])  # the longer reading stays
            exact.discard(kept[twin][1])
            kept[twin] = (line_id, key)
            exact.add(key)
        else:
            drop.append(line_id)
    return drop


def _found_in(key: str, others: list[str], block: int) -> float:
    """The share of `key`'s characters found in `others`, in runs of `block` or more."""
    covered = [False] * len(key)
    for other in others:
        for _, at, size in difflib.SequenceMatcher(None, other, key, autojunk=False).get_matching_blocks():
            if size >= block:
                covered[at:at + size] = [True] * size
    return sum(covered) / len(key) if key else 0.0


def scrolled(lines: list[tuple[int, str, int]], window: int = 8, seconds: float = 120, block: int = 3,
             share: float = 0.9, shortest: int = 6) -> list[int]:
    """Of lines (id, text, time in ms) in order, the ids of those whose text is all in the
    lines around them (the user, 2026-10-28: text scrolling up the screen read again at each
    step): within `window` lines and `seconds` either side, `share` of its characters found
    in their text in runs of `block` or more. A step's read that is a piece of another (the
    box cut mid-scroll) or spans two others (the end of one, the start of the next) goes; the
    shortest first, each against the lines still kept, so whatever goes is kept elsewhere.
    Lines under `shortest` characters only by `duplicates`: はい inside はい、行こう is a line."""
    keys = [(line_id, _key(text), at) for line_id, text, at in lines]
    kept = [bool(key) for _, key, _ in keys]
    drop: list[int] = []
    for n in sorted(range(len(keys)), key=lambda n: len(keys[n][1])):
        line_id, key, at = keys[n]
        if len(key) < shortest:
            continue
        near = [keys[m][1] for m in range(max(0, n - window), min(len(keys), n + window + 1))
                if m != n and kept[m] and abs(keys[m][2] - at) <= seconds * 1000]
        if near and _found_in(key, near, block) >= share:
            kept[n] = False
            drop.append(line_id)
    return sorted(drop)


def strays(lines: list[tuple[int, str, int]], seconds: float = 60) -> list[int]:
    """Of lines (id, text, time in ms) in order, the ids of those that are the line before
    them read with stray characters round it, or with more of them (`_round_it`, as a new
    reading is compared live), the next within `seconds`; the reading with fewer is kept."""
    drop: list[int] = []
    kept: tuple[int, str, int] | None = None
    for line_id, text, at in lines:
        key = _key(text)
        if not key:
            continue
        verdict = _round_it(kept[1], key) if kept and at - kept[2] <= seconds * 1000 else None
        if verdict and verdict.action == "drop":
            drop.append(line_id)
            continue
        if verdict and verdict.action == "replace":
            drop.append(kept[0])
        kept = (line_id, key, at)
    return drop


def repeats(lines: list[tuple[int, str, int]]) -> list[int]:
    """Clear duplicates' rule, of lines (id, text, time in ms) in order: `duplicates` across
    them, then `strays` and `scrolled` among the rest."""
    same = duplicates([(line_id, text) for line_id, text, _ in lines])
    gone = set(same)
    near = strays([line for line in lines if line[0] not in gone])
    gone |= set(near)
    return sorted(same + near + scrolled([line for line in lines if line[0] not in gone]))


def _pairs(text: str) -> list[str]:
    """The text's neighbouring pairs of Japanese characters (digits and the rest left out), or
    its one character."""
    chars = "".join(JAPANESE.findall(text))
    return [chars[i:i + 2] for i in range(len(chars) - 1)] or ([chars] if chars else [])


def _covered(pairs: list[str], by: set[str]) -> float:
    return sum(pair in by for pair in pairs) / len(pairs) if pairs else 0.0


def like(pattern: str, text: str, share: float = 0.6, back: float = 0.4) -> bool:
    """Whether `text` is a line like `pattern`, one the user chose to ignore (the user,
    2026-10-28: Fire Emblem's battle forecast, 攻撃必殺命中命中必殺攻撃一10, read from the same
    place as the dialogue): `share` of its pairs of characters are the pattern's, and `back`
    of the pattern's are its. Pairs, not characters, or an ignored sentence's kana would match
    other sentences; both ways, or a short line found inside a long pattern would. On 672
    real lines one forecast matched 27 forecasts with other words and numbers, and no
    dialogue."""
    lines, wanted = _pairs(text), _pairs(pattern)
    if not lines or not wanted:
        return False
    return _covered(lines, set(wanted)) >= share and _covered(wanted, set(lines)) >= back


def compare(previous: str | None, new: str, similarity: float = 0.9) -> Verdict:
    """What to do with `new` given the line before it."""
    if not new or is_junk(new):
        return Verdict("drop", "empty")
    if not previous:
        return Verdict("new")
    a, b = _key(previous), _key(new)
    if a == b:
        return Verdict("drop", "same as the previous line")
    if a and b.startswith(a):
        return Verdict("replace", "carries on the previous line")
    if b and a.startswith(b):
        return Verdict("drop", "part of the previous line")
    round_it = _round_it(a, b)
    if round_it:
        return round_it
    if difflib.SequenceMatcher(None, a, b, autojunk=False).ratio() >= similarity:
        # Nearly the same: one character read differently. The longer reading is kept.
        if len(b) > len(a):
            return Verdict("replace", "a better reading of the previous line")
        return Verdict("drop", "nearly the same as the previous line")
    return Verdict("new")


def _span(short: str, long: str, block: int = 3) -> tuple[float, int, int, int]:
    """How `short` is found in `long`: the share of its characters found in runs of `block` or
    more, how many of `long`'s characters come before the first run and after the last, and
    how many between the runs are not `short`'s."""
    runs = [run for run in difflib.SequenceMatcher(None, short, long, autojunk=False).get_matching_blocks()
            if run.size >= block]
    if not runs:
        return 0.0, len(long), len(long), 0
    matched = sum(run.size for run in runs)
    before, after = runs[0].b, len(long) - (runs[-1].b + runs[-1].size)
    return matched / len(short), before, after, len(long) - before - after - matched


def _round_it(a: str, b: str, shortest: int = 6, found: float = 0.9, slack: float = 0.15) -> Verdict | None:
    """The previous line `a` read again with stray characters before it, or `b` that again
    with fewer (the user, 2026-11-05: a see-through text box over a moving scene, whose grass
    and cloth gave で, がの, がに before the same line at each read). The same line when the
    shorter's characters are all but a tenth in the longer, and the longer has no more than
    `slack` of the shorter's length (one character at least) before it and nothing after it;
    the reading with fewer strays is kept, but not one that only lost a character inside the
    line. More after it is the line carrying on (行こう, 行こうか), not strays: three characters
    or more replace the previous line, fewer are left to `compare`'s other rules. Under
    `shortest` characters nothing is said: その and その先 are other lines."""
    if len(a) >= shortest and len(b) > len(a):
        share, before, after, inside = _span(a, b)
        allowed = max(1, int(slack * len(a)))
        if share >= found and before + inside <= allowed:
            if after >= 3 and after > allowed:
                return Verdict("replace", "carries on the previous line, with stray characters before it")
            if not after:
                return Verdict("drop", "the previous line with stray characters before it")
    if len(b) >= shortest and len(a) > len(b):
        share, before, after, inside = _span(b, a)
        if share >= found and not inside and not after and before <= max(1, int(slack * len(b))):
            return Verdict("replace", "the previous line with fewer stray characters")
    return None
