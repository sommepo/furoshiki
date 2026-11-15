#!/usr/bin/env python3
"""Draws the controller map's pads: writes "pads" into controllers.json beside this file.

Each pad is constructed here from geometry, on a 400 by 260 grid, symmetric about x = 200:
an outline (a smooth curve through anchors, or lines and arcs), then its parts in the order
they are painted. A photograph was the reference for proportions (Evan-Amos's public-domain
pictures on Wikimedia Commons); nothing is traced into the file as it was.

What a renderer gets, per pad: {"name", "draw": [item...]}, an item being
  {"k": "body"}   the outline: filled as a raised surface, the heaviest line
  {"k": "part"}   a piece of the body (a lid, a hinge): as the body, a lighter line
  {"k": "line"}   a detail: a soft hairline; "strong": the ink's own; "fill": true: sunk (a screen)
  {"k": "key"}    a button: "id" (controllers.json's inputs), "label" and where it is written
                  ("lx", "ly", "ls": its size) unless "quiet"; "glyph": a path drawn in the
                  label's place (the PlayStation's shapes); "edge": false: no line of its own
with its shape either {"cx", "cy", "r"} or {"d"}: an SVG path of M, L, C and Z only.

  draw.py            writes controllers.json
  draw.py FILE.html  and a page of every pad, to look at
"""
import json
import math
import os
import sys

CX = 200.0


def f(v):
    text = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if text == "-0" else text


class Path:
    def __init__(self):
        self.parts = []

    def M(self, x, y):
        self.parts.append(("M", [(x, y)]))
        return self

    def L(self, x, y):
        self.parts.append(("L", [(x, y)]))
        return self

    def C(self, a, b, c, d, x, y):
        self.parts.append(("C", [(a, b), (c, d), (x, y)]))
        return self

    def Z(self):
        self.parts.append(("Z", []))
        return self

    def add(self, other):
        self.parts += other.parts
        return self

    def d(self):
        return " ".join(cmd + "".join(f" {f(x)} {f(y)}" for x, y in pts) for cmd, pts in self.parts)


def unit(ax, ay):
    n = math.hypot(ax, ay) or 1.0
    return ax / n, ay / n


def poly(points, radius=0.0, closed=True):
    """Straight sides with every corner rounded (a radius, or one for each corner)."""
    n = len(points)
    radii = radius if isinstance(radius, (list, tuple)) else [radius] * n
    path = Path()
    first = True
    for i, (x, y) in enumerate(points):
        if not closed and i in (0, n - 1):
            (path.M if first else path.L)(x, y)
            first = False
            continue
        px, py = points[(i - 1) % n]
        nx, ny = points[(i + 1) % n]
        u1 = unit(px - x, py - y)
        u2 = unit(nx - x, ny - y)
        cos = max(-1.0, min(1.0, u1[0] * u2[0] + u1[1] * u2[1]))
        theta = math.acos(cos)
        r = radii[i]
        if r <= 0 or theta < 1e-3 or abs(theta - math.pi) < 1e-3:
            (path.M if first else path.L)(x, y)
            first = False
            continue
        t = r / math.tan(theta / 2)
        limit = min(math.hypot(px - x, py - y), math.hypot(nx - x, ny - y)) / 2
        if t > limit:
            t = limit
            r = t * math.tan(theta / 2)
        ax, ay = x + u1[0] * t, y + u1[1] * t
        bx, by = x + u2[0] * t, y + u2[1] * t
        h = 4.0 / 3.0 * math.tan((math.pi - theta) / 4) * r
        (path.M if first else path.L)(ax, ay)
        first = False
        path.C(ax - u1[0] * h, ay - u1[1] * h, bx - u2[0] * h, by - u2[1] * h, bx, by)
    if closed:
        path.Z()
    return path


def rrect(x, y, w, h, r):
    return poly([(x, y), (x + w, y), (x + w, y + h), (x, y + h)], r)


def ring(cx, cy, r):
    k = 0.5522847498 * r
    return (Path().M(cx + r, cy).C(cx + r, cy + k, cx + k, cy + r, cx, cy + r).C(cx - k, cy + r, cx - r, cy + k, cx - r, cy)
            .C(cx - r, cy - k, cx - k, cy - r, cx, cy - r).C(cx + k, cy - r, cx + r, cy - k, cx + r, cy).Z())


