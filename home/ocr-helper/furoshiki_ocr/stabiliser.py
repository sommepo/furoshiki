"""When to read the text area: once it has stopped changing.

Each frame's text area is turned into a small greyscale signature. Text that types itself out
changes the area from frame to frame; when it has not changed for `settle_ms`, the area is
read once. Small movement and anything that blinks or bounces (a menu's cursor, a "next" arrow:
the area goes back to a look it had a moment ago) is not counted as change, a slow fade is (the
area drifts away from the look it had when it last changed), an area with
almost nothing in it (the dialogue box closed) is not read, and neither is an area that looks
the same as the last one that was read.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass, field

from PIL import Image, ImageChops, ImageStat

# Width of the signature; the height follows the area's shape.
SIGNATURE_WIDTH = 320
# A signature pixel counts as changed when it moved by more than this (0-255).
PIXEL_THRESHOLD = 40


@dataclass
class StabiliserSettings:
    settle_ms: int = 400
    # Changes smaller than this share of the area are movement, not new text. At 4 frames a
    # second a line typing itself out changes a few percent of the box between frames.
    min_change: float = 0.004
    # An area whose greyscale standard deviation is below this holds no text.
    blank_std: float = 6.0
    # How many recent frames a new one is compared with (2 s at 4 fps). Something that blinks
    # or bounces (a menu's cursor, a "next" arrow) goes back to a look it had a moment ago;
    # new text never does, so only a look none of these frames had counts as change.
    history: int = 8
    # A slow fade (one line fading into the next over a second or two) moves each pixel less
    # than PIXEL_THRESHOLD a frame, so no frame is unlike the ones before it; once the area is
    # this far from its look when it last changed, that is change too. Well above a blinking
    # cursor or arrow (about 0.5 %), well below a new line (several percent).
    drift: float = 0.02
    # An area that never settles (the area reaches into an animated scene, 2026-10-28: a
    # character's hair moving along its bottom edge, 2 % a frame) is read anyway this long after
    # it began to change, and again every so long while it goes on: the same text again is
    # dropped as the same line, so only a new line is kept.
    restless_ms: int = 2500


@dataclass
class Signature:
    image: Image.Image
    std: float

    @classmethod
    def of(cls, area: Image.Image) -> "Signature":
        grey = area.convert("L")
        height = max(1, round(grey.height * SIGNATURE_WIDTH / max(1, grey.width)))
        small = grey.resize((SIGNATURE_WIDTH, height), Image.Resampling.BILINEAR)
        return cls(small, ImageStat.Stat(small).stddev[0])

    def changed_share(self, other: "Signature | None") -> float:
        """The share of the area that differs from `other` (1.0 when there is none to compare)."""
        if other is None or other.image.size != self.image.size:
            return 1.0
        diff = ImageChops.difference(self.image, other.image)
        histogram = diff.point(lambda v: 255 if v > PIXEL_THRESHOLD else 0).histogram()
        total = self.image.width * self.image.height
        return histogram[255] / total


@dataclass
class Decision:
    read: bool
    reason: str
    # The area took a look it had not had in the last frames (new text, a closed box).
    changed: bool = False


@dataclass
class Stabiliser:
    settings: StabiliserSettings = field(default_factory=StabiliserSettings)
    # (time, signature) of the last `history` frames.
    _recent: deque = field(default_factory=deque)
    _changed_at: float = 0.0
    # The area's look when it last changed.
    _anchor: Signature | None = None
    # When the look being settled began to change (its first change since the last decision):
    # where a line's typing or fade started, for its sound.
    _started: float | None = None
    # When the area was last read while it would not settle.
    _restless_at: float | None = None
    _armed: bool = True
    _last_read: Signature | None = None

    def reset(self) -> None:
        """Forget everything: a new area, or the picture came back after a gap."""
        self._recent.clear()
        self._anchor = None
        self._started = None
        self._restless_at = None
        self._armed = True
        self._last_read = None

    def offer(self, signature: Signature, now_ms: float) -> Decision:
        """Called for every frame's area; says whether to read this one."""
        changed = all(signature.changed_share(seen) > self.settings.min_change
                      for _, seen in self._recent)
        if not changed and self._anchor is not None:
            changed = signature.changed_share(self._anchor) > self.settings.drift
        if changed:
            if not self._armed or self._started is None:
                self._started = now_ms
            self._changed_at = now_ms
            self._anchor = signature
            self._armed = True
        self._recent.append((now_ms, signature))
        while len(self._recent) > max(1, self.settings.history):
            self._recent.popleft()
        if not self._armed:
            return Decision(False, "unchanged", changed)
        if now_ms - self._changed_at < self.settings.settle_ms:
            since = max(now_ms if self._started is None else self._started,
                        self._restless_at if self._restless_at is not None else -1e18)
            if (self.settings.restless_ms and now_ms - since >= self.settings.restless_ms
                    and signature.std >= self.settings.blank_std):
                self._restless_at = now_ms
                self._last_read = signature
                return Decision(True, "still moving", changed)
            return Decision(False, "settling", changed)
        self._restless_at = None
        self._armed = False
        if signature.std < self.settings.blank_std:
            # Text after a closed box is new text, even if it looks like the last line, and
            # what the box showed before it closed is no longer a look to go back to.
            self._last_read = None
            self._recent = deque([(now_ms, signature)])
            return Decision(False, "blank", changed)
        # Since it last changed, has the area looked as it did when it was read last (the
        # cursor on or off)? Then it is that text again.
        settled = [seen for at, seen in self._recent if at >= self._changed_at]
        if self._last_read is not None and any(
                seen.changed_share(self._last_read) <= self.settings.min_change for seen in settled):
            return Decision(False, "same as last read", changed)
        self._last_read = signature
        return Decision(True, "settled", changed)

    @property
    def started(self) -> float | None:
        """When the area's current look began to change (ms, the frames' clock), if known."""
        return self._started

    def mark_read(self, signature: Signature) -> None:
        """An area read on request (OCR now) is the last one read too."""
        self._last_read = signature
        self._armed = False
