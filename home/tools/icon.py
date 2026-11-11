#!/usr/bin/env python3
"""Makes what Furoshiki's icon needs from icon/icon.json: Android's launcher icons (a resource
and a manifest alias for each colour), the page's copy of the list, and for the default colour
the Windows icon, a picture and an SVG. Everything it writes is kept in Git; run it after
changing icon.json. The desktop draws the icon from icon.json itself (src/app_icon.cpp)."""
import json
import pathlib
import re
import shutil
import struct
import zlib

HOME = pathlib.Path(__file__).resolve().parent.parent
ICON = HOME / "icon"
ANDROID = HOME.parent / "android/app/src/main"
SQUARE = 512
SAFE = 33 / 250  # Android's safe circle, 33 of a 108 layer, over the mark's reach from the middle
SOURCE_NOTE = "made by home/tools/icon.py from home/icon/icon.json"


def rgb(colour):
    return tuple(int(colour[i:i + 2], 16) for i in (1, 3, 5))


def spans(polygon, y):
    """Where one line across the square is inside a polygon."""
    crossings = []
    for (x0, y0), (x1, y1) in zip(polygon, polygon[1:] + polygon[:1]):
        if (y0 > y) != (y1 > y):
            crossings.append(x0 + (y - y0) * (x1 - x0) / (y1 - y0))
    crossings.sort()
    return list(zip(crossings[::2], crossings[1::2]))


def coverage(mark, size, fineness=4):
    """How much of each pixel the mark covers, 0 to 1, a row at a time."""
    scale = SQUARE / size / fineness
    rows = []
    for row in range(size):
        cells = [0] * size
        for line in range(fineness):
            y = (row * fineness + line + .5) * scale
            for polygon in mark:
                for start, end in spans(polygon, y):
                    a = int(start / scale)
                    b = int(end / scale)
                    for x in range(a, min(b, size * fineness)):
                        cells[x // fineness] += 1
        rows.append([c / (fineness * fineness) for c in cells])
    return rows


def png(mark, size, ground, ink):
    g, i = rgb(ground), rgb(ink)
    raw = bytearray()
    for row in coverage(mark, size):
        raw.append(0)
        for c in row:
            raw += bytes(round(g[n] + (i[n] - g[n]) * c) for n in range(3))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def ico(pictures):
    """An .ico of PNGs, which Windows has read since Vista."""
    out = struct.pack("<HHH", 0, 1, len(pictures))
    place = 6 + 16 * len(pictures)
    for size, data in pictures:
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 24, len(data), place)
        place += len(data)
    return out + b"".join(data for _, data in pictures)


def svg(mark, ground, ink):
    shapes = "".join(
        '<polygon points="{}"/>'.format(" ".join(f"{x},{y}" for x, y in polygon)) for polygon in mark)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {SQUARE} {SQUARE}">'
            f'<rect width="{SQUARE}" height="{SQUARE}" fill="{ground}"/><g fill="{ink}">{shapes}</g></svg>\n')


def vector(mark, ink):
    """Android's foreground: the mark in the middle of a 108 layer."""
    path = "".join("M" + " L".join(f"{x},{y}" for x, y in polygon) + " Z " for polygon in mark).strip()
    move = 54 - SQUARE / 2 * SAFE
    return (f'<?xml version="1.0" encoding="utf-8"?>\n<!-- {SOURCE_NOTE} -->\n'
            '<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="108dp" '
            'android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">\n'
            f'    <group android:scaleX="{SAFE:.5f}" android:scaleY="{SAFE:.5f}" '
            f'android:translateX="{move:.3f}" android:translateY="{move:.3f}">\n'
            f'        <path android:fillColor="{ink}" android:pathData="{path}" />\n    </group>\n</vector>\n')


def adaptive(key):
    return (f'<?xml version="1.0" encoding="utf-8"?>\n<!-- {SOURCE_NOTE} -->\n'
            '<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">\n'
            f'    <background android:drawable="@color/icon_ground_{key}" />\n'
            f'    <foreground android:drawable="@drawable/ic_furoshiki_{key}" />\n'
            '    <monochrome android:drawable="@drawable/ic_furoshiki_mark" />\n</adaptive-icon>\n')


def alias(key, default):
    return (f'        <activity-alias\n            android:name=".Icon{key}"\n'
            '            android:targetActivity=".HomeActivity"\n'
            f'            android:enabled="{"true" if key == default else "false"}"\n'
            '            android:exported="true"\n'
            f'            android:icon="@mipmap/ic_furoshiki_{key}">\n'
            '            <intent-filter>\n'
            '                <action android:name="android.intent.action.MAIN" />\n'
            '                <category android:name="android.intent.category.LAUNCHER" />\n'
            '                <category android:name="android.intent.category.LEANBACK_LAUNCHER" />\n'
            '            </intent-filter>\n        </activity-alias>\n')


def android(table):
    mark, colours, default = table["mark"], table["colours"], table["default"]
    res = ANDROID / "res"
    (res / "mipmap-anydpi-v26").mkdir(exist_ok=True)
    (res / "drawable/ic_furoshiki_mark.xml").write_text(vector(mark, "#FFFFFF"))
    grounds = ""
    for colour in colours:
        key = colour["key"]
        (res / f"drawable/ic_furoshiki_{key}.xml").write_text(vector(mark, colour["ink"]))
        (res / f"mipmap-anydpi-v26/ic_furoshiki_{key}.xml").write_text(adaptive(key))
        grounds += f'    <color name="icon_ground_{key}">{colour["ground"]}</color>\n'
    (res / "values/icon_colours.xml").write_text(
        f'<?xml version="1.0" encoding="utf-8"?>\n<!-- {SOURCE_NOTE} -->\n<resources>\n{grounds}</resources>\n')
    manifest = ANDROID / "AndroidManifest.xml"
    text = manifest.read_text()
    begin, end = "<!-- [icons -->\n", "        <!-- icons] -->"
    block = "".join(alias(c["key"], default) for c in colours)
    text, done = re.subn(re.escape(begin) + r".*?" + re.escape(end), lambda _: begin + block + end, text, flags=re.S)
    if done != 1:
        raise SystemExit("the manifest has no [icons ... icons] lines")
    text = re.sub(r'android:icon="@(mipmap|drawable)/ic_(launcher|furoshiki_\d+)"(\n\s+android:label)',
                  lambda m: f'android:icon="@mipmap/ic_furoshiki_{default}"{m.group(3)}', text)
    manifest.write_text(text)
    shutil.copyfile(ICON / "icon.json", ANDROID / "assets/front/icon.json")


def main():
    table = json.loads((ICON / "icon.json").read_text())
    android(table)
    chosen = next(c for c in table["colours"] if c["key"] == table["default"])
    ground, ink, mark = chosen["ground"], chosen["ink"], table["mark"]
    (ICON / "furoshiki.svg").write_text(svg(mark, ground, ink))
    (ICON / "furoshiki-512.png").write_bytes(png(mark, 512, ground, ink))
    (ICON / "furoshiki.ico").write_bytes(ico([(s, png(mark, s, ground, ink)) for s in (16, 24, 32, 48, 64, 128, 256)]))


if __name__ == "__main__":
    main()