def spline(points, closed=True):
    """A smooth curve through the points (Catmull-Rom, as cubics)."""
    n = len(points)
    path = Path().M(*points[0])
    for i in range(n if closed else n - 1):
        p0 = points[(i - 1) % n] if (closed or i > 0) else points[i]
        p1, p2 = points[i], points[(i + 1) % n]
        p3 = points[(i + 2) % n] if (closed or i + 2 < n) else p2
        path.C(p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6,
               p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6, p2[0], p2[1])
    if closed:
        path.Z()
    return path


def both(half):
    """The right half's anchors (how far right of the middle, how far down), top to bottom,
    as the whole outline's."""
    right = [(CX + dx, y) for dx, y in half]
    left = [(CX - dx, y) for dx, y in reversed(half) if dx > 0]
    return right + left


def turned(points, cx, cy, degrees):
    a = math.radians(degrees)
    return [(cx + (x - cx) * math.cos(a) - (y - cy) * math.sin(a), cy + (x - cx) * math.sin(a) + (y - cy) * math.cos(a))
            for x, y in points]


# ---- items ----

def body(path):
    return {"k": "body", "d": path.d()}


def part(path):
    return {"k": "part", "d": path.d()}


def line(shape, strong=False, fill=False):
    item = {"k": "line"}
    if strong:
        item["strong"] = True
    if fill:
        item["fill"] = True
    item.update(shape if isinstance(shape, dict) else {"d": shape.d()})
    return item


def dot(cx, cy, r):
    return {"cx": cx, "cy": cy, "r": r}


def key(id, label, shape, at=None, size=8.0, quiet=False, glyph=None, edge=True):
    item = {"k": "key", "id": id, "label": label}
    item.update(shape if isinstance(shape, dict) else {"d": shape.d()})
    if quiet or not label or glyph is not None:
        item["quiet"] = True
    else:
        item["lx"], item["ly"], item["ls"] = at[0], at[1], size
    if glyph is not None:
        item["glyph"] = glyph.d()
    if not edge:
        item["edge"] = False
    return item


def tab(id, label, x, y, w, h, shown, r=8.0, size=6.5):
    """A shoulder button behind the body: what shows of it is `shown` high from its top."""
    return key(id, label, rrect(x, y, w, h, r), at=(x + w / 2, y + shown / 2 + 0.4), size=size)


def stick(id, cx, cy, well, cap):
    out = [line(dot(cx, cy, well))]
    out.append(key(id, id.upper(), dot(cx, cy, cap), quiet=True) if id else line(dot(cx, cy, cap), strong=True))
    out.append(line(dot(cx, cy, cap * 0.6)))
    return out


def face(cx, cy, off, r, north, east, south, west, size=None):
    size = size or r * 0.95
    return [key("north", north, dot(cx, cy - off, r), at=(cx, cy - off), size=size),
            key("east", east, dot(cx + off, cy, r), at=(cx + off, cy), size=size),
            key("south", south, dot(cx, cy + off, r), at=(cx, cy + off), size=size),
            key("west", west, dot(cx - off, cy, r), at=(cx - off, cy), size=size)]


def sony(cx, cy, off, r):
    """The four shapes: each drawn, not written."""
    s = r * 0.5
    tri_y = cy - off + s * 0.12
    triangle = poly([(cx, tri_y - s), (cx + s * 0.92, tri_y + s * 0.62), (cx - s * 0.92, tri_y + s * 0.62)], 0.0)
    circle = ring(cx + off, cy, s * 0.92)
    a = s * 0.72
    cross = (Path().M(cx - a, cy + off - a).L(cx + a, cy + off + a).M(cx + a, cy + off - a).L(cx - a, cy + off + a))
    b = s * 0.78
    square = poly([(cx - off - b, cy - b), (cx - off + b, cy - b), (cx - off + b, cy + b), (cx - off - b, cy + b)], 0.0)
    return [key("north", "△", dot(cx, cy - off, r), glyph=triangle), key("east", "○", dot(cx + off, cy, r), glyph=circle),
            key("south", "×", dot(cx, cy + off, r), glyph=cross), key("west", "□", dot(cx - off, cy, r), glyph=square)]


def arrows(cx, cy, reach, gap, width, r=2.0):
    """Sony's direction buttons: four, each pointing at the middle."""
    w = width / 2
    up = [(cx - w, cy - reach), (cx + w, cy - reach), (cx + w, cy - gap - w * 0.85), (cx, cy - gap), (cx - w, cy - gap - w * 0.85)]
    out = []
    for id, degrees in (("up", 0), ("right", 90), ("down", 180), ("left", 270)):
        out.append(key(id, "", poly(turned(up, cx, cy, degrees), [r, r, r, r * 0.6, r])))
    return out


def cross(cx, cy, reach, width, r=2.2):
    """One cross: its arms the four directions, an outline round the whole."""
    w = width / 2
    out = []
    arm = [(cx - w, cy - reach), (cx + w, cy - reach), (cx + w, cy - w), (cx - w, cy - w)]
    for id, degrees in (("up", 0), ("right", 90), ("down", 180), ("left", 270)):
        out.append(key(id, "", poly(turned(arm, cx, cy, degrees), [r, r, 0, 0]), edge=False))
    quarter = [(cx - w, cy - reach), (cx + w, cy - reach), (cx + w, cy - w)]
    points = []
    for degrees in (0, 90, 180, 270):
        points += turned(quarter, cx, cy, degrees)
    out.append(line(poly(points, [r, r, r * 0.5] * 4), strong=True))
    out.append(line(dot(cx, cy, width * 0.24)))
    return out


def mirror_x(x):
    return 2 * CX - x


def superellipse(a, b, n, cy=130.0, count=96):
    """An oval between an ellipse and a rounded box, as the Vita is: its points, clockwise from the right."""
    out = []
    for i in range(count):
        t = 2 * math.pi * i / count
        c, s = math.cos(t), math.sin(t)
        out.append((CX + a * math.copysign(abs(c) ** (2 / n), c), cy + b * math.copysign(abs(s) ** (2 / n), s)))
    return out


def cubic_points(p0, c1, c2, p3, count=40):
    out = []
    for i in range(count + 1):
        t = i / count
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * c1[0] + 3 * u * t * t * c2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * c1[1] + 3 * u * t * t * c2[1] + t ** 3 * p3[1]))
    return out


def band(along, thick, inside=1.5):
    """A shoulder button that wraps a corner of the body, as the PSP's and the Vita's clear ones
    do: the strip `thick` wide outside the body's edge `along` (points of it, in order), its
    ends rounded. It goes a little under the body, which is painted over it."""
    def pushed(by):
        out = []
        for i, (x, y) in enumerate(along):
            ax, ay = along[max(i - 1, 0)]
            bx, by_ = along[min(i + 1, len(along) - 1)]
            tx, ty = unit(bx - ax, by_ - ay)
            nx, ny = -ty, tx
            if (x - CX) * nx + (y - 130) * ny < 0:
                nx, ny = -nx, -ny
            out.append((x + nx * by, y + ny * by))
        return out
    outer, inner = pushed(thick), pushed(-inside)
    path = spline(outer, closed=False)
    r = (thick + inside) / 2
    def cap(a, b):
        # Half a circle from a round to b, bulging along the strip's own direction.
        mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
        ux, uy = unit(a[0] - mx, a[1] - my)
        vx, vy = -uy, ux
        return (mx + vx * r, my + vy * r), (ux, uy), (vx, vy)
    k = 0.5522847498 * r
    for a, b, rest in ((outer[-1], inner[-1], list(reversed(inner))), (inner[0], outer[0], None)):
        tip, (ux, uy), (vx, vy) = cap(a, b)
        # The bulge must leave the strip: away from its neighbouring points.
        ref = along[-2] if rest is not None else along[1]
        end = along[-1] if rest is not None else along[0]
        if (tip[0] - end[0]) * (ref[0] - end[0]) + (tip[1] - end[1]) * (ref[1] - end[1]) > 0:
            vx, vy = -vx, -vy
            tip = ((a[0] + b[0]) / 2 + vx * r, (a[1] + b[1]) / 2 + vy * r)
        path.C(a[0] + vx * k, a[1] + vy * k, tip[0] + ux * k, tip[1] + uy * k, tip[0], tip[1])
        path.C(tip[0] - ux * k, tip[1] - uy * k, b[0] + vx * k, b[1] + vy * k, b[0], b[1])
        if rest is not None:
            path.add(Path_tail(spline(rest, closed=False)))
    path.Z()
    return path


def Path_tail(path):
    """A path without its first move, to go on from another."""
    out = Path()
    out.parts = path.parts[1:]
    return out


def flipped(points):
    return [(mirror_x(x), y) for x, y in points]


# ---- the pads ----

def dualshock2():
    half = [(0.0, 63.6), (14.7, 63.6), (29.9, 63.6), (44.0, 63.3), (57.1, 61.3), (68.7, 55.0), (78.2, 46.4), (89.5, 38.6),
            (101.9, 35.0), (115.1, 34.9), (128.5, 38.3), (138.7, 45.3), (146.4, 55.2), (154.5, 66.1), (163.4, 75.9),
            (171.2, 86.6), (176.7, 99.4), (178.9, 112.6), (179.5, 126.5), (179.6, 141.5), (179.6, 155.6), (179.6, 170.7),
            (179.6, 184.9), (179.3, 199.0), (177.4, 213.4), (172.6, 225.4), (164.3, 235.3), (152.7, 243.0), (140.5, 247.5),
            (127.6, 248.4), (116.5, 246.6), (106.5, 239.5), (99.5, 227.0), (95.6, 211.0), (94.0, 197.0), (93.3, 187.2), (93.3, 187.2), (86.8, 194.3), (78.6, 199.5), (69.4, 202.4), (59.8, 202.9), (50.3, 200.8), (41.8, 196.3), (35.5, 190.8), (32.0, 186.2), (32.0, 186.2), (20.0, 185.79999999999998), (0.0, 185.6)]
    # The underside: the grips' inner sides come up to the two round stick housings, a crease at each end of them.
    draw = [
        tab("l2", "L2", 60, 8, 50, 30, 13), tab("r2", "R2", 290, 8, 50, 30, 13),
        tab("l1", "L1", 52, 21, 66, 30, 13), tab("r1", "R1", 282, 21, 66, 30, 13),
        body(spline(both(half))),
        line(dot(80, 108, 45)), line(dot(320, 108, 45)),
    ]
    draw += arrows(80, 108, 28, 5.5, 19)
    draw += sony(320, 108, 27, 12.5)
    draw += [
        key("select", "SELECT", rrect(153, 100, 21, 11, 2.5), quiet=True),
        key("start", "START", poly([(228, 99.5), (248, 105.5), (228, 111.5)], 2.5), quiet=True),
        line(rrect(192, 127, 16, 9, 2)), line(rrect(196, 141, 8, 3, 1.5)),
    ]
    draw += stick("l3", 137, 166, 31, 23.5) + stick("r3", 263, 166, 31, 23.5)
    return {"name": "DualShock 2", "draw": draw}


def psp():
    outline = (Path().M(78, 52).L(322, 52).C(357, 52, 382, 86, 382, 130).C(382, 174, 357, 208, 322, 208)
               .L(78, 208).C(43, 208, 18, 174, 18, 130).C(18, 86, 43, 52, 78, 52).Z())
    # The clear shoulder buttons wrap the top corners: from the top edge round onto the side.
    corner = cubic_points((78, 52), (43, 52), (18, 86), (18, 130))
    left_corner = [(104, 52), (95, 52), (86, 52)] + corner[:19]
    draw = [
        key("l1", "L", band(left_corner, 5.5), at=(27, 53), size=7.5),
        key("r1", "R", band(flipped(left_corner), 5.5), at=(373, 53), size=7.5),
        body(outline),
        line(rrect(92, 68, 216, 122, 3), fill=True),
        line(dot(55, 112, 30)), line(dot(345, 112, 30)),
    ]
    draw += arrows(55, 112, 23, 4.5, 15.5, 1.8)
    draw += sony(345, 112, 20.5, 9.6)
    draw += [line(dot(55, 170, 10.5), strong=True), line(dot(55, 170, 6))]
    for x, w in ((98, 22), (148, 15), (167, 15), (206, 15), (225, 15)):
        draw.append(line(rrect(x, 195.5, w, 8, 4)))
    draw += [key("select", "SELECT", rrect(256, 195.5, 22, 8, 4), quiet=True),
             key("start", "START", rrect(283, 195.5, 22, 8, 4), quiet=True)]
    return {"name": "PSP", "draw": draw}


def vita():
    # A long oval: straight along the top and bottom, its ends all but half circles.
    outline = (Path().M(104, 46).L(296, 46).C(346, 46, 384, 84, 384, 130).C(384, 176, 346, 214, 296, 214)
               .L(104, 214).C(54, 214, 16, 176, 16, 130).C(16, 84, 54, 46, 104, 46).Z())
    # The clear shoulder buttons wrap the top corners.
    corner = cubic_points((104, 46), (54, 46), (16, 84), (16, 130))
    left_corner = [(122, 46), (113, 46)] + corner[:20]
    draw = [
        key("l1", "L", band(left_corner, 5.5), at=(36, 44), size=7.5),
        key("r1", "R", band(flipped(left_corner), 5.5), at=(364, 44), size=7.5),
        body(outline),
        line(rrect(89, 67, 222, 126, 4), fill=True),
    ]
    draw += arrows(53, 104, 18, 3.5, 12.5, 1.6)
    draw += sony(347, 104, 16.5, 7.8)
    draw += stick("", 58, 152, 15.5, 11.5) + stick("", 342, 152, 15.5, 11.5)
    draw += [line(rrect(58, 176, 13, 6, 3)),
             key("select", "SELECT", rrect(312, 176, 13, 6, 3), quiet=True),
             key("start", "START", rrect(329, 176, 13, 6, 3), quiet=True)]
    return {"name": "PS Vita", "draw": draw}


def n3ds():
    draw = [
        tab("l1", "L", 40, 23, 58, 24, 11), tab("r1", "R", 302, 23, 58, 24, 11),
        part(rrect(32, 34, 336, 44, [12, 12, 4, 4])),
        body(rrect(32, 62, 336, 188, [8, 8, 20, 20])),
        part(rrect(70, 55, 260, 13, 6.5)),
        line(rrect(113, 76, 174, 144, 6)),
        line(rrect(124, 84, 152, 110, 2.5), fill=True),
    ]
    draw += [line(dot(78, 114, 21)), line(dot(78, 114, 15.5), strong=True), line(dot(78, 114, 9))]
    draw += cross(78, 174, 18, 12.5)
    draw += face(322, 126, 18.5, 9.2, "X", "A", "B", "Y")
    draw += [key("select", "SELECT", rrect(124, 201, 46, 12, 3), at=(147, 207), size=5.6),
             line(rrect(177, 201, 46, 12, 3)),
             key("start", "START", rrect(230, 201, 46, 12, 3), at=(253, 207), size=5.6),
             line(dot(322, 200, 5))]
    return {"name": "Nintendo 3DS", "draw": draw}


def dreamcast():
    half = [(0.0, 19.2), (11.2, 20.1), (22.5, 22.6), (32.7, 25.8), (42.5, 29.8), (52.5, 35.3), (61.5, 41.5), (70.5, 49.2),
            (79.0, 56.1), (88.4, 60.9), (98.9, 65.1), (108.0, 70.1), (114.9, 77.6), (119.8, 87.6), (122.0, 98.1),
            (122.7, 109.2), (122.6, 121.2), (121.8, 132.3), (120.5, 143.3), (118.7, 154.9), (116.8, 165.6), (114.6, 177.0),
            (112.3, 187.6), (109.9, 198.1), (106.8, 209.1), (103.2, 219.1), (98.8, 228.8), (93.0, 238.7), (86.0, 248.0),
            (77.0, 254.6), (68.6, 252.6), (63.6, 244.0), (61.8, 232.0), (60.6, 221.5), (55.0, 214.2), (44.0, 211.6),
            (33.3, 209.2), (22.7, 207.0), (11.2, 205.1), (0.0, 204.3)]
    housing = (Path().M(156, 31).L(156, 98).C(156, 121, 175, 133, 200, 133).C(225, 133, 244, 121, 244, 98).L(244, 31))
    draw = [
        key("l2", "L", rrect(100, 28, 50, 34, 11), at=(117, 39), size=6.5),
        key("r2", "R", rrect(250, 28, 50, 34, 11), at=(283, 39), size=6.5),
        body(spline(both(half))),
        line(housing),
        line(rrect(171, 58, 58, 44, 5), fill=True),
    ]
    draw += [line(dot(103, 93, 21)), line(dot(103, 93, 15), strong=True), line(dot(103, 93, 9))]
    draw += cross(123, 141, 19, 13)
    draw += face(282, 97, 21.5, 9.4, "Y", "B", "A", "X")
    draw += [key("start", "START", poly([(200, 158), (212.5, 178), (187.5, 178)], 3), quiet=True)]
    return {"name": "Dreamcast controller", "draw": draw}


def pro():
    half = [(0, 40), (40, 39.4), (80, 39.4), (112, 42.5), (136, 52), (151, 70), (159, 96), (162.5, 126), (162, 158),
            (157.5, 190), (149, 218), (137, 239), (121, 249.5), (104.5, 245.5), (93, 229), (84.5, 207), (72, 190.5),
            (52, 183.6), (26, 182), (0, 181.6)]
    draw = [
        tab("l2", "ZL", 84, 13, 54, 30, 13), tab("r2", "ZR", 262, 13, 54, 30, 13),
        tab("l1", "L", 74, 26, 74, 30, 13.5), tab("r1", "R", 252, 26, 74, 30, 13.5),
        body(spline(both(half))),
    ]
    draw += stick("l3", 100, 92, 25, 18.5)
    draw += cross(146, 143, 19.5, 13.5)
    draw += face(300, 92, 21.5, 10, "X", "A", "B", "Y")
    draw += stick("r3", 254, 143, 25, 18.5)
    minus = Path().M(152.5, 64).L(159.5, 64)
    plus = Path().M(240.5, 64).L(247.5, 64).M(244, 60.5).L(244, 67.5)
    draw += [key("select", "−", dot(156, 64, 6.5), glyph=minus), key("start", "+", dot(244, 64, 6.5), glyph=plus),
             line(rrect(170.5, 83, 11, 11, 2.5)), line(dot(224, 88.5, 6))]
    return {"name": "Switch Pro Controller", "draw": draw}


def steam():
    half = [(0, 40), (42, 39.4), (84, 39.6), (118, 43), (142, 53), (157, 71), (165, 97), (168.5, 128), (168, 160),
            (164, 192), (156, 220), (144, 241), (128, 251), (111, 247.5), (99, 233), (90, 219), (74, 213), (48, 212.4),
            (22, 212.2), (0, 212.2)]
    draw = [
        tab("l2", "LT", 78, 13, 54, 30, 13), tab("r2", "RT", 268, 13, 54, 30, 13),
        tab("l1", "LB", 68, 26, 74, 30, 13.5), tab("r1", "RB", 258, 26, 74, 30, 13.5),
        body(spline(both(half))),
    ]
    draw += cross(88, 98, 19, 13)
    draw += face(312, 98, 21, 9.8, "Y", "B", "A", "X")
    draw += stick("l3", 146, 108, 23, 17) + stick("r3", 254, 108, 23, 17)
    for cx, degrees in ((150, -8), (250, 8)):
        square = [(cx - 22, 150), (cx + 22, 150), (cx + 22, 194), (cx - 22, 194)]
        draw.append(line(poly(turned(square, cx, 172, degrees), 8), fill=True))
    draw += [key("select", "View", rrect(169, 61, 14, 8, 4), quiet=True),
             key("start", "Menu", rrect(217, 61, 14, 8, 4), quiet=True),
             line(dot(200, 84, 7)), line(rrect(192, 126, 16, 7, 3.5))]
    return {"name": "Steam Controller", "draw": draw}


def pads():
    return {
        "steam": steam(), "dualshock2": dualshock2(), "dualshock": {"name": "DualShock", "same_as": "dualshock2"},
        "psp": psp(), "vita": vita(), "dreamcast": dreamcast(), "3ds": n3ds(), "pro": pro(),
    }


# ---- a page to look at ----

def page(all_pads, grounds):
    def item_svg(item, chosen):
        cls = item["k"]
        if item.get("strong"):
            cls += " strong"
        if item.get("fill"):
            cls += " sunk"
        if item.get("edge") is False:
            cls += " edgeless"
        on = item.get("id") in chosen
        if on:
            cls += " on"
        shape = (f'<circle cx="{item["cx"]}" cy="{item["cy"]}" r="{item["r"]}" class="{cls}"/>' if "cx" in item
                 else f'<path d="{item["d"]}" class="{cls}"/>')
        if item.get("glyph"):
            shape += f'<path d="{item["glyph"]}" class="glyph{" on" if on else ""}"/>'
        if item["k"] == "key" and not item.get("quiet"):
            shape += (f'<text x="{item["lx"]}" y="{item["ly"]}" font-size="{item["ls"]}" class="label{" on" if on else ""}">'
                      f'{item["label"]}</text>')
        return shape

    out = ["<!doctype html><meta charset=utf-8><style>",
           "body{margin:0;font-family:'IBM Plex Mono',monospace}",
           ".sheet{display:grid;grid-template-columns:repeat(2,820px);gap:0;padding:20px}",
           ".cell{padding:16px 10px 6px}.cell p{margin:6px 0 0;font-size:13px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted);text-align:center}",
           "svg{width:800px;height:520px;overflow:visible}",
           ".body{fill:var(--raised);stroke:var(--ink);stroke-width:1.5;stroke-linejoin:round}",
           ".part{fill:var(--raised);stroke:var(--ink);stroke-width:1.2;stroke-linejoin:round}",
           ".line{fill:none;stroke:var(--ink);stroke-opacity:.38;stroke-width:1;stroke-linejoin:round;stroke-linecap:round}",
           ".line.strong{stroke-opacity:.9;stroke-width:1.15}.line.sunk{fill:var(--ground)}",
           ".key{fill:var(--ground);stroke:var(--ink);stroke-width:1.15;stroke-linejoin:round}.key.edgeless{stroke:none;fill:none}",
           ".key.on{fill:var(--accent);stroke:var(--accent)}.key.edgeless.on{fill:var(--accent)}",
           ".glyph{fill:none;stroke:var(--ink);stroke-width:1.1;stroke-linejoin:round;stroke-linecap:round}.glyph.on{stroke:var(--ground)}",
           ".label{fill:var(--ink);text-anchor:middle;dominant-baseline:central;font-weight:500}.label.on{fill:var(--ground)}",
           "</style>"]
    for ground, raised, ink, muted, accent in grounds:
        out.append(f'<div class="sheet" style="--ground:{ground};--raised:{raised};--ink:{ink};--muted:{muted};--accent:{accent};background:{ground}">')
        for name, pad in all_pads.items():
            if "same_as" in pad:
                continue
            chosen = {"south"}
            items = "".join(item_svg(item, chosen) for item in pad["draw"])
            out.append(f'<div class="cell"><svg viewBox="0 0 400 260">{items}</svg><p>{pad["name"]}</p></div>')
        out.append("</div>")
    return "\n".join(out)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    file = os.path.join(here, "controllers.json")
    data = json.load(open(file, encoding="utf-8"))
    data["pads"] = pads()
    text = json.dumps(data, ensure_ascii=False, indent=1)
    open(file, "w", encoding="utf-8").write(text + "\n")
    if len(sys.argv) > 1:
        grounds = [("#0a0a0a", "#161616", "#f2f2f0", "#8c8c88", "#f2f2f0"), ("#101a3a", "#18244c", "#ece5d3", "#8a93b5", "#e0452c")]
        open(sys.argv[1], "w", encoding="utf-8").write(page(data["pads"], grounds))


if __name__ == "__main__":
    main()
