// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Furoshiki 3's page (docs/furoshiki-3.md; the maintainer, 2026-11-10: "shelf: spines", "type:
// machine", "default theme: mono, but all themes here are good", "go ahead with the complete
// redesign now, it can be furoshiki 3"). Every title of the library stands as a spine with its
// name set down it; the one in hand is open: its cover as a plate, its name large, what was read
// of it, and the one filled thing on the screen, the key that plays it. Type and hairlines only:
// no boxes with shadows, no icons where a word will do. A theme is seven values and three faces
// (shelf/*.json). Part of home_window.cpp (it uses that file's own helpers), inside namespace Home.

struct HomeWindow::ShelfLook {
    QString key, name;
    QColor ground, raised, ink, muted, accent, rule;
    QString display, text, mono; ///< Font files in fonts/
    bool beam{};                 ///< Nocturne: horizontal electric beam on active selection
};

namespace {

constexpr double ShelfLeft = 40, ShelfRight = SheetW - 40, ShelfTop = 86, ShelfBottom = 626;
constexpr double SpineOpen = 560, SpineGap = 6, SpineMin = 34, SpineMax = 64;
constexpr double ShelfGlide = 0.075; // seconds: the open spine is most of the way there in a tenth
constexpr double PlateShare = 0.44;  // of an open spine: its cover
constexpr double SpinePlate = 38;    // over a real spine's picture: its number on the ground
constexpr double FloorColumn = 150;  // the shelves' floors, at the right of the shelf
constexpr double FloorPitch = 38;
constexpr double FloorFirst = FloorPitch + 14; // the shelves' floors, under the settings' one

struct ShelfFace {
    QString family;
    int weight{400};
};

ShelfFace ShelfFaceOf(const QString& file) {
    static QHash<QString, ShelfFace> faces;
    auto found = faces.find(file);
    if (found == faces.end()) {
        ShelfFace face{QGuiApplication::font().family(), 400};
        QFile data(QStringLiteral(":/fonts/") + file);
        if (data.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = data.readAll();
            const QStringList families = QFontDatabase::applicationFontFamilies(QFontDatabase::addApplicationFontFromData(bytes));
            const QRawFont raw(bytes, 12);
            if (!families.isEmpty()) {
                face.family = families.first();
            }
            if (raw.isValid()) {
                face.weight = raw.weight();
            }
        }
        found = faces.insert(file, face);
    }
    return *found;
}

/// A face at a size on the sheet, its letters set apart by a share of that size.
QFont ShelfFont(const QString& file, double px, double spacing = 0) {
    const ShelfFace face = ShelfFaceOf(file);
    QFont font(face.family);
    if (file.startsWith(QStringLiteral("ZenKakuGothicNew"))) {
        const ShelfFace latin = ShelfFaceOf(QStringLiteral("SpaceGrotesk-Medium.ttf"));
        if (!latin.family.isEmpty()) {
            font.setFamilies({latin.family, face.family});
        }
    }
    if (file.startsWith(QStringLiteral("ChakraPetch"))) {
        font.setFeature(QFont::Tag("tnum"), 1);
    }
    font.setWeight(static_cast<QFont::Weight>(face.weight));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(px))));
    font.setHintingPreference(QFont::PreferNoHinting); // it is drawn at the window's scale
    font.setStyleStrategy(QFont::NoSubpixelAntialias); // turned on its side, a screen's colour fringes show
    if (spacing != 0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, px * spacing);
    }
    return font;
}

void PaintCornerTicks(QPainter& p, const QRectF& box, const QColor& colour, double arm = 7.0) {
    const QRectF r = box.adjusted(-3.5, -3.5, 3.5, 3.5);
    p.save();
    p.setPen(QPen(colour, 1.0));
    // Top-left, top-right, bottom-left, bottom-right registration brackets.
    p.drawLine(QPointF(r.left(), r.top()), QPointF(r.left() + arm, r.top()));
    p.drawLine(QPointF(r.left(), r.top()), QPointF(r.left(), r.top() + arm));
    p.drawLine(QPointF(r.right(), r.top()), QPointF(r.right() - arm, r.top()));
    p.drawLine(QPointF(r.right(), r.top()), QPointF(r.right(), r.top() + arm));
    p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.left() + arm, r.bottom()));
    p.drawLine(QPointF(r.left(), r.bottom()), QPointF(r.left(), r.bottom() - arm));
    p.drawLine(QPointF(r.right(), r.bottom()), QPointF(r.right() - arm, r.bottom()));
    p.drawLine(QPointF(r.right(), r.bottom()), QPointF(r.right(), r.bottom() - arm));
    p.restore();
}

bool HasJapanese(const QString& text) {
    for (const QChar c : text) {
        if (c.unicode() >= 0x2E80) {
            return true;
        }
    }
    return false;
}

/// A title as the shelf says it: without what stands in brackets after it (a console's name, the
/// name in another language), which the spine's foot and the title itself already say.
QString ShelfName(const QString& title) {
    QString name = title.trimmed();
    // Also what a dump's file name adds after the title: "(Japan)", "(Rev 1)", "(1)", "[!]".
    static const QRegularExpression tail(QStringLiteral("\\s*(?:[（(][^（()）]*[)）]|\\[[^\\[\\]]*\\])\\s*$"));
    for (int i = 0; i < 6; ++i) {
        const QString shorter = QString(name).remove(tail).trimmed();
        if (shorter.isEmpty() || shorter == name) {
            break;
        }
        name = shorter;
    }
    // A file's name cannot hold a colon: "Title_ Subtitle" is "Title: Subtitle".
    name.replace(QStringLiteral("_ "), QStringLiteral(": "));
    return name;
}

/// Text in capitals as the pages set it, but for what must keep its own case: an address
/// ("ws://localhost:6677", "127.0.0.1:8794"), a path, a time ("1h 05m").
QString Capitals(const QString& text) {
    static const QRegularExpression own(QStringLiteral("://|^[\\w.-]+:\\d+$|[/\\\\]|^\\d+[hms]$"));
    QStringList words = text.split(QLatin1Char(' '));
    for (QString& word : words) {
        if (!own.match(word).hasMatch()) {
            word = word.toUpper();
        }
    }
    return words.join(QLatin1Char(' '));
}

/// A game's title in Latin letters, from its title's record (../titles/<key>.json, "english"; built
/// in as :/titles): the display face has no Japanese, and the maintainer would rather read every
/// spine in that one face ("if the japanese titles wont show in the same font, just show english
/// titles", 2026-11-10). Empty for a game no record names.
struct TitleRecord {
    QString key, english, guide, tracker_title;
    qint64 jiten{};
    QString vndb;
};

TitleRecord RecordOf(const QString& id) {
    static QHash<QString, TitleRecord> records;
    static bool read = false;
    if (!read) {
        read = true;
        const QDir folder(QStringLiteral(":/titles"));
        for (const QString& file : folder.entryList({QStringLiteral("*.json")}, QDir::Files)) {
            QFile data(folder.filePath(file));
            if (!data.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonObject record = QJsonDocument::fromJson(data.readAll()).object();
            const TitleRecord found{QFileInfo(file).completeBaseName(), record.value(QStringLiteral("english")).toString(),
                                    record.value(QStringLiteral("guide")).toString(), record.value(QStringLiteral("tracker_title")).toString(),
                                    record.value(QStringLiteral("jiten")).toInteger(),
                                    record.value(QStringLiteral("vndb")).isArray() ? record.value(QStringLiteral("vndb")).toArray().first().toString()
                                                                                   : record.value(QStringLiteral("vndb")).toString()};
            for (const QJsonValue game : record.value(QStringLiteral("ids")).toArray()) {
                records.insert(game.toString().toUpper(), found);
            }
            // Read from lines (another program's, the clipboard), the title's ID is made from its key.
            records.insert(Lines::GameId(QString(), found.key), found);
        }
    }
    return records.value(id.toUpper());
}

/// The key of the title record whose name (its own, or its English one) is exactly `name`; empty
/// when none is, or more than one.
QString RecordNamed(const QString& name) {
    if (name.isEmpty()) {
        return {}; // nothing typed is no title's name
    }
    QStringList keys;
    const QDir folder(QStringLiteral(":/titles"));
    for (const QString& file : folder.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile data(folder.filePath(file));
        if (!data.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject record = QJsonDocument::fromJson(data.readAll()).object();
        if (record.value(QStringLiteral("title")).toString() == name || record.value(QStringLiteral("english")).toString() == name) {
            keys.append(QFileInfo(file).completeBaseName());
        }
    }
    return keys.size() == 1 ? keys.first() : QString();
}

/// A title's people for its stage: the first few of its names (titles/names/<key>.json), the
/// leads first, none that is a spoiler; and how many there are besides.
struct CastMember {
    QString name, romaji, role;
};

QList<CastMember> CastOf(const QString& key, int most, int* more) {
    static QHash<QString, QList<CastMember>> casts;
    if (!casts.contains(key)) {
        QList<CastMember> cast;
        QFile data(QStringLiteral(":/titles/names/%1.json").arg(key));
        if (!key.isEmpty() && data.open(QIODevice::ReadOnly)) {
            const auto rank = [](const QString& role) {
                return role == QStringLiteral("main") ? 0 : role == QStringLiteral("primary") ? 1 : role == QStringLiteral("side") ? 2 : 3;
            };
            for (const QJsonValue value : QJsonDocument::fromJson(data.readAll()).object().value(QStringLiteral("entries")).toArray()) {
                const QJsonObject entry = value.toObject();
                if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("person") && entry.value(QStringLiteral("spoiler")).toInt() == 0) {
                    cast.append({entry.value(QStringLiteral("name")).toString(), entry.value(QStringLiteral("romaji")).toString(),
                                 entry.value(QStringLiteral("role")).toString()});
                }
            }
            std::stable_sort(cast.begin(), cast.end(), [&](const CastMember& a, const CastMember& b) { return rank(a.role) < rank(b.role); });
        }
        casts.insert(key, cast);
    }
    const QList<CastMember>& cast = casts[key];
    *more = std::max<int>(0, cast.size() - most);
    return cast.mid(0, most);
}

/// A title's box spine, where the build has one (:/spines/<key>.jpg, tools/titles.py spines).
/// The console a game's ID is of, as tools/titles.py names it (spine_platform there, the same
/// rules): its box art is that console's only (the maintainer, 2026-11-10: "important the console
/// spine and cover is not wrong"; a record of several consoles has a picture for each).
QString ArtPlatform(const QString& game_id) {
    const QString i = game_id.toUpper();
    static const QRegularExpression psp(QStringLiteral("^(?:ULJ|NPJ|UCJ|ULA)"));
    static const QRegularExpression vita(QStringLiteral("^PCS"));
    static const QRegularExpression n3ds(QStringLiteral("^0004[0-9A-F]{12}$"));
    static const QRegularExpression swi(QStringLiteral("^[0-9A-F]{16}$"));
    static const QRegularExpression ps2(QStringLiteral("^(?:SLPS-?2|SLPM-?[5-7]|SCPS-?[15-7]|SLPS-?7)"));
    static const QRegularExpression ps1(QStringLiteral("^(?:SLPS-?[0189]|SLPM-?8|SCPS-?[14]0)"));
    static const QRegularExpression dc(QStringLiteral("^(?:T-?\\d|HDR-|MK-|\\d{7}$)"));
    static const QRegularExpression nds(QStringLiteral("^[A-Z0-9]{4}$"));
    if (psp.match(i).hasMatch()) return QStringLiteral("psp");
    if (vita.match(i).hasMatch()) return QStringLiteral("psv");
    if (n3ds.match(i).hasMatch()) return QStringLiteral("n3d");
    if (swi.match(i).hasMatch()) return QStringLiteral("swi");
    if (ps2.match(i).hasMatch()) return QStringLiteral("ps2");
    if (ps1.match(i).hasMatch()) return QStringLiteral("ps1");
    if (dc.match(i).hasMatch()) return QStringLiteral("drc");
    if (nds.match(i).hasMatch()) return QStringLiteral("nds");
    return {};
}

QString EnglishTitle(const QString& id) {
    return RecordOf(id).english;
}

/// What the shelf calls a game: its English title; else what its own title says in brackets in
/// Latin letters; else its title without the brackets (Japanese, in the text face).
QString ShelfTitleOf(const QString& id, const QString& title) {
    const QString english = EnglishTitle(id);
    if (!english.isEmpty()) {
        return english;
    }
    static const QRegularExpression tail(QStringLiteral("[（(]([^（()）]+)[)）]\\s*$"));
    const QRegularExpressionMatch found = tail.match(title);
    if (HasJapanese(title) && found.hasMatch() && !HasJapanese(found.captured(1)) && found.captured(1).size() > 6) {
        return found.captured(1).trimmed();
    }
    return ShelfName(title);
}

/// Latin letters and figures, which lie on their side as a run in a line set downwards.
bool IsLatin(QChar c) {
    return c.unicode() > 0x20 && c.unicode() < 0x250;
}

/// Marks that lie on their side in a line set downwards.
bool LiesDown(QChar c) {
    static const QString marks = QStringLiteral("ー〜～−-—–…‥()（）「」『』【】〈〉《》[]:：;；=＝~");
    return marks.contains(c);
}

QColor Faded(QColor colour, double share) {
    colour.setAlphaF(colour.alphaF() * std::clamp(share, 0.0, 1.0));
    return colour;
}

/// Between two colours: 0 the first, 1 the second.
QColor Mix(const QColor& from, const QColor& to, double share) {
    const double t = std::clamp(share, 0.0, 1.0);
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * t, from.greenF() + (to.greenF() - from.greenF()) * t,
                            from.blueF() + (to.blueF() - from.blueF()) * t, from.alphaF() + (to.alphaF() - from.alphaF()) * t);
}

double Advance(const QFont& font, const QString& text) {
    return QFontMetricsF(font).horizontalAdvance(text);
}

/// Text on a baseline; `align` 0 from x, 1 ending at x, 2 centred on x. Returns its width.
double Put(QPainter& p, const QFont& font, double x, double baseline, const QString& text, const QColor& colour, int align = 0) {
    const double width = Advance(font, text);
    p.setFont(font);
    p.setPen(colour);
    p.drawText(QPointF(align == 1 ? x - width : align == 2 ? x - width / 2 : x, baseline), text);
    return width;
}

/// Active text with a humming Strong Halo glow around its edges.
double PutHalo(QPainter& p, const QFont& font, double x, double baseline, const QString& text, const QColor& ink,
               const QColor& glow, double phase, bool light_ground, int align = 0) {
    const double width = Advance(font, text);
    const double left = align == 1 ? x - width : align == 2 ? x - width / 2 : x;
    const double hum = 0.82 + 0.18 * std::sin(phase * 2.6);
    p.setFont(font);
    static constexpr double Ring[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7, 0.7}, {-0.7, 0.7}, {0.7, -0.7}, {-0.7, -0.7}};
    const double outer_a = (light_ground ? 0.08 : 0.14) * hum;
    const double inner_a = (light_ground ? 0.14 : 0.26) * hum;
    p.setPen(Faded(glow, outer_a));
    for (const auto& off : Ring) {
        p.drawText(QPointF(left + off[0] * 4.2, baseline + off[1] * 4.2), text);
    }
    p.setPen(Faded(glow, inner_a));
    for (const auto& off : Ring) {
        p.drawText(QPointF(left + off[0] * 1.9, baseline + off[1] * 1.9), text);
    }
    p.setPen(ink);
    p.drawText(QPointF(left, baseline), text);
    return width;
}

/// A dot with PutHalo's two rings of glow round it.
void HaloDot(QPainter& p, const QPointF& at, double radius, const QColor& colour, double phase, bool light_ground) {
    const double hum = 0.82 + 0.18 * std::sin(phase * 2.6);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(Faded(colour, (light_ground ? 0.08 : 0.14) * hum));
    p.drawEllipse(at, radius + 4.2, radius + 4.2);
    p.setBrush(Faded(colour, (light_ground ? 0.14 : 0.26) * hum));
    p.drawEllipse(at, radius + 1.9, radius + 1.9);
    p.setBrush(colour);
    p.drawEllipse(at, radius, radius);
    p.restore();
}

/// A level hairline with the same glow.
void HaloRule(QPainter& p, double left, double right, double y, const QColor& colour, double phase, bool light_ground) {
    const double hum = 0.82 + 0.18 * std::sin(phase * 2.6);
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(Faded(colour, (light_ground ? 0.08 : 0.14) * hum));
    p.drawRoundedRect(QRectF(left - 4.2, y - 4.2, right - left + 8.4, 9.4), 4.7, 4.7);
    p.setBrush(Faded(colour, (light_ground ? 0.14 : 0.26) * hum));
    p.drawRoundedRect(QRectF(left - 1.9, y - 1.9, right - left + 3.8, 4.8), 2.4, 2.4);
    p.restore();
    p.fillRect(QRectF(left, y, right - left, 1), colour);
}

/// Nocturne's horizontal luminous electric beam slicing through the active selection.
void PaintBeam(QPainter& p, double left, double right, double cy, const QColor& ink, const QColor& glow, double phase) {
    const double hum = 0.78 + 0.22 * std::sin(phase * 2.6);
    QLinearGradient core(left, cy, right, cy);
    core.setColorAt(0.0, Faded(glow, 0.0));
    core.setColorAt(0.10, Faded(glow, 0.55 * hum));
    core.setColorAt(0.36, Faded(ink, 0.94 * hum));
    core.setColorAt(0.72, Faded(glow, 0.80 * hum));
    core.setColorAt(1.0, Faded(glow, 0.0));
    QLinearGradient halo(left, cy, right, cy);
    halo.setColorAt(0.0, Faded(glow, 0.0));
    halo.setColorAt(0.18, Faded(glow, 0.22 * hum));
    halo.setColorAt(0.68, Faded(glow, 0.16 * hum));
    halo.setColorAt(1.0, Faded(glow, 0.0));
    p.fillRect(QRectF(left, cy - 6.0, right - left, 12.0), halo);
    p.fillRect(QRectF(left, cy - 0.85, right - left, 1.7), core);
}

inline double DotHash(int ix, int iy) {
    const double n = std::sin(ix * 127.1 + iy * 311.7) * 43758.5453123;
    return n - std::floor(n);
}

inline double DotNoise(double x, double y) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const double ux = fx * fx * (3.0 - 2.0 * fx);
    const double uy = fy * fy * (3.0 - 2.0 * fy);
    const double a = DotHash(ix, iy), b = DotHash(ix + 1, iy);
    const double c = DotHash(ix, iy + 1), d = DotHash(ix + 1, iy + 1);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

inline double DotFbm(double x, double y) {
    return DotNoise(x, y) * 0.58 + DotNoise(x * 2.13 + 7.4, y * 2.13 - 3.1) * 0.30 +
           DotNoise(x * 4.31 - 2.9, y * 4.31 + 11.2) * 0.12;
}

QString ShortCount(qint64 n) {
    if (n >= 1000000) {
        return QStringLiteral("%1M").arg(n / 1000000.0, 0, 'f', n >= 10000000 ? 1 : 2);
    }
    if (n >= 1000) {
        return n % 1000 == 0 ? QStringLiteral("%1K").arg(n / 1000) : QStringLiteral("%1K").arg(n / 1000.0, 0, 'f', 1);
    }
    return QString::number(n);
}

QString ShortHours(qint64 ms) {
    const double h = ms / 3600000.0;
    if (h >= 100.0) {
        return QStringLiteral("%1H").arg(std::lround(h));
    }
    return QStringLiteral("%1H").arg(h, 0, 'f', 1);
}

/// How a console's Japanese boxes set their spines, for the spines made here where no picture of
/// the real one is had (the maintainer, 2026-11-10: "generic spine art matching the look of that
/// console's spine art"). Their colours, bands and type only: nothing of a console maker's (no
/// logo, wordmark or mark of a rating board) is drawn, as everywhere in Furoshiki.
struct SpineStyle {
    double shape;          ///< Width to height, as the real case's spine
    QColor body, ink;      ///< The spine, and the title on it
    QColor band, band_ink; ///< The band at its head with the console's name in plain type
    double band_share;     ///< How much of the height the band takes (0: none)
    const char* name;      ///< Said in the band, or at the head where there is none
    QColor rule;           ///< Signature accent divider under the head band (alpha 0: none)
    QColor foot;           ///< A band at its foot (alpha 0: none), for the serial
    QColor foot_ink;
    bool serial;           ///< The game's ID at the foot, as the real ones print it
};

const SpineStyle* SpineStyleOf(const QString& platform) {
    static const QHash<QString, SpineStyle> styles = {
        // Iteration A (Authentic Studio Cases): matched across desktop and Android.
        {QStringLiteral("swi"), {0.066, QColor(0xc8, 0x1e, 0x32), Qt::white, QColor(0xa8, 0x14, 0x26), Qt::white, 0.13, "SWI", QColor(255, 255, 255, 50), QColor(0, 0, 0, 0), QColor(255, 255, 255, 204), false}},
        {QStringLiteral("ps2"), {0.075, QColor(0xec, 0xef, 0xf4), QColor(0x0f, 0x17, 0x2a), QColor(0x0f, 0x17, 0x2a), QColor(0xf8, 0xfa, 0xfc), 0.16, "PS2", QColor(0x25, 0x63, 0xeb), QColor(0x1e, 0x29, 0x3b), QColor(0x93, 0xc5, 0xfd), true}},
        {QStringLiteral("ps1"), {0.055, QColor(0x1c, 0x1d, 0x22), QColor(0xf4, 0xf4, 0xf5), QColor(0x0b, 0x0b, 0x0e), QColor(0xe4, 0xe4, 0xe7), 0.16, "PS1", QColor(0x9c, 0xa3, 0xaf), QColor(0x1c, 0x1d, 0x22), QColor(0xa1, 0xa1, 0xaa), true}},
        {QStringLiteral("psp"), {0.082, QColor(0xe8, 0xe5, 0xdc), QColor(0x17, 0x17, 0x17), QColor(0x14, 0x14, 0x18), QColor(0xf3, 0xf4, 0xf6), 0.13, "PSP", QColor(0xd4, 0xa0, 0x17), QColor(0x1e, 0x3a, 0x5f), QColor(0xe2, 0xe8, 0xf0), true}},
        {QStringLiteral("psv"), {0.080, QColor(0x1d, 0x4e, 0xd8), Qt::white, QColor(0x17, 0x25, 0x54), QColor(0x93, 0xc5, 0xfd), 0.13, "VITA", QColor(0x38, 0xbd, 0xf8), QColor(0x17, 0x25, 0x54), QColor(0x93, 0xc5, 0xfd), true}},
        {QStringLiteral("drc"), {0.062, QColor(0xeb, 0xe7, 0xde), QColor(0x1f, 0x29, 0x37), QColor(0xea, 0x58, 0x0c), Qt::white, 0.15, "DC", QColor(0, 0, 0, 0), QColor(0xeb, 0xe7, 0xde), QColor(0x6b, 0x72, 0x80), true}},
        {QStringLiteral("n3d"), {0.070, QColor(0xe2, 0xe6, 0xee), QColor(0x1e, 0x29, 0x3b), QColor(0xf1, 0xf5, 0xf9), QColor(0x0f, 0x17, 0x2a), 0.12, "3DS", QColor(0xdc, 0x26, 0x26), QColor(0xe2, 0xe6, 0xee), QColor(0x47, 0x55, 0x69), true}},
        {QStringLiteral("nds"), {0.085, QColor(0xe2, 0xe6, 0xee), QColor(0x1e, 0x29, 0x3b), QColor(0xcb, 0xd5, 0xe1), QColor(0x0f, 0x17, 0x2a), 0.13, "DS", QColor(0x64, 0x74, 0x8b), QColor(0xe2, 0xe6, 0xee), QColor(0x47, 0x55, 0x69), true}},
        {QStringLiteral("pc"),  {0.100, QColor(0x1e, 0x29, 0x48), QColor(0xec, 0xe5, 0xd3), QColor(0x10, 0x1a, 0x3a), QColor(0xec, 0xe5, 0xd3), 0.10, "PC", QColor(0xd9, 0x47, 0x2b), QColor(0, 0, 0, 0), QColor(0x8a, 0x93, 0xb5), false}},
        {QStringLiteral("ps3"), {0.075, QColor(0x14, 0x14, 0x16), Qt::white, QColor(0x05, 0x05, 0x06), Qt::white, 0.12, "PS3", QColor(0xd9, 0x47, 0x2b), QColor(0, 0, 0, 0), Qt::white, false}},
    };
    const auto found = styles.constFind(platform);
    return found == styles.constEnd() ? nullptr : &*found;
}

/// A spine in its console's look: the band, the title set down it as a Japanese book's is, the
/// serial at its foot. 1000 high, as the fetched ones are kept. (On 2026-11-23 it took the cover's
/// colours for an evening; the maintainer preferred these, "when they looked like the original
/// consoles' real spines".)
QImage MadeSpine(const SpineStyle& style, const QString& title, const QString& serial) {
    const int height = 1000;
    const int width = std::max(40, static_cast<int>(std::lround(height * style.shape)));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(style.body);
    QPainter p(&image);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    double top = 0;
    // The console's name, lying down along the band, in Michroma (matching Android Iteration A).
    const double band = height * style.band_share;
    if (band > 0) {
        p.fillRect(QRectF(0, 0, width, band), style.band);
        p.save();
        p.translate(width / 2.0, band / 2.0);
        p.rotate(90);
        double name_size = width * 0.38;
        while (name_size > width * 0.16 && Advance(ShelfFont(QStringLiteral("Michroma-Regular.ttf"), name_size), QLatin1String(style.name)) > band - 16) {
            name_size *= 0.93;
        }
        p.setFont(ShelfFont(QStringLiteral("Michroma-Regular.ttf"), name_size));
        p.setPen(style.band_ink);
        p.drawText(QRectF(-band / 2 + 6, -width / 2.0, band - 12, width), Qt::AlignCenter, QLatin1String(style.name));
        p.restore();
        top = band;
        if (style.rule.alpha() > 0) {
            const double rule_h = 8.0;
            p.fillRect(QRectF(0, band, width, rule_h), style.rule);
            top += rule_h;
        }
    } else {
        // No band: the console's name small at the head, in the title's colour.
        p.save();
        p.translate(width / 2.0, 22);
        p.rotate(90);
        p.setFont(ShelfFont(QStringLiteral("Michroma-Regular.ttf"), width * 0.28));
        p.setPen(style.ink);
        p.drawText(QPointF(0, width * 0.11), QLatin1String(style.name));
        p.restore();
        top = 22 + Advance(ShelfFont(QStringLiteral("Michroma-Regular.ttf"), width * 0.28), QLatin1String(style.name));
    }
    // The foot: the serial, as on the real ones.
    const double foot = style.serial ? height * 0.12 : height * 0.05;
    if (style.foot.alpha() > 0) {
        p.fillRect(QRectF(0, height - foot, width, foot), style.foot);
    }
    if (style.serial && !serial.isEmpty()) {
        // At its hyphen ("SLPM" over "66302"), else a long one in two (a Vita's "PCSG" over "00488",
        // a 3DS title ID eight figures over eight), as large as the widest line lets it be.
        const QString id = serial.toUpper();
        QStringList lines;
        int letters = 0;
        while (letters < id.size() && id[letters].isLetter()) {
            ++letters;
        }
        if (id.contains(QLatin1Char('-'))) {
            lines = id.split(QLatin1Char('-'));
        } else if (letters >= 2 && letters <= 5 && id.size() > letters) {
            lines = {id.left(letters), id.mid(letters)};
        } else if (id.size() > 8) {
            lines = {id.left(id.size() / 2), id.mid(id.size() / 2)};
        } else {
            lines = {id};
        }
        double size = width * 0.2;
        QFont mono = ShelfFont(QStringLiteral("ChakraPetch-Medium.ttf"), size);
        double widest = 0;
        for (const QString& line : lines) {
            widest = std::max(widest, Advance(mono, line));
        }
        if (widest > width * 0.9) {
            mono = ShelfFont(QStringLiteral("ChakraPetch-Medium.ttf"), size * width * 0.9 / widest);
        }
        p.setFont(mono);
        p.setPen(style.foot_ink);
        p.drawText(QRectF(0, height - foot, width, foot), Qt::AlignCenter, lines.join(QLatin1Char('\n')));
    }
    // The title, from just under the head to just over the foot, as large as it will fit.
    const double from = top + width * 0.5, until = height - foot - width * 0.4;
    const double room = until - from;
    const bool japanese = HasJapanese(title);
    double size = width * (japanese ? 0.62 : 0.5);
    QFont face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Bold.ttf"), size);
    // Each character a cell going down, a run of Latin letters lying on its side.
    const auto measure = [&](const QFont& f) {
        const double cell = QFontMetricsF(f).height() * 0.92;
        double length = 0;
        for (int c = 0; c < title.size(); ++c) {
            if (title[c].isSpace()) {
                length += cell * 0.4;
                continue;
            }
            int run = 0;
            while (c + run < title.size() && IsLatin(title[c + run])) {
                ++run;
            }
            if (run > 2 || (!japanese && run > 0)) {
                length += Advance(f, title.mid(c, run)) + cell * 0.15;
                c += run - 1;
            } else {
                length += cell;
            }
        }
        return length;
    };
    while (size > width * 0.24 && measure(face) > room) {
        size *= 0.94;
        face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Bold.ttf"), size);
    }
    p.save();
    p.setClipRect(QRectF(0, from, width, std::max(0.0, room)));
    p.setFont(face);
    p.setPen(style.ink);
    const double cell = QFontMetricsF(face).height() * 0.92;
    double y = from;
    for (int c = 0; c < title.size() && y < until; ++c) {
        const QChar ch = title[c];
        if (ch.isSpace()) {
            y += cell * 0.4;
            continue;
        }
        int run = 0;
        while (c + run < title.size() && IsLatin(title[c + run])) {
            ++run;
        }
        if (run > 2 || (!japanese && run > 0)) {
            const QString word = title.mid(c, run);
            p.save();
            p.translate(width / 2.0, y);
            p.rotate(90);
            p.drawText(QPointF(0, QFontMetricsF(face).ascent() / 2 - QFontMetricsF(face).descent() / 2), word);
            p.restore();
            y += Advance(face, word) + cell * 0.15;
            c += run - 1;
            continue;
        }
        const QRectF at(0, y, width, cell);
        if (LiesDown(ch)) {
            p.save();
            p.translate(at.center());
            p.rotate(90);
            p.drawText(QRectF(-cell / 2, -width / 2.0, cell, width), Qt::AlignCenter, QString(ch));
            p.restore();
        } else {
            p.drawText(at, Qt::AlignCenter, QString(ch));
        }
        y += cell;
    }
    p.restore();
    // Subtle 3D case bevel: light highlight on the left edge, darker shadow on the right.
    p.fillRect(QRectF(0, 0, 2.0, height), QColor(255, 255, 255, 56));
    p.fillRect(QRectF(width - 2.0, 0, 2.0, height), QColor(0, 0, 0, 88));
    return image;
}

/// The art code of a console as the library names it (a title read from lines has no ID that says).
QString PlatformOfSystem(const QString& system) {
    static const QHash<QString, QString> known{{QStringLiteral("PC"), QStringLiteral("pc")},
                                               {QStringLiteral("Nintendo Switch"), QStringLiteral("swi")},
                                               {QStringLiteral("Nintendo 3DS"), QStringLiteral("n3d")},
                                               {QStringLiteral("Nintendo DS"), QStringLiteral("nds")},
                                               {QStringLiteral("PlayStation"), QStringLiteral("ps1")},
                                               {QStringLiteral("PlayStation 2"), QStringLiteral("ps2")},
                                               {QStringLiteral("PlayStation 3"), QStringLiteral("ps3")},
                                               {QStringLiteral("PlayStation Portable"), QStringLiteral("psp")},
                                               {QStringLiteral("PlayStation Vita"), QStringLiteral("psv")},
                                               {QStringLiteral("Dreamcast"), QStringLiteral("drc")}};
    return known.value(system);
}

} // namespace

HomeWindow::StripLook HomeWindow::LookOfStrip() {
    if (!shelf_look) {
        ShelfLoad();
    }
    const ShelfLook& l = *shelf_look;
    return {l.ground, l.ink, l.muted, l.accent, l.rule, ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 26), ShelfFont(l.mono, 11, 0.14)};
}

HomeWindow::ScreenLook HomeWindow::LookOfScreen() {
    if (!shelf_look) {
        ShelfLoad();
    }
    const ShelfLook l = *shelf_look;
    return {l.ground, l.raised.isValid() && l.raised.alpha() > 0 ? l.raised : l.ground, l.ink, l.muted, l.accent,
            l.rule.isValid() && l.rule.alpha() > 0 ? l.rule : l.muted,
            [l](int face, double px, double spacing) { return ShelfFont(face == 0 ? l.display : face == 1 ? l.text : l.mono, px, spacing); }};
}

QString HomeWindow::AddLinesTitle(const QString& by, const QString& title) {
    const QString folder = SharedFolder();
    Lines::Sources sources = Lines::Load(folder);
    const QString id = Lines::Add(sources, title, by, RecordNamed(title.trimmed()));
    if (id.isEmpty() || !Lines::Save(folder, sources)) {
        sounds.Play(Sound::Refuse);
        Say(id.isEmpty() ? QStringLiteral("Enter a name and choose a texthooker or the clipboard first")
                         : QStringLiteral("Couldn't save the game. Check that Furoshiki's data folder can be written"));
        return {};
    }
    if (lines_changed) {
        lines_changed();
    } else {
        Lines::WriteApp(folder, sources, QCoreApplication::applicationFilePath(), nullptr);
    }
    sounds.Play(Sound::Select);
    ReloadKeepingChoice();
    Changed();
    Say(QStringLiteral("Added %1 to the library").arg(title.trimmed()));
    return id;
}

void HomeWindow::SetLinesAddress(const QString& address) {
    const QUrl url(address.trimmed());
    if (!url.isValid() || url.scheme() != QStringLiteral("ws") || url.host().isEmpty()) {
        sounds.Play(Sound::Refuse);
        Say(QStringLiteral("Enter a WebSocket address such as ws://localhost:6677"));
        return;
    }
    Lines::Sources sources = Lines::Load(SharedFolder());
    sources.address = url.toString();
    Lines::Save(SharedFolder(), sources);
    sounds.Play(Sound::Select);
    Changed();
}

QString HomeWindow::TrackerUrl() const {
    const QString said = qEnvironmentVariable("READER_HOME_TRACKER");
    QString url = said.isEmpty() ? QSettings().value(QStringLiteral("tracker/url")).toString() : said;
    if (url.isEmpty()) {
        // None set by hand: the one the paired save sync service names (services.h).
        url = Services::Of(QStringLiteral("tracker"));
    }
    while (url.endsWith(QLatin1Char('/'))) {
        url.chop(1);
    }
    return url;
}

void HomeWindow::SetTrackerUrl(const QString& url) {
    QSettings().setValue(QStringLiteral("tracker/url"), url);
    tracker_by_jiten.clear();
    tracker_by_title.clear();
    tracker_asked = 0;
    shelf_stale = true;
    TrackerAsk();
    update();
}

const HomeWindow::TrackerTitle* HomeWindow::TrackerOf(const QString& game_id) const {
    const TitleRecord record = RecordOf(game_id);
    if (record.jiten != 0) {
        const auto found = tracker_by_jiten.constFind(record.jiten);
        if (found != tracker_by_jiten.constEnd()) {
            return &*found;
        }
    }
    if (!record.tracker_title.isEmpty()) {
        const auto found = tracker_by_title.constFind(record.tracker_title);
        if (found != tracker_by_title.constEnd()) {
            return &*found;
        }
    }
    return nullptr;
}

void HomeWindow::TrackerAsk() {
#ifdef HOME_TRACKER
    const QString url = TrackerUrl();
    if (url.isEmpty() || tracker_waiting) {
        return;
    }
    if (tracker_net == nullptr) {
        tracker_net = new QNetworkAccessManager(this);
    }
    tracker_asked = clock.elapsed() + 1;
    tracker_waiting = true;
    QNetworkRequest request(QUrl(url + QStringLiteral("/api/media")));
    request.setTransferTimeout(15000);
    QNetworkReply* reply = static_cast<QNetworkAccessManager*>(tracker_net)->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        tracker_waiting = false;
        if (reply->error() != QNetworkReply::NoError) {
            qInfo("the study tracker did not answer: %s", qPrintable(reply->errorString()));
            return; // what it said before stands
        }
        const QJsonArray items = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("items")).toArray();
        if (items.isEmpty()) {
            return;
        }
        tracker_by_jiten.clear();
        tracker_by_title.clear();
        for (const QJsonValue value : items) {
            const QJsonObject item = value.toObject();
            const QJsonObject jiten = item.value(QStringLiteral("jiten")).toObject();
            TrackerTitle title;
            title.read = item.value(QStringLiteral("characterProgress")).toInteger();
            title.total = item.value(QStringLiteral("characterCount")).toInteger();
            title.words = item.value(QStringLiteral("wordCount")).toInt();
            title.unique = item.value(QStringLiteral("uniqueWordCount")).toInt();
            title.speed = item.value(QStringLiteral("readingSpeed")).toInt();
            title.sessions = item.value(QStringLiteral("sessions")).toInt();
            title.rating = item.value(QStringLiteral("externalRating")).toInt();
            title.difficulty = item.value(QStringLiteral("difficulty")).toDouble();
            title.coverage = jiten.value(QStringLiteral("coverage")).toDouble();
            title.hours = item.value(QStringLiteral("immersionHours")).toDouble();
            title.last = item.value(QStringLiteral("lastLoggedOn")).toString();
            const qint64 deck = item.value(QStringLiteral("jitenDeckId")).toInteger();
            // A title the tracker has twice (an anime and its manga) is told apart by its deck.
            if (deck != 0) {
                tracker_by_jiten.insert(deck, title);
            }
            tracker_by_title.insert(item.value(QStringLiteral("originalTitle")).toString(), title);
        }
        shelf_stale = true; // what was read last may be another title now
        update();
    });
#endif
}

void HomeWindow::ShelfLoad() {
    // The themes there are, in their order; the one chosen last (Mono at first).
    QList<QPair<int, QString>> found;
    const QDir folder(QStringLiteral(":/shelf"));
    for (const QString& file : folder.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile data(folder.filePath(file));
        if (data.open(QIODevice::ReadOnly)) {
            found.append({QJsonDocument::fromJson(data.readAll()).object().value(QStringLiteral("order")).toInt(99),
                          QFileInfo(file).completeBaseName()});
        }
    }
    std::sort(found.begin(), found.end());
    shelf_themes.clear();
    for (const auto& theme : found) {
        shelf_themes.append(theme.second);
    }
    QSettings settings;
    // The shelf is the page since its own pages for statistics and the settings came (2026-11-10);
    // F3, or "save" here, brings the save menu back while it still exists.
#ifdef HOME_PUBLIC
    shelf_on = true; // a published build has the shelf alone
#else
    shelf_on = settings.value(QStringLiteral("look/shape")).toString() != QStringLiteral("save");
#endif
    dots_on = settings.value(QStringLiteral("shelf/dots"), true).toBool();
    show_stats_tab = settings.value(QStringLiteral("shelf/show_stats"), true).toBool();
    shelf_layout = std::clamp(settings.value(QStringLiteral("shelf/layout"), 0).toInt(), 0, 2);
    dots_mode = static_cast<int>((QDateTime::currentMSecsSinceEpoch() ^ reinterpret_cast<quintptr>(this)) % 3);
    if (dots_mode < 0) {
        dots_mode = -dots_mode;
    }
    SetShelfTheme(settings.value(QStringLiteral("shelf/theme"), QStringLiteral("kon")).toString(), false);
    SortLoad();
    if (shelf_on) {
        TrackerAsk();
    }
}

void HomeWindow::SetShelfTheme(const QString& key, bool keep) {
    const QString chosen = shelf_themes.contains(key) ? key : shelf_themes.value(0);
    auto look = std::make_shared<ShelfLook>();
    look->key = chosen;
    QJsonObject values;
    QFile data(QStringLiteral(":/shelf/%1.json").arg(chosen));
    if (data.open(QIODevice::ReadOnly)) {
        values = QJsonDocument::fromJson(data.readAll()).object();
    }
    const auto colour = [&](const char* name, const char* otherwise) {
        bool ok = false;
        const QColor read = ParseColour(values.value(QLatin1String(name)).toString(), &ok);
        return ok ? read : QColor(QLatin1String(otherwise));
    };
    look->name = values.value(QStringLiteral("name")).toString(chosen);
    look->ground = colour("ground", "#0a0a0a");
    look->raised = colour("raised", "#161616");
    look->ink = colour("ink", "#f2f2f0");
    look->muted = colour("muted", "#8c8c88");
    look->accent = colour("accent", "#f2f2f0");
    look->rule = colour("rule", "#444444");
    look->display = values.value(QStringLiteral("display")).toString(QStringLiteral("Michroma-Regular.ttf"));
    look->text = values.value(QStringLiteral("text")).toString(QStringLiteral("ZenKakuGothicNew-Bold.ttf"));
    look->mono = values.value(QStringLiteral("mono")).toString(QStringLiteral("ChakraPetch-Medium.ttf"));
    look->beam = values.value(QStringLiteral("beam")).toBool(false);
    shelf_look = look;
    // The popup takes the shelf's look unless one was chosen (its theme "auto", the default).
    ReaderApp::ReaderSettings::SetPopupThemeDefault(ReaderApp::ReaderSettings::PopupThemeOfShelf(chosen));
    if (keep) {
        QSettings().setValue(QStringLiteral("shelf/theme"), chosen);
    }
    update();
}

void HomeWindow::SetShelf(bool on, bool keep) {
    shelf_on = on;
    shelf_at = shelf_choice;
    if (on && tracker_asked == 0) {
        TrackerAsk();
    }
    if (keep) {
        QSettings().setValue(QStringLiteral("look/shape"), on ? QStringLiteral("shelf") : QStringLiteral("save"));
    }
    setCursor(Qt::ArrowCursor);
    Changed();
}

QStringList HomeWindow::ShelfSections() const {
    return Systems();
}

const QList<HomeWindow::ShelfTitle>& HomeWindow::ShelfTitles() {
    if (!shelf_stale) {
        return shelf_titles;
    }
    shelf_stale = false;
    const QStringList systems = Systems();
    shelf_section = std::clamp(shelf_section, 0, static_cast<int>(systems.size()));
    const QString kept = shelf_titles.isEmpty() ? shelf_kept.value(shelf_section) : shelf_titles.value(shelf_choice).game.path;
    shelf_titles.clear();
    for (int i = 0; i < systems.size(); ++i) {
        if (shelf_section != 0 && shelf_section != i + 1) {
            continue;
        }
        for (const Game& game : GamesOf(systems[i])) {
            shelf_titles.append({game, systems[i]});
        }
    }
    // In the order chosen (SortLine): each key in turn, the next only where the one before is
    // even; a title that has nothing for a key (never read, not in the study tracker) after those
    // that have, whichever way; and by name at the last.
    struct Facts {
        QString last, name, console;
        qint64 read{}, time{}, length{};
        double progress{-1};
    };
    QHash<QString, Facts> facts;
    for (const ShelfTitle& title : shelf_titles) {
        const Game& game = title.game;
        const TrackerTitle* tracked = TrackerOf(game.id);
        Facts f;
        f.last = tracked != nullptr ? std::max(tracked->last, game.last_day) : game.last_day;
        f.name = ShelfTitleOf(game.id, game.title).toCaseFolded();
        f.console = SystemLabel(library, title.system).toCaseFolded();
        f.read = tracked != nullptr ? tracked->read : game.chars;
        f.time = tracked != nullptr && tracked->hours > 0 ? static_cast<qint64>(tracked->hours * 3.6e6) : game.active_ms;
        f.length = tracked != nullptr ? tracked->total : 0;
        f.progress = tracked != nullptr && tracked->total > 0 ? static_cast<double>(tracked->read) / tracked->total : -1;
        facts.insert(game.path + QLatin1Char('\n') + game.id, f);
    }
    const QList<ShelfSortKey> order = shelf_sort;
    const auto compare = [](const auto& a, const auto& b, bool a_has, bool b_has, bool down) {
        if (a_has != b_has) {
            return a_has ? -1 : 1;
        }
        if (!a_has || a == b) {
            return 0;
        }
        return (a < b) != down ? -1 : 1;
    };
    std::stable_sort(shelf_titles.begin(), shelf_titles.end(), [&](const ShelfTitle& x, const ShelfTitle& y) {
        const Facts& a = facts[x.game.path + QLatin1Char('\n') + x.game.id];
        const Facts& b = facts[y.game.path + QLatin1Char('\n') + y.game.id];
        for (const ShelfSortKey& by : order) {
            int c = 0;
            switch (by.key) {
            case 0: c = compare(a.last, b.last, !a.last.isEmpty(), !b.last.isEmpty(), by.down); break;
            case 1: c = compare(a.name, b.name, true, true, by.down); break;
            case 2: c = compare(a.console, b.console, true, true, by.down); break;
            case 3: c = compare(a.read, b.read, a.read > 0, b.read > 0, by.down); break;
            case 4: c = compare(a.time, b.time, a.time > 0, b.time > 0, by.down); break;
            case 5: c = compare(a.progress, b.progress, a.progress >= 0, b.progress >= 0, by.down); break;
            case 6: c = compare(a.length, b.length, a.length > 0, b.length > 0, by.down); break;
            default: break;
            }
            if (c != 0) {
                return c < 0;
            }
        }
        return a.name < b.name;
    });
    shelf_choice = kept == QStringLiteral("\n+") ? static_cast<int>(shelf_titles.size()) : 0; // the spine that adds one
    for (int i = 0; i < shelf_titles.size(); ++i) {
        if (!kept.isEmpty() && shelf_titles[i].game.path == kept) {
            shelf_choice = i;
        }
    }
    // A title just added is the one in hand, once its file lists it.
    for (int i = 0; !shelf_want.isEmpty() && i < shelf_titles.size(); ++i) {
        if (shelf_titles[i].game.id.compare(shelf_want, Qt::CaseInsensitive) == 0 && !Lines::WayOf(shelf_titles[i].game.path).isEmpty()) {
            shelf_choice = i;
            shelf_want.clear();
        }
    }
    shelf_at = shelf_choice;
    return shelf_titles;
}

void HomeWindow::ShelfMove(int by) {
    // After the last title stands the spine that adds one (not on a title's stage).
    const int count = static_cast<int>(ShelfTitles().size()) + (shelf_stage ? 0 : 1);
    const int next = std::clamp(shelf_choice + by, 0, std::max(0, count - 1));
    if (next == shelf_choice) {
        return;
    }
    shelf_choice = next;
    sounds.Play(Sound::Move);
    update();
}

void HomeWindow::SetShelfSection(int section) {
    const int count = static_cast<int>(ShelfSections().size()) + 1;
    section = ((section % count) + count) % count;
    if (section == shelf_section) {
        return;
    }
    // The title in hand here is kept for coming back; the shelf as it stands goes with the lift.
    const QList<ShelfTitle>& titles = ShelfTitles();
    shelf_kept.insert(shelf_section, shelf_choice < titles.size() ? titles[shelf_choice].game.path : QStringLiteral("\n+"));
    if (!SystemWantsStillness() && isVisible()) {
        lift_old = ShelfSnapshot();
        lift_t = 0;
    }
    lift_dir = section > shelf_section ? 1 : -1;
    shelf_stage = false;
    shelf_section = section;
    shelf_stale = true;
    shelf_titles.clear();
    spine_lift.clear();
    sounds.Play(Sound::Tab);
    update();
}

QList<Game> HomeWindow::SectionGames(int section) const {
    const QStringList systems = Systems();
    QList<Game> games;
    for (int i = 0; i < systems.size(); ++i) {
        if (section == 0 || section == i + 1) {
            games.append(GamesOf(systems[i]));
        }
    }
    return games;
}

QImage HomeWindow::ShelfSnapshot() {
    const double fit = std::min(width() / SheetW, height() / SheetH);
    const qreal dpr = devicePixelRatioF();
    QImage image(QSize(static_cast<int>(std::ceil(SheetW * fit * dpr)), static_cast<int>(std::ceil(SheetH * fit * dpr))), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.scale(fit * dpr, fit * dpr);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    const QList<Hit> hits = shelf_hits;
    const double shift = shelf_shift, fade = shelf_fade;
    shelf_shift = 0;
    shelf_fade = 1;
    PaintShelfBody(p);
    shelf_shift = shift;
    shelf_fade = fade;
    shelf_hits = hits;
    return image;
}

namespace {

/// The shelf's sort keys (the maintainer, 2026-11-10: "sort the all page by title, by console, by
/// date etc and can have multiple mixed sorts in priority order").
struct SortKeyInfo {
    const char* name;
    bool down;           ///< Its way when it is added
    const char* up_word; ///< What each way is called
    const char* down_word;
};
constexpr SortKeyInfo SortKeys[] = {
    {"LAST READ", true, "OLDEST FIRST", "NEWEST FIRST"},
    {"TITLE", false, "A TO Z", "Z TO A"},
    {"CONSOLE", false, "A TO Z", "Z TO A"},
    {"CHARACTERS READ", true, "FEWEST", "MOST"},
    {"READING TIME", true, "LEAST", "MOST"},
    {"PROGRESS", true, "LEAST", "FURTHEST"},
    {"LENGTH", false, "SHORTEST", "LONGEST"},
};
constexpr int SortKeyCount = static_cast<int>(std::size(SortKeys));

} // namespace

void HomeWindow::SortLoad() {
    shelf_sort.clear();
    // "0-,1+": each key and its way, the first the strongest.
    for (const QString& part : QSettings().value(QStringLiteral("shelf/sort"), QStringLiteral("0-")).toString().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int key = part.left(part.size() - 1).toInt(&ok);
        if (ok && key >= 0 && key < SortKeyCount && std::none_of(shelf_sort.begin(), shelf_sort.end(), [&](const ShelfSortKey& k) { return k.key == key; })) {
            shelf_sort.append({key, part.endsWith(QLatin1Char('-'))});
        }
    }
}

void HomeWindow::SortSave() const {
    QStringList parts;
    for (const ShelfSortKey& k : shelf_sort) {
        parts.append(QString::number(k.key) + (k.down ? QLatin1Char('-') : QLatin1Char('+')));
    }
    QSettings().setValue(QStringLiteral("shelf/sort"), parts.join(QLatin1Char(',')));
}

/// The keys as the sort line shows them: those in use in their order, then the rest.
template <typename Key>
QList<int> SortShown(const QList<Key>& sort) {
    QList<int> shown;
    for (const auto& k : sort) {
        shown.append(k.key);
    }
    for (int key = 0; key < SortKeyCount; ++key) {
        if (!shown.contains(key)) {
            shown.append(key);
        }
    }
    return shown;
}

void HomeWindow::SortPress(int key, bool remove) {
    const auto at = std::find_if(shelf_sort.begin(), shelf_sort.end(), [&](const ShelfSortKey& k) { return k.key == key; });
    if (remove) {
        if (at == shelf_sort.end()) {
            return;
        }
        shelf_sort.erase(at);
        sounds.Play(Sound::Back);
    } else if (at != shelf_sort.end()) {
        at->down = !at->down;
        sounds.Play(Sound::Move);
    } else {
        shelf_sort.append({key, SortKeys[key].down});
        sounds.Play(Sound::Select);
    }
    sort_choice = std::clamp(static_cast<int>(SortShown(shelf_sort).indexOf(key)), 0, SortKeyCount - 1);
    SortSave();
    shelf_stale = true; // the title in hand stays in hand (ShelfTitles keeps it by its file)
    update();
}

void HomeWindow::PaintSortLine(QPainter& p, double baseline) {
    const ShelfLook& l = *shelf_look;
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont word = ShelfFont(l.mono, 10.5, 0.1);
    double x = ShelfLeft;
    x += Put(p, word, x, baseline, QStringLiteral("SORT"), l.accent) + 18;
    const QList<int> shown = SortShown(shelf_sort);
    sort_choice = std::clamp(sort_choice, 0, SortKeyCount - 1);
    for (int i = 0; i < shown.size(); ++i) {
        const int key = shown[i];
        const auto in = std::find_if(shelf_sort.begin(), shelf_sort.end(), [&](const ShelfSortKey& k) { return k.key == key; });
        const bool used = in != shelf_sort.end();
        const bool lit = ShelfHovered(12, key);
        const double from = x;
        if (used) {
            x += Put(p, tiny, x, baseline, QStringLiteral("%1 ").arg(in - shelf_sort.begin() + 1), l.accent);
            x += Put(p, word, x, baseline, QLatin1String(SortKeys[key].name), l.ink);
            x += Put(p, tiny, x, baseline, QStringLiteral("  ") + QLatin1String(in->down ? SortKeys[key].down_word : SortKeys[key].up_word),
                     lit ? l.ink : l.muted);
        } else {
            x += Put(p, word, x, baseline, QStringLiteral("+ ") + QLatin1String(SortKeys[key].name), lit ? l.ink : Faded(l.muted, 0.8));
        }
        shelf_hits.append({QRectF(from - 4, baseline - 18, x - from + 8, 28), 12, key});
        if (i == sort_choice) {
            p.fillRect(QRectF(from, baseline + 7, x - from, 2), l.accent);
        }
        if (used) {
            // Taken off by its cross.
            const double cross = x + 8;
            x = cross + Put(p, word, cross, baseline, QStringLiteral("×"), ShelfHovered(14, key) ? l.ink : Faded(l.muted, 0.7));
            shelf_hits.append({QRectF(cross - 5, baseline - 18, x - cross + 10, 28), 14, key});
        }
        x += 22;
    }
    // Only the way out; the line says itself (the maintainer, 2026-11-10: "don't explain sort on the page").
    PutKeys(p, tiny, ShelfRight, baseline, {{QStringLiteral("ESC DONE"), Qt::Key_Escape}});
}

namespace {

/// What a key of a foot is on a controller: its letter ("A", "×", "L1"), the arrow itself for
/// the d-pad, or nothing where the controller has no button for it.
QString PadGlyph(const QString& key, int style) {
    static const QString arrows = QStringLiteral("←↑↓→");
    if (key.size() == 1 && arrows.contains(key)) {
        return key;
    }
    // The letters by the controller's family, as everywhere else (pad_names.h).
    // The controller in use's own (its layout as set up), else a family's for a script's "pad".
    const Home::PadNames::Names current = Home::PadNames::Current();
    const Home::PadNames::Names names = current.family == style ? current : Home::PadNames::OfFamily(style);
    if (key == QStringLiteral("ENTER")) {
        return names.south;
    }
    if (key == QStringLiteral("ESC")) {
        return names.east;
    }
    if (key == QStringLiteral("F5")) {
        return names.north;
    }
    if (key == QStringLiteral("Q")) {
        return names.l1;
    }
    if (key == QStringLiteral("E")) {
        return names.r1;
    }
    return {};
}

/// One hint of a foot for a controller: its buttons, then what they do. Empty where none of
/// its keys is a button ("F2 SETTINGS", "TYPE A NAME").
struct PadHint {
    QStringList caps;
    QString action;
    int key{-1};
};

PadHint PadHintOf(const QString& text, int key, int style) {
    PadHint hint;
    hint.key = key;
    QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    // The keys come first; the first word that is no key begins what they do. A hint that
    // begins with words ("DRAG OR ← ↑ ↓ →") has its keys after them.
    while (!words.isEmpty() && (words.first() == QStringLiteral("DRAG") || words.first() == QStringLiteral("OR"))) {
        words.removeFirst();
    }
    while (!words.isEmpty()) {
        const QString glyph = PadGlyph(words.first(), style);
        if (glyph.isEmpty()) {
            break;
        }
        hint.caps.append(glyph);
        words.removeFirst();
    }
    hint.action = words.join(QLatin1Char(' '));
    return hint;
}

/// A foot's hints for a controller, set from `right` leftwards: each button a small filled cap
/// with its letter cut out in the ground's colour (the popup's key marks), the arrows as they
/// are, then the action. Returns where the line begins, and where each hint stands.
double PutPadHints(QPainter& p, const QFont& font, double right, double baseline, const QList<PadHint>& hints, const QColor& ink,
                   const QColor& muted, const QColor& ground, QList<QPair<QRectF, int>>* places) {
    static const QString arrows = QStringLiteral("←↑↓→");
    QFont legend = font;
    legend.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    legend.setBold(true);
    const double tall = QFontMetricsF(font).capHeight() + 7;
    const auto cap_wide = [&](const QString& cap) {
        return arrows.contains(cap) ? Advance(font, cap) : std::max(tall, Advance(legend, cap) + 8);
    };
    for (int i = static_cast<int>(hints.size()) - 1; i >= 0; --i) {
        const PadHint& hint = hints[i];
        if (hint.caps.isEmpty()) {
            continue;
        }
        double wide = hint.action.isEmpty() ? 0 : Advance(font, hint.action) + 6;
        for (const QString& cap : hint.caps) {
            wide += cap_wide(cap) + 4;
        }
        wide -= 4;
        double x = right - wide;
        const double left = x;
        for (const QString& cap : hint.caps) {
            const double w = cap_wide(cap);
            if (arrows.contains(cap)) {
                Put(p, font, x, baseline, cap, muted);
            } else {
                const QRectF box(x, baseline - QFontMetricsF(font).capHeight() - 3.5, w, tall);
                p.setPen(Qt::NoPen);
                p.setBrush(muted);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.drawRoundedRect(box, tall / 2, tall / 2);
                p.setFont(legend);
                p.setPen(ground);
                p.drawText(box, Qt::AlignCenter, cap);
            }
            x += w + 4;
        }
        if (!hint.action.isEmpty()) {
            Put(p, font, x + 2, baseline, hint.action, ink);
        }
        if (places != nullptr) {
            places->append({QRectF(left - 6, baseline - 18, wide + 12, 28), hint.key});
        }
        right = left - 22;
    }
    return right + 22;
}

} // namespace

void HomeWindow::PutFoot(QPainter& p, const QFont& font, const QString& keys) {
    const ShelfLook& l = *shelf_look;
    const int style = PadStyle();
    if (style < 0) {
        Put(p, font, ShelfRight, 674, keys, l.muted, 1);
        return;
    }
    QList<PadHint> hints;
    static const QRegularExpression between(QStringLiteral(" {3,}"));
    for (const QString& group : keys.split(between, Qt::SkipEmptyParts)) {
        hints.append(PadHintOf(group, -1, style));
    }
    PutPadHints(p, font, ShelfRight, 674, hints, l.muted, l.muted, l.ground, nullptr);
}

void HomeWindow::PaintReadingHud(QPainter& p, const QRectF& picture, const QString& mark, const QList<QPair<QStringList, QString>>& hints) {
    if (shelf_look == nullptr || !picture.isValid()) {
        return;
    }
    // Over a game's picture, whatever the theme: light on a dark glass, as the popup's marks.
    const QColor ground(0x0B, 0x0E, 0x14, 0xD2);
    const QColor ink(0xF4, 0xF4, 0xF2);
    const QColor muted(0xC4, 0xC9, 0xD1);
    const double scale = std::clamp(picture.height() / 720.0, 0.8, 3.0);
    const double wide = picture.width() / scale;
    const double high = picture.height() / scale;
    const QFont tiny = ShelfFont(shelf_look->mono, 9.5, 0.14);
    p.translate(picture.topLeft());
    p.scale(scale, scale);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    if (!mark.isEmpty()) {
        const QRectF box(14, 12, Advance(tiny, mark) + 24, 24);
        p.setPen(Qt::NoPen);
        p.setBrush(ground);
        p.drawRoundedRect(box, 12, 12);
        Put(p, tiny, box.left() + 12, box.bottom() - 8, mark, ink);
    }
    QList<PadHint> list;
    for (const auto& [caps, action] : hints) {
        list.append({caps, action, -1});
    }
    if (!list.isEmpty()) {
        // Measured on nothing first, then set in the middle of the foot on a glass of its own.
        QImage nothing(1, 1, QImage::Format_ARGB32_Premultiplied);
        QPainter measure(&nothing);
        const double line = wide - PutPadHints(measure, tiny, wide, 0, list, ink, muted, ground, nullptr);
        measure.end();
        const double right = std::min(wide - 14, (wide + line) / 2);
        const QRectF box(right - line - 14, high - 12 - 26, line + 28, 26);
        p.setPen(Qt::NoPen);
        p.setBrush(ground);
        p.drawRoundedRect(box, 13, 13);
        PutPadHints(p, tiny, right, box.bottom() - 9, list, ink, muted, QColor(0x0B, 0x0E, 0x14), nullptr);
    }
}

double HomeWindow::PutKeys(QPainter& p, const QFont& font, double right, double baseline, const QList<QPair<QString, int>>& keys) {
    const ShelfLook& l = *shelf_look;
    if (const int style = PadStyle(); style >= 0) {
        // A controller is the input: its buttons, by the letters on them. A hint of one key
        // ("↑", "Q") belongs with the next one's action, as the keys' line has it.
        QList<PadHint> hints;
        QStringList before;
        for (const auto& [text, key] : keys) {
            PadHint hint = PadHintOf(text, key, style);
            if (!hint.caps.isEmpty() && hint.action.isEmpty() && !text.contains(QLatin1Char(' '))) {
                before += hint.caps;
                continue;
            }
            if (!hint.caps.isEmpty()) {
                hint.caps = before + hint.caps;
                hints.append(hint);
            }
            before.clear();
        }
        QList<QPair<QRectF, int>> places;
        const double begins = PutPadHints(p, font, right, baseline, hints, l.muted, l.muted, l.ground, &places);
        for (const auto& [area, key] : places) {
            if (key >= 0) {
                shelf_hits.append({area, 11, key});
            }
        }
        return begins;
    }
    for (int i = static_cast<int>(keys.size()) - 1; i >= 0; --i) {
        const double width = Advance(font, keys[i].first);
        const double left = right - width;
        const double lit = keys[i].second >= 0 ? Ease(11, keys[i].second & 0xffff, ShelfHovered(11, keys[i].second)) : 0;
        Put(p, font, left, baseline, keys[i].first, Mix(l.muted, l.ink, lit));
        if (keys[i].second >= 0) {
            shelf_hits.append({QRectF(left - 6, baseline - 18, width + 12, 28), 11, keys[i].second});
        }
        right = left - 20;
    }
    return right + 20;
}

QPixmap HomeWindow::TitleCover(const Game& game) {
    // The front of its box, fetched by tools/titles.py covers and built in (as the spines are),
    // else one found here (Find box art), before what its app gives: an app's picture is often
    // its icon or a screen, not the box.
    const QString key = RecordOf(game.id).key, platform = ArtPlatform(game.id);
    QPixmap box = key.isEmpty() || platform.isEmpty() ? QPixmap() : Cover(QStringLiteral(":/covers/%1.%2.jpg").arg(key, platform));
    if (box.isNull()) {
        box = Cover(Art::CoverFile(SharedFolder(), game.id));
    }
    return box.isNull() ? Cover(game.cover) : box;
}

QString HomeWindow::ConsoleOfGame(const Game& game) const {
    return game.system;
}

QPixmap HomeWindow::SpineOf(const QString& key, const QString& game_id) {
    const QString platform = ArtPlatform(game_id);
    const QString name = key + QLatin1Char('.') + platform;
    auto found = spine_cache.find(name);
    if (found == spine_cache.end()) {
        found = spine_cache.insert(name, key.isEmpty() || platform.isEmpty() ? QPixmap() : QPixmap(QStringLiteral(":/spines/%1.jpg").arg(name)));
    }
    return *found;
}

QPixmap HomeWindow::ShelfSpine(const Game& game) {
    const QPixmap real = SpineOf(RecordOf(game.id).key, game.id);
    if (!real.isNull()) {
        return real;
    }
    const QPixmap found = Cover(Art::SpineFile(SharedFolder(), game.id));
    if (!found.isNull()) {
        return found;
    }
    QString platform = ArtPlatform(game.id);
    if (platform.isEmpty()) {
        platform = PlatformOfSystem(game.system);
    }
    const SpineStyle* style = SpineStyleOf(platform);
    if (style == nullptr) {
        return {};
    }
    const QString key = platform + QLatin1Char('\n') + game.id + QLatin1Char('\n') + game.title;
    auto at = made_spines.find(key);
    if (at == made_spines.end()) {
        at = made_spines.insert(key, QPixmap::fromImage(MadeSpine(*style, ShelfName(game.title), platform == QStringLiteral("swi") || game.id.startsWith(QStringLiteral("LINES-")) ? QString() : game.id)));
    }
    return *at;
}

bool HomeWindow::ArtMissing(const Game& game) {
    const bool real_spine = !SpineOf(RecordOf(game.id).key, game.id).isNull() || QFileInfo::exists(Art::SpineFile(SharedFolder(), game.id));
    const QString key = RecordOf(game.id).key, platform = ArtPlatform(game.id);
    const bool box = (!key.isEmpty() && !platform.isEmpty() && QFileInfo::exists(QStringLiteral(":/covers/%1.%2.jpg").arg(key, platform))) ||
                     QFileInfo::exists(Art::CoverFile(SharedFolder(), game.id));
    return !box || !real_spine;
}

void HomeWindow::FindArt() {
    const QList<ShelfTitle>& titles = ShelfTitles();
    if (shelf_choice >= titles.size()) {
        return;
    }
    const Game game = titles[shelf_choice].game;
    if (!art_finder) {
        art_finder = new Art::Finder(this);
        connect(art_finder, &Art::Finder::Done, this, [this](const QString& id, bool found, const QString& said) {
            art_said = said.toUpper();
            art_said_of = id;
            // What was drawn of it before goes; the shelf draws it again with what was found.
            covers.remove(Art::CoverFile(SharedFolder(), id));
            covers.remove(Art::SpineFile(SharedFolder(), id));
            shelf_tints.clear();
            sounds.Play(found ? Sound::Select : Sound::Refuse);
            Say(said);
            update();
        });
    }
    if (art_finder->Busy()) {
        return;
    }
    Art::Want want;
    want.game_id = game.id;
    want.title = ShelfName(game.title);
    want.english = RecordOf(game.id).english;
    want.system = game.system;
    want.vndb = RecordOf(game.id).vndb;
    // A title hooked in a program: that program's file, and its whole path where it was kept.
    const Lines::Sources sources = Lines::Load(SharedFolder());
    for (const Lines::Title& title : sources.titles) {
        if (title.id == game.id) {
            want.exe_name = title.process;
            want.exe_path = title.path;
        }
    }
    if (want.exe_path.isEmpty() && !want.exe_name.isEmpty()) {
        if (const auto running = Hooks::ProgramNamed(want.exe_name)) {
            want.exe_path = running->path;
        }
    }
    art_said = QStringLiteral("SEARCHING…");
    art_said_of = game.id;
    art_finder->Start(want, SharedFolder());
    sounds.Play(Sound::Select);
    update();
}

void HomeWindow::GlideTo(double& at, double to, double time) {
    at = shelf_still || std::fabs(to - at) < 0.002 ? to : at + (to - at) * (1 - std::exp(-shelf_dt / time));
    shelf_busy = shelf_busy || at != to;
}

double HomeWindow::Ease(int kind, int index, bool on) {
    Eased& one = eased[(static_cast<quint32>(kind) << 20) | (static_cast<quint32>(index + 1) & 0xfffff)];
    one.to = on;
    one.seen = ease_frame;
    return one.at * one.at * (3 - 2 * one.at); // in and out softly
}

void HomeWindow::PagesDrag(const QPointF& at) {
    if (pages_drag == 1) {
        // The slider's tick follows the pointer: the row's steps, one at a time, towards it.
        const QList<Item> items = Items();
        const Item& item = items[std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1)];
        const int srow = item.kind == Item::Settings ? SettingAt(item.system, more_open.contains(item.system), RowChoice(item)) : -1;
        if (srow < 0 || SettingRows[srow].step == 0 || pages_drag_area.width() <= 0) {
            return;
        }
        const SettingRow& setting = SettingRows[srow];
        const double share = std::clamp((at.x() - pages_drag_area.left()) / pages_drag_area.width(), 0.0, 1.0);
        const double before = Slide(srow, 0);
        SetSlide(srow, std::round((setting.low + share * (setting.high - setting.low)) / setting.step) * setting.step);
        if (Slide(srow, 0) != before) {
            sounds.Play(Sound::Move);
            update();
        }
    } else if (pages_drag == 2 && pages_drag_area.height() > 0) {
        // The list's bar: where the pointer is along it is where the list stands.
        const QList<Item> items = Items();
        const int count = static_cast<int>(Rows(items[std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1)]).size());
        const double share = std::clamp((at.y() - pages_drag_area.top()) / pages_drag_area.height(), 0.0, 1.0);
        pages_first = static_cast<int>(std::lround(share * count - 6));
        update();
    }
}

void HomeWindow::ShelfHover(const QPointF& at, bool lights) {
    int kind = -1, index = -1;
    const bool idle = running.isEmpty() && !playing_game;
    // What is drawn last lies on top, and its hit comes last.
    for (const Hit& hit : shelf_hits) {
        if (hit.area.contains(at)) {
            kind = hit.kind;
            index = hit.index;
        }
    }
    const Qt::CursorShape shape = kind >= 0 && idle ? Qt::PointingHandCursor : Qt::ArrowCursor;
    if (cursor().shape() != shape) {
        setCursor(shape);
    }
    if (!idle || !lights) {
        kind = index = -1;
    }
    if (kind != shelf_hover_kind || index != shelf_hover_index) {
        shelf_hover_kind = kind;
        shelf_hover_index = index;
        update();
    }
}

void HomeWindow::ShelfPlay() {
    const QList<ShelfTitle>& titles = ShelfTitles();
    if (shelf_choice >= titles.size()) {
        OpenWizard(); // the last spine: a title is added
        return;
    }
    // Started as the save menu starts it: its console's page, its row, and that page's key.
    const ShelfTitle title = titles[shelf_choice];
    const QList<Item> items = Items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].kind != Item::System || items[i].system != title.system) {
            continue;
        }
        const QList<Game> games = GamesOf(title.system);
        for (int row = 0; row < games.size(); ++row) {
            if (games[row].id == title.game.id && games[row].path == title.game.path) {
                axis_choice = i;
                RowChoice(items[i]) = row;
                Accept();
                return;
            }
        }
    }
    sounds.Play(Sound::Refuse);
}

const HomeWindow::TitleReading& HomeWindow::ReadingOf(const QString& game) {
    auto found = title_readings.find(game);
    if (found == title_readings.end()) {
        found = title_readings.insert(game, title_reading && !game.isEmpty() ? title_reading(game) : TitleReading());
    }
    return *found;
}

void HomeWindow::SetLog(bool on) {
    if (shelf_log == on) {
        return;
    }
    if (on) {
        const QList<ShelfTitle>& titles = ShelfTitles();
        if (shelf_choice >= titles.size() || ReadingOf(titles[shelf_choice].game.id).lines.isEmpty()) {
            sounds.Play(Sound::Refuse);
            Say(QStringLiteral("No lines read yet"));
            return;
        }
        log_line = -1;
    }
    shelf_log = on;
    sounds.Play(on ? Sound::Select : Sound::Back);
    update();
}

void HomeWindow::SetPages(bool on) {
    if (shelf_pages == on) {
        return;
    }
    const int from = shelf_pages ? 2 : shelf_stats ? 1 : 0;
    const int to = on ? 2 : 0;
    tab_dir = to >= from ? 1 : -1;
    if (on && shelf_stats) {
        shelf_stats = false;
    }
    shelf_pages = on;
    pages_rows = false; // the sections at the left have the keys first
    find_text = QString();
    const bool back_to_game = !on && static_cast<bool>(pages_back);
    if (back_to_game) {
        // They stood over a game: back to it.
        const std::function<void()> back = std::move(pages_back);
        pages_back = nullptr;
        back();
    }
    if (on) {
        // On the page it was on, if that is a settings section; else the first of the settings.
        const QList<Item> items = Items();
        const int cur = std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1);
        if (!pages_visited || items[cur].kind == Item::System || items[cur].kind == Item::Statistics) {
            pages_visited = true;
            axis_choice = static_cast<int>(items.size()) - 1;
            for (int i = 0; i < items.size(); ++i) {
                if (items[i].kind == Item::Settings) {
                    axis_choice = i;
                    break;
                }
            }
        }
    }
    sounds.Play(back_to_game ? Sound::Back : Sound::Tab);
    update();
}

void HomeWindow::SetStats(bool on) {
    if (on && !show_stats_tab) {
        return;
    }
    if (shelf_stats == on) {
        return;
    }
    const int from = shelf_pages ? 2 : shelf_stats ? 1 : 0;
    const int to = on ? 1 : 0;
    tab_dir = to >= from ? 1 : -1;
    if (on) {
        shelf_stage = false;
        shelf_log = false;
        shelf_sorting = false;
        if (shelf_pages) {
            shelf_pages = false;
            pages_rows = false;
            find_text = QString();
        }
    }
    shelf_stats = on;
    sounds.Play(Sound::Tab);
    update();
}

void HomeWindow::OpenPages(std::function<void()> back) {
    if (!shelf_on) {
        SetShelf(true, false);
    }
    // Out of whatever else the shelf showed (a title's stage, its Log, the wizard).
    wizard_on = false;
    pages_back = std::move(back);
    if (shelf_pages) {
        update();
    } else {
        SetPages(true);
    }
}

QList<int> HomeWindow::FoundSettings(const QString& text) const {
    QList<int> starts, holds;
    const QString wanted = text.trimmed();
    for (int i = 0; i < SettingCount && !wanted.isEmpty(); ++i) {
        const SettingRow& row = SettingRows[i];
        const QString name = QString::fromLatin1(row.name);
        if (name == QStringLiteral("More") || qstrcmp(row.section, "Save menu") == 0) {
            continue;
        }
        // Its name's words first, then anywhere in its name, its page's or what is said of it.
        bool word = false;
        // What it is called on the page, and what it is in the table.
        const QString called = OwnLabel(QString::fromLatin1(row.section), name);
        for (const QString& each : (called + QLatin1Char(' ') + name).split(QLatin1Char(' '))) {
            word = word || each.startsWith(wanted, Qt::CaseInsensitive);
        }
        if (word) {
            starts.append(i);
        } else if ((called + QLatin1Char(' ') + name + QLatin1Char(' ') + QLatin1String(row.section) + QLatin1Char(' ') + QString::fromUtf8(row.about))
                       .contains(wanted, Qt::CaseInsensitive)) {
            holds.append(i);
        }
    }
    return starts + holds;
}

void HomeWindow::GoToSetting(int index) {
    pages_rows = true; // a setting found is a row to work
    const QString section = QString::fromLatin1(SettingRows[index].section);
    if (SettingRows[index].more) {
        more_open.insert(section);
    }
    const QList<Item> items = Items();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::Settings && items[i].system == section) {
            axis_choice = i;
            const QList<Row> rows = Rows(items[i]);
            for (int row = 0; row < rows.size(); ++row) {
                if (rows[row].setting == index) {
                    RowChoice(items[i]) = row;
                }
            }
        }
    }
    find_text = QString();
    update();
}

bool HomeWindow::ShelfKey(int key) {
    if (wizard_on && WizardKey(key)) {
        return true;
    }
    if (shelf_sorting && !shelf_pages && !shelf_stage) {
        // The sort line at the foot: along it the keys, Enter adds one or turns its way, Backspace
        // takes it off, up and down move it earlier or later among those in use.
        const QList<int> shown = SortShown(shelf_sort);
        sort_choice = std::clamp(sort_choice, 0, SortKeyCount - 1);
        const int key_at = shown[sort_choice];
        const int used = static_cast<int>(std::find_if(shelf_sort.begin(), shelf_sort.end(), [&](const ShelfSortKey& k) { return k.key == key_at; }) - shelf_sort.begin());
        switch (key) {
        case Qt::Key_Left:
        case Qt::Key_Right:
            sort_choice = std::clamp(sort_choice + (key == Qt::Key_Left ? -1 : 1), 0, SortKeyCount - 1);
            sounds.Play(Sound::Move);
            update();
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            SortPress(key_at, false);
            return true;
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
            SortPress(key_at, true);
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down: {
            const int to = used + (key == Qt::Key_Up ? -1 : 1);
            if (used < shelf_sort.size() && to >= 0 && to < shelf_sort.size()) {
                shelf_sort.swapItemsAt(used, to);
                sort_choice = to;
                SortSave();
                shelf_stale = true;
                sounds.Play(Sound::Move);
                update();
            }
            return true;
        }
        case Qt::Key_Escape:
        case Qt::Key_S:
            shelf_sorting = false;
            sounds.Play(Sound::Back);
            update();
            return true;
        default:
            break; // the rest are the shelf's as ever
        }
    }
    if (key == Qt::Key_F2 && !wizard_on) {
        // The settings are these pages, from anywhere on the shelf.
        if (shelf_pages) {
            SetPages(false);
        } else {
            OpenPages();
        }
        return true;
    }
    if (shelf_pages && !find_text.isNull()) {
        // A setting looked for by its name: letters are typed, the arrows choose among the found.
        const QList<int> found = FoundSettings(find_text);
        if (key == Qt::Key_Escape) {
            find_text = QString();
            sounds.Play(Sound::Back);
        } else if (key == Qt::Key_Backspace) {
            find_text.chop(1);
            find_choice = 0;
        } else if (key == Qt::Key_Up || key == Qt::Key_Down) {
            find_choice = std::clamp(find_choice + (key == Qt::Key_Up ? -1 : 1), 0, std::max(0, static_cast<int>(found.size()) - 1));
            sounds.Play(Sound::Move);
        } else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            if (find_choice < found.size()) {
                sounds.Play(Sound::Select);
                GoToSetting(found[find_choice]);
            }
        } else if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9) || key == Qt::Key_Space) {
            find_text += QChar(key).toLower();
            find_choice = 0;
        }
        update();
        return true;
    }
    if (shelf_pages && pages_rows && ((DictionariesShown() && dictionary_panel) || (ScriptsShown() && scripts_panel))) {
        // The dictionaries' or game scripts' list has the keys: what it does not take is the
        // page's (along the sections, F2, F5); back with nothing marked goes out to the sections.
        if (DictionariesShown() ? dictionary_panel->Key(key) : scripts_panel->Key(key)) {
            update();
            return true;
        }
        if (key == Qt::Key_Escape) {
            pages_rows = false;
            sounds.Play(Sound::Back);
            update();
            return true;
        }
        if (key != Qt::Key_Q && key != Qt::Key_E && key != Qt::Key_PageUp && key != Qt::Key_PageDown && key != Qt::Key_Tab &&
            key != Qt::Key_F2 && key != Qt::Key_F5 && key != Qt::Key_F11) {
            return true;
        }
    }
    if (shelf_stats) {
        // Statistics dashboard: Q/E (L1/R1) switch top tabs, Up/Down move focus between
        // Arc Range (0), Daily Goal (1), and Yearly Goal (2). Left/Right or Enter change the range;
        // on a goal Left/Right go to the other goal and Enter opens its soroban, which then has
        // the keys until it is closed (GoalKey).
        if (GoalKey(key)) {
            return true;
        }
        switch (key) {
        case Qt::Key_Escape:
        case Qt::Key_Backspace:
            SetStats(false);
            return true;
        case Qt::Key_Q:
        case Qt::Key_PageUp:
            SetStats(false); // back to Library
            return true;
        case Qt::Key_E:
        case Qt::Key_PageDown:
        case Qt::Key_Tab:
            SetPages(true); // forward to Settings
            return true;
        case Qt::Key_Up:
            stats_focus = (stats_focus + 2) % 3;
            sounds.Play(Sound::Move);
            update();
            return true;
        case Qt::Key_Down:
            stats_focus = (stats_focus + 1) % 3;
            sounds.Play(Sound::Move);
            update();
            return true;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space: {
            const int dir = key == Qt::Key_Left ? -1 : 1;
            if (stats_focus == 0) {
                stats_range = (stats_range + dir + 4) % 4;
            } else if (key == Qt::Key_Left || key == Qt::Key_Right) {
                stats_focus = stats_focus == 1 ? 2 : 1; // the two goals stand side by side
            } else {
                OpenGoal(stats_focus); // confirm opens the goal's soroban
                return true;
            }
            sounds.Play(Sound::Move);
            update();
            return true;
        }
        case Qt::Key_F4: {
            const int at = static_cast<int>(shelf_themes.indexOf(shelf_look->key));
            SetShelfTheme(shelf_themes.value((at + 1) % std::max<int>(1, shelf_themes.size())), true);
            sounds.Play(Sound::Tab);
            return true;
        }
        default:
            return true;
        }
    }
    if (shelf_pages) {
        // Two levels, as on Android (the maintainer, 2026-11-20): the sections at the left first,
        // up and down going from one to the next; confirm (Enter, right, the controller's
        // confirm) goes into its rows, back (Esc, the controller's back) comes out to the
        // sections again, and back on the sections leaves the pages.
        if (!pages_rows && !renaming) {
            switch (key) {
            case Qt::Key_Up:
            case Qt::Key_Down: {
                // Move along the Settings left rail (skipping System and Statistics).
                const QList<Item> all = Items();
                QList<int> secs;
                for (int i = 0; i < all.size(); ++i) {
                    if (all[i].kind != Item::System && all[i].kind != Item::Statistics) {
                        secs.append(i);
                    }
                }
                if (!secs.isEmpty()) {
                    const int count = static_cast<int>(secs.size());
                    const int at = std::max<int>(0, secs.indexOf(axis_choice));
                    axis_choice = secs[(at + (key == Qt::Key_Up ? count - 1 : 1)) % count];
                    sounds.Play(Sound::Move);
                    Changed();
                    update();
                }
                return true;
            }
            case Qt::Key_Q:
            case Qt::Key_PageUp:
                if (!pages_back) {
                    if (show_stats_tab) {
                        SetStats(true);
                    } else {
                        SetPages(false);
                    }
                    return true;
                }
                break;
            case Qt::Key_E:
            case Qt::Key_PageDown:
            case Qt::Key_Tab:
                if (!pages_back) {
                    SetPages(false); // wrap around to Library
                    return true;
                }
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Right:
            case Qt::Key_Space:
                pages_rows = true;
                sounds.Play(Sound::Select);
                update();
                return true;
            case Qt::Key_Escape:
                SetPages(false);
                return true;
            case Qt::Key_Left:
            case Qt::Key_Backspace:
                return true; // nothing at the left of the sections, and nothing to reset there
            default:
                break;
            }
        } else if (key == Qt::Key_Escape) {
            pages_rows = false;
            sounds.Play(Sound::Back);
            update();
            return true;
        }
        if (key == Qt::Key_Slash || key == Qt::Key_F) {
            find_text = QLatin1String("");
            find_choice = 0;
            sounds.Play(Sound::Select);
            update();
            return true;
        }
        const QList<Item> now = Items();
        const Item& page = now[std::clamp(axis_choice, 0, static_cast<int>(now.size()) - 1)];
        const QList<Row> page_rows = Rows(page);
        const int page_row = RowChoice(page);
        const bool look_row = page.kind == Item::Settings && page_row < page_rows.size() && page_rows[page_row].setting >= 0 &&
                              IsSetting(page_rows[page_row].setting, "Look");
        if (look_row && !renaming && (key == Qt::Key_Return || key == Qt::Key_Enter) && OpenPick(page_row)) {
            return true; // every theme at once (choice_picker.h)
        }
        if (look_row && !(renaming && key != Qt::Key_Left && key != Qt::Key_Right) && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Return || key == Qt::Key_Enter)) {
            // Look: this page's own themes, not the save menu's.
            const int count = std::max<int>(1, shelf_themes.size());
            const int at = static_cast<int>(shelf_themes.indexOf(shelf_look->key));
            SetShelfTheme(shelf_themes.value((at + (key == Qt::Key_Left ? count - 1 : 1)) % count), true);
            sounds.Play(Sound::Move);
            return true;
        }
        if (renaming && (key == Qt::Key_Return || key == Qt::Key_Enter) && page_row < page_rows.size() &&
            !(page.kind == Item::Settings && page.system == QStringLiteral("Developer"))) {
            Rename(page.kind == Item::Settings || page.kind == Item::Dictionaries || page.kind == Item::Scripts ? page.system : page.kind == Item::Consoles ? QStringLiteral("Consoles") : QStringLiteral("Statistics"),
                   page_rows[page_row].title);
            return true;
        }
        if (key == Qt::Key_Q || key == Qt::Key_E || key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Tab) {
            // Along the pages, round from the last to the first and back.
            QList<int> pages;
            for (int i = 0; i < now.size(); ++i) {
                if (now[i].kind != Item::System) {
                    pages.append(i);
                }
            }
            const int count = static_cast<int>(pages.size());
            const int at = std::max<int>(0, pages.indexOf(axis_choice));
            axis_choice = pages[(at + (key == Qt::Key_Q || key == Qt::Key_PageUp ? count - 1 : 1)) % count];
            sounds.Play(Sound::Tab);
            Changed();
            update();
            return true;
        }
        if (key != Qt::Key_F4) {
            // The rows are the save menu's own, and so is what a key does to them; only where
            // it would go along to a console's games it comes round to the pages again.
            shelf_passing = true;
            Key(key);
            shelf_passing = false;
            const QList<Item> items = Items();
            if (items[std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1)].kind == Item::System) {
                const bool back = key == Qt::Key_Left || key == Qt::Key_Q || key == Qt::Key_PageUp;
                axis_choice = static_cast<int>(items.size()) - 1;
                for (int i = 0; !back && i < items.size(); ++i) {
                    if (items[i].kind != Item::System) {
                        axis_choice = i;
                        break;
                    }
                }
            }
            update();
            return true;
        }
    }
    if (shelf_log) {
        const QList<ShelfTitle>& titles = ShelfTitles();
        const int count = shelf_choice < titles.size() ? static_cast<int>(ReadingOf(titles[shelf_choice].game.id).lines.size()) : 0;
        const int at = log_line < 0 ? count - 1 : log_line;
        const auto go = [&](int to) {
            const int next = std::clamp(to, 0, std::max(0, count - 1));
            if (next != at) {
                log_line = next;
                sounds.Play(Sound::Move);
                update();
            }
        };
        switch (key) {
        case Qt::Key_Up:
            go(at - 1);
            return true;
        case Qt::Key_Down:
            go(at + 1);
            return true;
        case Qt::Key_PageUp:
        case Qt::Key_Q:
            go(at - 8);
            return true;
        case Qt::Key_PageDown:
        case Qt::Key_E:
            go(at + 8);
            return true;
        case Qt::Key_Home:
            go(0);
            return true;
        case Qt::Key_End:
            go(count - 1);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (at >= 0 && at < count) {
                QGuiApplication::clipboard()->setText(ReadingOf(titles[shelf_choice].game.id).lines[at].text);
                sounds.Play(Sound::Select);
                Say(QStringLiteral("Copied"));
            }
            return true;
        case Qt::Key_Escape:
        case Qt::Key_Backspace:
        case Qt::Key_Left:
            SetLog(false);
            return true;
        case Qt::Key_F4:
            break;
        default:
            return false;
        }
    }
    switch (key) {
    case Qt::Key_Left:
        if (!shelf_stage && shelf_layout == 1) {
            SetShelfSection(shelf_section - 1);
        } else {
            ShelfMove(-1);
        }
        return true;
    case Qt::Key_Right:
        if (!shelf_stage && shelf_layout == 1) {
            SetShelfSection(shelf_section + 1);
        } else {
            ShelfMove(1);
        }
        return true;
    case Qt::Key_Home:
        ShelfMove(-100000);
        return true;
    case Qt::Key_End:
        ShelfMove(100000);
        return true;
    case Qt::Key_Down:
        if (shelf_stage) {
            SetLog(true); // further in: what was read of it
        } else if (shelf_layout == 1) {
            ShelfMove(1);
        } else if (shelf_section < ShelfSections().size()) {
            SetShelfSection(shelf_section + 1); // the shelf below
        } else {
            sounds.Play(Sound::Refuse);
        }
        return true;
    case Qt::Key_Up:
        if (shelf_stage) {
            SetStage(false);
        } else if (shelf_layout == 1) {
            ShelfMove(-1);
        } else if (shelf_section > 0) {
            SetShelfSection(shelf_section - 1); // the shelf above
        } else {
            SetPages(true); // above every shelf: the settings
        }
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        if (shelf_stage) {
            SetStage(false);
            return true;
        }
        if (key == Qt::Key_Escape && !pages_back) {
            OpenChoices(QStringLiteral("Quit Furoshiki?"), {QStringLiteral("Cancel"), QStringLiteral("Quit Furoshiki")}, 0, [this](int idx) {
                if (idx == 1) {
                    qInfo("quitting: Quit Furoshiki from Library");
                    window()->close();
                }
            });
            return true;
        }
        return false;
    case Qt::Key_Q:
    case Qt::Key_PageUp:
        if (!shelf_stage) {
            SetPages(true); // wrap left to Settings
        } else {
            SetShelfSection(shelf_section - 1);
        }
        return true;
    case Qt::Key_E:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
        if (!shelf_stage) {
            if (show_stats_tab) {
                SetStats(true);
            } else {
                SetPages(true);
            }
        } else {
            SetShelfSection(shelf_section + 1);
        }
        return true;
    case Qt::Key_F:
        if (shelf_stage) {
            FindArt(); // the title in hand's box art, where it lacks some
        }
        return true;
    case Qt::Key_S:
        if (!shelf_stage) {
            shelf_sorting = !shelf_sorting;
            sounds.Play(shelf_sorting ? Sound::Select : Sound::Back);
            update();
        }
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        // On the shelf it opens the title (or adds one, on the last spine); on its stage it plays.
        if (shelf_stage || shelf_choice >= ShelfTitles().size()) {
            ShelfPlay();
        } else {
            SetStage(true);
        }
        return true;
    case Qt::Key_F4: {
        const int at = static_cast<int>(shelf_themes.indexOf(shelf_look->key));
        SetShelfTheme(shelf_themes.value((at + 1) % std::max<int>(1, shelf_themes.size())), true);
        sounds.Play(Sound::Tab);
        return true;
    }
    default:
        return false; // F2, F5, F11, Escape: the window's own, as on the save menu
    }
}

bool HomeWindow::ShelfClick(const QPointF& at) {
    if (wizard_on) {
        return WizardPress(at);
    }
    // What is drawn last lies on top: its hit is asked first.
    const QList<Hit> hits(shelf_hits.crbegin(), shelf_hits.crend());
    if (shelf_stats && goal_open != 0) {
        // The open soroban: a bead pushes itself and those before it to the bar, a rod is taken
        // in hand, and a click anywhere else keeps what the beads say.
        const GoalAbacus::Kind& kind = goal_open == 1 ? GoalAbacus::Daily : GoalAbacus::Yearly;
        for (const Hit& hit : hits) {
            if (!hit.area.contains(at) || (hit.kind != 24 && hit.kind != 25)) {
                continue;
            }
            if (hit.kind == 25) {
                goal_rod = std::clamp(hit.index, 0, 2);
            } else {
                const int rod = std::clamp(hit.index / 8, 0, 2), slot = hit.index % 8;
                const int digit = GoalAbacus::Digits(kind, goal_draft)[static_cast<std::size_t>(rod)];
                const bool five = kind.rods[static_cast<std::size_t>(rod)].heaven && digit >= 5;
                const int ones = kind.rods[static_cast<std::size_t>(rod)].heaven ? digit % 5 : digit;
                goal_rod = rod;
                if (slot == 0) {
                    goal_draft = GoalAbacus::WithDigit(kind, goal_draft, rod, five ? digit - 5 : digit + 5);
                } else {
                    goal_draft = GoalAbacus::WithDigit(kind, goal_draft, rod, (five ? 5 : 0) + (slot - 1 < ones ? slot - 1 : slot));
                }
            }
            sounds.Play(Sound::Move);
            update();
            return true;
        }
        CloseGoal(true);
        return true;
    }
    for (const Hit& hit : hits) {
        if (hit.area.contains(at)) {
            if (hit.kind == 10 && hit.index < 0) {
                SetPages(true);
            } else if (hit.kind == 10) {
                if (hit.index != shelf_section) {
                    SetShelfSection(hit.index);
                }
            } else if (hit.kind == 11) {
                Key(hit.index); // a key along the foot, as if pressed
            } else if (hit.kind == 12 || hit.kind == 14) {
                SortPress(hit.index, hit.kind == 14);
            } else if (hit.kind == 13) {
                ShelfKey(Qt::Key_S);
            } else if (hit.kind == 5 && renaming && hit.index == axis_choice) {
                Rename(QStringLiteral("Page"), Items()[hit.index].label);
            } else if (hit.kind == 5) {
                axis_choice = hit.index;
                pages_rows = false; // the section shown, its rows not yet entered
                sounds.Play(Sound::Tab);
                update();
            } else if (hit.kind == 6 || hit.kind == 15 || hit.kind == 16) {
                // A click takes a row at once, as Enter does on the row in hand; on the arrows
                // beside its value, as left and right do.
                const QList<Item> items = Items();
                RowChoice(items[axis_choice]) = hit.index;
                pages_seen_row = hit.index; // chosen where it stands: the list does not jump to it
                pages_rows = true;          // a click on a row goes into the rows
                ShelfKey(hit.kind == 6 ? Qt::Key_Return : hit.kind == 15 ? Qt::Key_Left : Qt::Key_Right);
                update();
            } else if (hit.kind == 17 || hit.kind == 18) {
                pages_drag = hit.kind == 17 ? 1 : 2;
                pages_drag_area = hit.area.adjusted(hit.kind == 17 ? 0 : 0, hit.kind == 17 ? 0 : 0, 0, 0);
                PagesDrag(at);
            } else if (hit.kind == 20) {
                if (hit.index == 0) {
                    SetStats(false);
                    SetPages(false);
                } else if (hit.index == 1) {
                    SetStats(true);
                } else if (hit.index == 2) {
                    SetPages(true);
                }
            } else if (hit.kind == 21) {
                stats_focus = 0;
                stats_range = std::clamp(hit.index, 0, 3);
                sounds.Play(Sound::Move);
                update();
            } else if (hit.kind == 22) {
                OpenGoal(hit.index == 1 ? 1 : 2);
            } else if (hit.kind == 23) {
                OpenChoices(QStringLiteral("Quit Furoshiki?"), {QStringLiteral("Cancel"), QStringLiteral("Quit Furoshiki")}, 0, [this](int idx) {
                    if (idx == 1) {
                        qInfo("quitting: Quit Furoshiki from top bar");
                        window()->close();
                    }
                });
            } else if (hit.kind == 24) {
                SetLog(true);
            } else if (hit.kind == 19) {
                GoToSetting(hit.index);
                sounds.Play(Sound::Select);
            } else if (hit.kind == 8) {
                SetLog(false);
            } else if (hit.kind == 9) {
                log_line = hit.index;
                update();
            } else if (hit.kind == 7) {
                SetPages(false);
            } else if (hit.kind == 4) {
                SetStage(false);
            } else if (hit.kind == 3) {
                ShelfPlay();
            } else if (hit.index == shelf_choice) {
                ShelfKey(Qt::Key_Return);
            } else {
                ShelfMove(hit.index - shelf_choice);
            }
            return true;
        }
    }
    return false;
}

void HomeWindow::ShelfStep() {
    const QString minute = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    if (tracker_asked == 0 || clock.elapsed() - tracker_asked > 5 * 60 * 1000) {
        TrackerAsk();
    }
    WizardStep();
    if (std::fabs(pages_at - (shelf_pages ? 1 : 0)) > 0.001 || std::fabs(stats_at - (shelf_stats ? 1 : 0)) > 0.001 ||
        std::fabs(log_at - (shelf_log ? 1 : 0)) > 0.001 || std::fabs(wizard_at - (wizard_on ? 1 : 0)) > 0.001) {
        update();
    }
    if (shelf_busy || goal_open != 0 || clock.elapsed() - goal_kept_ms < 700) {
        update();
    }
    if (dots_on && !SystemWantsStillness() && !playing_game && running.isEmpty() && isVisible() && !isMinimized()) {
        const qint64 now_ms = clock.elapsed();
        if (now_ms - dots_last_ms >= 32) { // ~30 fps cap to keep PC CPU usage negligible
            dots_last_ms = now_ms;
            update();
        }
    }
    bool lifting = false;
    for (auto lift = spine_lift.cbegin(); lift != spine_lift.cend(); ++lift) {
        const double to = ShelfHovered(1, lift.key()) && lift.key() != shelf_choice && !shelf_stage ? 1 : 0;
        lifting = lifting || std::fabs(lift.value() - to) > 0.002;
    }
    const bool moving = std::fabs(shelf_at - shelf_choice) > 0.001 || std::fabs(stage_at - (shelf_stage ? 1 : 0)) > 0.001 || lifting ||
                        lift_t < 0.999 || std::fabs(floor_at - shelf_section) > 0.001;
    if ((minute != shelf_minute || moving) && isVisible() && !isMinimized()) {
        update();
    }
}

void HomeWindow::SetStage(bool on) {
    if (on && shelf_choice >= ShelfTitles().size()) {
        return; // the spine that adds a title has no stage
    }
    if (shelf_stage != on) {
        shelf_stage = on;
        shelf_log = false;
        sounds.Play(on ? Sound::Select : Sound::Back);
        update();
    }
}

void HomeWindow::PaintShelf() {
    const ShelfLook& l = *shelf_look;
    ShelfTitles();
    // The open spine goes to the chosen one, and the stage comes and goes, by the clock.
    // After the page stood still, the first frame of a move is one frame on, not the time since
    // the last frame: else that frame ate a tenth of a second of every glide's start.
    const double since = shelf_clock.isValid() ? shelf_clock.nsecsElapsed() / 1e9 : 0;
    const double dt = since > 0.05 ? 1.0 / 60 : since;
    shelf_clock.restart();
    const bool still = SystemWantsStillness();
    const auto glide = [&](double& at, double to, double time) {
        at = still || std::fabs(to - at) < 0.002 ? to : at + (to - at) * (1 - std::exp(-dt / time));
    };
    glide(shelf_at, shelf_choice, ShelfGlide);
    glide(stage_at, shelf_stage ? 1 : 0, 0.085);
    glide(pages_at, shelf_pages ? 1 : 0, 0.085);
    glide(stats_at, shelf_stats ? 1 : 0, 0.085);
    glide(log_at, shelf_log ? 1 : 0, 0.085);
    if (dots_on && !still) {
        dots_time += dt * 0.8;
    }
    glide(wizard_at, wizard_on ? 1 : 0, 0.085);
    glide(lift_t, 1, 0.075);
    glide(floor_at, shelf_section, 0.06);
    // What comes and goes softly under the pointer; what was not drawn last time goes.
    shelf_busy = false;
    for (auto one = eased.begin(); one != eased.end();) {
        const double to = one->to && one->seen == ease_frame ? 1 : 0;
        glide(one->at, to, 0.045);
        shelf_busy = shelf_busy || one->at != to;
        one = one->at == 0 && to == 0 ? eased.erase(one) : std::next(one);
    }
    ++ease_frame;
    glide(pages_first_at, pages_first, 0.05);
    glide(page_t, 1, 0.07);
    glide(wizard_t, 1, 0.07);
    shelf_busy = shelf_busy || pages_first_at != pages_first || page_t != 1 || wizard_t != 1;
    shelf_dt = dt;
    shelf_still = still;
    if (lift_t >= 1) {
        lift_old = QImage();
    }
    // The lift: what leaves goes most of its way in the first part, what comes eases in after it.
    const double leave = std::clamp(lift_t / 0.45, 0.0, 1.0);
    const double come = std::clamp((lift_t - 0.2) / 0.8, 0.0, 1.0);
    shelf_shift = lift_old.isNull() ? 0 : 64 * lift_dir * (1 - come) * (1 - come);
    shelf_fade = lift_old.isNull() ? 1 : come;
    // A closed spine under the pointer is drawn a little out of the shelf, as a finger would.
    if (shelf_hover_kind == 1 && !spine_lift.contains(shelf_hover_index)) {
        spine_lift.insert(shelf_hover_index, 0);
    }
    for (auto lift = spine_lift.begin(); lift != spine_lift.end();) {
        glide(lift.value(), ShelfHovered(1, lift.key()) && lift.key() != shelf_choice && !shelf_stage ? 1 : 0, 0.07);
        lift = lift.value() == 0 && !ShelfHovered(1, lift.key()) ? spine_lift.erase(lift) : std::next(lift);
    }

    QPainter p(this);
    p.fillRect(rect(), l.ground);
    const double fit = std::min(width() / SheetW, height() / SheetH);
    p.translate((width() - SheetW * fit) / 2, (height() - SheetH * fit) / 2);
    p.scale(fit, fit);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    if (dots_on) {
        PaintDotMatrix(p, stats_at > 0.35);
    }
    shelf_minute = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    shelf_hits.clear();
    // The shelf goes up and out as the stage comes up into its place; top-level tabs
    // (Library, Statistics, Settings) slide horizontally left and right.
    // The wizard that adds a title comes over whichever of them it was opened from.
    const double behind = 1 - wizard_at;
    const double over = std::min(1.0, pages_at + stats_at);
    const double under = (1 - over) * behind;
    const double tab_slide = 56.0;
    const bool lib_target = !shelf_pages && !shelf_stats;
    const double lib_dx = (lib_target ? 1.0 : -1.0) * tab_dir * tab_slide * over;
    if (stage_at < 0.999 && under > 0.001) {
        p.save();
        p.setOpacity((1 - stage_at) * under);
        p.translate(0, -14 * stage_at);
        tab_body_dx = lib_dx;
        if (!lift_old.isNull() && leave < 1) {
            p.save();
            p.setClipRect(QRectF(0, 66, ShelfRight - FloorColumn + 1, 578));
            p.setOpacity(p.opacity() * (1 - leave));
            p.translate(tab_body_dx, -64 * lift_dir * leave * (2 - leave));
            p.drawImage(QRectF(0, 0, SheetW, SheetH), lift_old, QRectF(0, 0, lift_old.width(), lift_old.height()));
            p.restore();
        }
        PaintShelfBody(p);
        tab_body_dx = 0;
        p.restore();
    }
    if (stage_at > 0.001 && under > 0.001 && log_at < 0.999) {
        if (stage_at > 0.5) {
            shelf_hits.clear(); // the stage's own
        }
        p.save();
        p.setOpacity(stage_at * under * (1 - log_at));
        p.translate(lib_dx, 18 * (1 - stage_at) - 14 * log_at);
        PaintStage(p);
        p.restore();
    }
    if (log_at > 0.001 && under > 0.001) {
        if (log_at > 0.5) {
            shelf_hits.clear();
        }
        p.save();
        p.setOpacity(log_at * under);
        p.translate(lib_dx, 18 * (1 - log_at));
        PaintLog(p);
        p.restore();
    }
    if (pages_at > 0.001 && behind > 0.001) {
        if (pages_at > 0.5) {
            shelf_hits.clear();
        }
        p.save();
        p.setOpacity(pages_at * behind);
        tab_body_dx = (shelf_pages ? 1.0 : -1.0) * tab_dir * tab_slide * (1 - pages_at);
        PaintPages(p);
        tab_body_dx = 0;
        p.restore();
    }
    if (stats_at > 0.001 && behind > 0.001) {
        if (stats_at > 0.5) {
            shelf_hits.clear();
        }
        p.save();
        p.setOpacity(stats_at * behind);
        tab_body_dx = (shelf_stats ? 1.0 : -1.0) * tab_dir * tab_slide * (1 - stats_at);
        PaintStats(p);
        tab_body_dx = 0;
        p.restore();
    }
    // Pinned top header across Library / Statistics / Settings (never slides horizontally).
    const double header_alpha = behind * (1.0 - stage_at * (1.0 - over));
    if (header_alpha > 0.001) {
        const int active_tab = shelf_pages ? 2 : shelf_stats ? 1 : 0;
        QString right_note;
        if (active_tab == 0) {
            const int count = static_cast<int>(ShelfTitles().size());
            right_note = QStringLiteral("%1 %2 · ").arg(count).arg(count == 1 ? QStringLiteral("GAME") : QStringLiteral("GAMES"));
        } else if (active_tab == 2 && renaming) {
            right_note = QStringLiteral("RENAMING · CLICK A ROW OR PRESS ENTER · ");
        }
        p.save();
        p.setOpacity(header_alpha);
        PaintTopTabs(p, active_tab, right_note);
        p.restore();
    }
    if (wizard_at > 0.001 && wizard) {
        if (wizard_at > 0.5) {
            shelf_hits.clear();
        }
        p.save();
        p.setOpacity(wizard_at);
        p.translate(0, 18 * (1 - wizard_at));
        PaintWizard(p);
        p.restore();
    }
}

void HomeWindow::PaintDotMatrix(QPainter& p, bool clear_stats_band) {
    const ShelfLook& l = *shelf_look;
    const bool white_dots = l.key == QStringLiteral("mono") || l.key == QStringLiteral("nocturne");
    const QColor dot_rgb = white_dots ? l.ink : l.accent;
    const double base_bright = l.key == QStringLiteral("nocturne") ? 0.84 : (l.ground.lightness() > 140 ? 0.36 : 0.48);
    const double t = dots_time;
    const double step = 22.0;
    const double v1x = SheetW * (0.30 + 0.18 * std::sin(t * 0.36));
    const double v1y = SheetH * (0.48 + 0.18 * std::cos(t * 0.29));
    const double v2x = SheetW * (0.72 + 0.15 * std::cos(t * 0.41));
    const double v2y = SheetH * (0.52 + 0.20 * std::sin(t * 0.33));
    const double clear_top = 274.0, clear_bot = 496.0, feather = 36.0;

    p.save();
    p.setPen(Qt::NoPen);
    for (double y = step * 0.5; y < SheetH; y += step) {
        double mask = 1.0;
        if (clear_stats_band) {
            if (y >= clear_top && y <= clear_bot) {
                continue;
            }
            if (y > clear_top - feather && y < clear_top) {
                mask = (clear_top - y) / feather;
            } else if (y > clear_bot && y < clear_bot + feather) {
                mask = (y - clear_bot) / feather;
            }
        }
        for (double x = step * 0.5; x < SheetW; x += step) {
            double val = 0.0;
            if (dots_mode == 1) { // Vortex Currents
                const double a1 = std::atan2(y - v1y, x - v1x), r1 = std::hypot(x - v1x, y - v1y);
                const double a2 = std::atan2(y - v2y, x - v2x), r2 = std::hypot(x - v2x, y - v2y);
                const double swirl = std::sin(a1 * 3.0 + r1 * 0.022 - t * 1.7) * 0.5 +
                                     std::cos(a2 * 2.0 - r2 * 0.018 - t * 1.3) * 0.5;
                const double turb = DotFbm(x / (SheetW * 0.24) + t * 0.25, y / (SheetH * 0.24) - t * 0.20);
                val = std::clamp(((swirl + 1.0) * 0.5) * 0.62 + turb * 0.52 - 0.18, 0.0, 1.0);
            } else if (dots_mode == 2) { // Horizon Pulse
                const double d = std::hypot(x - SheetW * 0.5, (y - SheetH * 0.5) * 1.35);
                const double ring = std::sin(d * 0.022 - t * 1.1) * 0.5 + 0.5;
                const double n = DotNoise(x / (SheetW * 0.28) + t * 0.08, y / (SheetH * 0.28));
                val = std::clamp(ring * 0.55 + n * 0.55 - 0.16, 0.0, 1.0);
            } else { // Gentle Drift
                const double w1 = std::sin(x * 0.0085 + y * 0.0055 - t * 0.85) * 0.5 + 0.5;
                const double n = DotFbm(x / (SheetW * 0.30) + t * 0.12, y / (SheetH * 0.30) - t * 0.09);
                val = std::clamp(w1 * 0.42 + n * 0.68 - 0.15, 0.0, 1.0);
            }
            const double alpha = std::min(0.90, (0.04 + std::pow(val, 1.9) * 0.85) * base_bright) * mask;
            if (alpha < 0.012) {
                continue;
            }
            const double rad = 0.8 + std::pow(val, 1.7) * 2.3;
            p.setBrush(Faded(dot_rgb, alpha));
            p.drawEllipse(QPointF(x, y), rad, rad);
        }
    }
    p.restore();
}

void HomeWindow::PaintTopTabs(QPainter& p, int active_tab, const QString& right_note) {
    const ShelfLook& l = *shelf_look;
    const QFont tab_face = ShelfFont(l.display, 11, 0.15);
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const bool light_ground = l.ground.lightness() > 140;

    double x = ShelfLeft;
    if (active_tab == 2 && pages_back) {
        const double w = Put(p, small, x, 49, QStringLiteral("‹  BACK TO GAME"), Mix(l.muted, l.ink, Ease(7, 0, ShelfHovered(7, 0))));
        shelf_hits.append({QRectF(x - 4, 28, w + 12, 34), 7, 0});
    } else {
        struct TabDef {
            int id;
            QString label;
        };
        QList<TabDef> tabs{{0, QStringLiteral("LIBRARY")}};
        if (show_stats_tab) {
            tabs.append({1, QStringLiteral("STATISTICS")});
        }
        tabs.append({2, QStringLiteral("SETTINGS")});
        for (const TabDef& tab : tabs) {
            const bool on = tab.id == active_tab;
            const double lit = Ease(20, tab.id, ShelfHovered(20, tab.id));
            const double w = on ? PutHalo(p, tab_face, x, 49, tab.label, l.ink, l.accent, dots_time, light_ground)
                                : Put(p, tab_face, x, 49, tab.label, Mix(l.muted, l.ink, lit));
            if (on) {
                p.fillRect(QRectF(x, 62, w, 2.5), l.accent);
            }
            shelf_hits.append({QRectF(x - 6, 26, w + 12, 38), 20, tab.id});
            x += w + 28;
        }
    }

    double right = ShelfRight;
    if (!pages_back) {
        const double qlit = Ease(23, 0, ShelfHovered(23, 0));
        const double qw = Put(p, small, right, 49, QStringLiteral("QUIT"), Mix(l.muted, l.accent, qlit), 1);
        shelf_hits.append({QRectF(right - qw - 8, 28, qw + 14, 34), 23, 0});
        right -= qw + 18;
        right -= Put(p, small, right, 49, QStringLiteral("· "), Faded(l.muted, 0.55), 1);
    }
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    if (!right_note.isEmpty()) {
        Put(p, small, right, 49, right_note, l.muted, 1);
    }
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
}

void HomeWindow::OpenGoal(int which) {
    const GoalAbacus::Kind& kind = which == 1 ? GoalAbacus::Daily : GoalAbacus::Yearly;
    stats_focus = which;
    goal_open = which;
    goal_draft = GoalAbacus::Snap(kind, which == 1 ? DailyGoal() : YearlyGoal());
    goal_rod = 1;
    goal_beads.clear();
    goal_opened_ms = goal_painted_ms = clock.elapsed();
    sounds.Play(Sound::Select);
    update();
}

void HomeWindow::CloseGoal(bool keep) {
    if (goal_open == 0) {
        return;
    }
    if (keep) {
        if (goal_open == 1) {
            SetDailyGoal(goal_draft);
        } else {
            SetYearlyGoal(goal_draft);
        }
        goal_kept = goal_open;
        goal_kept_ms = clock.elapsed();
    }
    sounds.Play(keep ? Sound::Select : Sound::Back);
    goal_open = 0;
    update();
}

bool HomeWindow::GoalKey(int key) {
    if (goal_open == 0) {
        return false;
    }
    const GoalAbacus::Kind& kind = goal_open == 1 ? GoalAbacus::Daily : GoalAbacus::Yearly;
    const auto set = [this](int to) {
        if (to != goal_draft) {
            goal_draft = to;
            sounds.Play(Sound::Move);
        }
    };
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down:
        goal_rod = std::clamp(goal_rod + (key == Qt::Key_Up ? -1 : 1), 0, 2);
        sounds.Play(Sound::Move);
        break;
    case Qt::Key_Left:
        set(GoalAbacus::Stepped(kind, goal_draft, goal_rod, -1));
        break;
    case Qt::Key_Right:
        set(GoalAbacus::Stepped(kind, goal_draft, goal_rod, 1));
        break;
    case Qt::Key_Q:
    case Qt::Key_PageUp:
        set(GoalAbacus::Round(kind, goal_draft, -1)); // the shoulders: the next round figure
        break;
    case Qt::Key_E:
    case Qt::Key_PageDown:
        set(GoalAbacus::Round(kind, goal_draft, 1));
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        CloseGoal(true);
        break;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        CloseGoal(false);
        break;
    default:
        return key < Qt::Key_F1 || key > Qt::Key_F35; // the function keys stay the page's
    }
    update();
    return true;
}

void HomeWindow::PaintGoal(QPainter& p, int which, double x, double baseline) {
    const ShelfLook& l = *shelf_look;
    const bool light_ground = l.ground.lightness() > 140;
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont mono = ShelfFont(l.mono, 11.5, 0.08);
    const bool daily = which == 1;
    const GoalAbacus::Kind& kind = daily ? GoalAbacus::Daily : GoalAbacus::Yearly;
    const qint64 now = clock.elapsed();
    if (goal_open != which) {
        // Closed: the goal in words. Confirm on it, or a click, opens the soroban in its place.
        const int goal = daily ? DailyGoal() : YearlyGoal();
        const QString text = (daily ? QStringLiteral("DAILY GOAL %1") : QStringLiteral("GOAL %1"))
                                 .arg(goal > 0 ? ShortCount(goal) : QStringLiteral("OFF"));
        const bool focused = stats_focus == which && goal_open == 0;
        const double width = focused ? PutHalo(p, tiny, x, baseline, text, l.ink, l.accent, dots_time, light_ground)
                                     : Put(p, tiny, x, baseline, text, l.muted);
        const double since = (now - goal_kept_ms) / 600.0;
        if (goal_kept == which && since < 1.0) {
            p.fillRect(QRectF(x, baseline + 4.5, width * (1.0 - since), 1.2), Faded(l.accent, 1.0 - since)); // just kept
        }
        shelf_hits.append({QRectF(x - 8, baseline - 16, std::max(width + 16.0, 80.0), 24), 22, which});
        return;
    }
    // Open: three rods lying on their sides, tens above ones above tenths. A bead counts when it
    // is against the bar; the one left of the bar is worth five. The rod in hand glows.
    const double dt = std::clamp((now - goal_painted_ms) / 1000.0, 0.0, 0.05);
    goal_painted_ms = now;
    const double ease = 1.0 - std::exp(-dt * 26.0);
    const double shown = std::clamp((now - goal_opened_ms) / 140.0, 0.0, 1.0);
    const std::array<int, 3> digits = GoalAbacus::Digits(kind, goal_draft);
    const double cy = baseline - 4, bar = x + 15;
    p.save();
    p.setOpacity(p.opacity() * shown);
    p.translate((1.0 - shown) * -6.0, 0);
    p.fillRect(QRectF(bar, cy - 11, 1, 22), Faded(l.ink, 0.55));
    for (int i = 0; i < 3; ++i) {
        const GoalAbacus::Rod& rod = kind.rods[static_cast<std::size_t>(i)];
        const double y = cy + (i - 1) * 7.0;
        const bool in_hand = i == goal_rod;
        const int digit = digits[static_cast<std::size_t>(i)];
        const bool five = rod.heaven && digit >= 5;
        const int ones = rod.heaven ? digit % 5 : digit;
        if (in_hand) {
            HaloRule(p, x, x + 48, y - 0.5, Faded(l.accent, 0.9), dots_time, light_ground);
        } else {
            p.fillRect(QRectF(x, y - 0.5, 48, 1), l.rule);
        }
        shelf_hits.append({QRectF(x - 2, y - 3.5, 52, 7), 25, i});
        const auto bead = [&](int slot, double to, bool counts) {
            const int key = which * 64 + i * 8 + slot;
            const auto found = goal_beads.find(key);
            const double at = found == goal_beads.end() ? to : *found + (to - *found) * ease;
            goal_beads[key] = at;
            if (counts && in_hand) {
                HaloDot(p, QPointF(at, y), 2.3, l.accent, dots_time, light_ground);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(counts ? l.ink : Faded(l.muted, 0.5));
                p.drawEllipse(QPointF(at, y), 2.3, 2.3);
            }
            shelf_hits.append({QRectF(to - 3, y - 3.5, 6, 7), 24, i * 8 + slot});
        };
        if (rod.heaven) {
            bead(0, five ? x + 11 : x + 4, five);
        }
        for (int j = 0; j < rod.earth; ++j) {
            const bool counts = j < ones;
            bead(j + 1, x + 19.5 + (counts ? j : j + 1) * 6.0, counts);
        }
    }
    PutHalo(p, mono, x + 57, baseline, goal_draft > 0 ? ShortCount(goal_draft) : QStringLiteral("OFF"), l.ink, l.accent, dots_time,
            light_ground);
    p.restore();
}

void HomeWindow::PaintStats(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const bool light_ground = l.ground.lightness() > 140;
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont mono = ShelfFont(l.mono, 11.5, 0.08);
    const QFont hero = ShelfFont(l.display, 27, 0.02);
    const QFont eta_face = ShelfFont(l.display, 14, 0.05);

    p.save();
    p.translate(tab_body_dx, 0);

    // Index daily reading data by ISO date.
    const QDate today = QDate::currentDate();
    QHash<QString, qint64> day_chars;
    QHash<QString, qint64> day_ms;
    qint64 year_chars = 0, year_ms = 0, recent30_chars = 0;
    int year_active = 0;
    struct PeakDay {
        QDate date;
        qint64 chars{};
        qint64 ms{};
    };
    QList<PeakDay> peaks;
    for (const Day& d : library.days) {
        day_chars[d.day] += d.chars;
        day_ms[d.day] += d.active_ms;
        const QDate dt = QDate::fromString(d.day, Qt::ISODate);
        if (dt.isValid()) {
            if (dt.year() == today.year()) {
                year_chars += d.chars;
                year_ms += d.active_ms;
                if (d.chars > 0) {
                    ++year_active;
                }
            }
            if (dt.daysTo(today) >= 0 && dt.daysTo(today) < 30) {
                recent30_chars += d.chars;
            }
            if (d.chars > 0) {
                peaks.append({dt, d.chars, d.active_ms});
            }
        }
    }
    std::sort(peaks.begin(), peaks.end(), [](const PeakDay& a, const PeakDay& b) { return a.chars > b.chars; });

    const QString today_iso = today.toString(Qt::ISODate);
    const qint64 today_chars = day_chars.value(today_iso);
    const qint64 today_ms = day_ms.value(today_iso);
    // While a goal's soroban is open the bars and percentages follow its beads.
    const int daily_goal = goal_open == 1 ? goal_draft : DailyGoal();
    const int yearly_goal = goal_open == 2 ? goal_draft : YearlyGoal();
    const qint64 eff_year_goal = yearly_goal > 0 ? yearly_goal : (daily_goal > 0 ? qint64(daily_goal) * 365 : 5000000);
    const qint64 eff_daily_goal = daily_goal > 0 ? daily_goal : 10000;
    const int streak = Streak(library.days, today_iso);
    const int active_days = std::max(1, static_cast<int>(library.days.size()));
    const qint64 avg_day_chars = library.chars / active_days;
    const double avg_day_hrs = (library.active_ms / 3600000.0) / active_days;
    const qint64 chars_per_hr = library.active_ms > 60000 ? static_cast<qint64>(library.chars / (library.active_ms / 3600000.0)) : 0;
    int titles_read = 0;
    QSet<QString> seen_titles;
    for (const Game& g : library.games) {
        if (g.chars > 0 && !seen_titles.contains(g.id)) {
            seen_titles.insert(g.id);
            ++titles_read;
        }
    }

    // ---- Band 1: 4 borderless hero telemetry pillars (y = 82..192) ----
    const double w_all = ShelfRight - ShelfLeft;
    const double col_w[4] = {w_all * 0.30, w_all * 0.27, w_all * 0.21, w_all * 0.22};
    double cx = ShelfLeft;
    for (int c = 0; c < 4; ++c) {
        if (c > 0) {
            p.fillRect(QRectF(cx, 84, 1, 96), l.rule);
        }
        const double px = c == 0 ? cx : cx + 18;
        const double pw = (c == 0 ? col_w[c] : col_w[c] - 18) - 16;
        if (c == 0) {
            const int pct = eff_year_goal > 0 ? std::min(100, static_cast<int>(year_chars * 100 / eff_year_goal)) : 0;
            Put(p, tiny, px, 96, QStringLiteral("YEAR CHARACTERS"), l.muted);
            Put(p, tiny, px + pw, 96, QStringLiteral("%1%").arg(pct), l.accent, 1);
            const double vw = PutHalo(p, hero, px, 134, ShortCount(year_chars), l.ink, l.accent, dots_time, light_ground);
            Put(p, mono, px + vw + 8, 134, ShortHours(year_ms), l.muted);
            // 24-dot segmented goal bar
            const int lit = std::clamp(pct * 24 / 100, 0, 24);
            p.save();
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 24; ++i) {
                p.setBrush(i < lit ? (i >= std::max(0, lit - 4) ? l.accent : l.ink) : Faded(l.muted, 0.26));
                p.drawEllipse(QPointF(px + 3 + i * 10.5, 152), 3.0, 3.0);
            }
            p.restore();
            PaintGoal(p, 2, px, 174);
            Put(p, tiny, px + pw, 174, QStringLiteral("%1 ACTIVE").arg(year_active), l.muted, 1);
        } else if (c == 1) {
            const int pct = eff_daily_goal > 0 ? std::min(100, static_cast<int>(today_chars * 100 / eff_daily_goal)) : 0;
            Put(p, tiny, px, 96, QStringLiteral("TODAY"), l.muted);
            Put(p, tiny, px + pw, 96, daily_goal > 0 ? QStringLiteral("%1%").arg(pct) : QString(), l.accent, 1);
            const double vw = Put(p, hero, px, 134, CountText(today_chars), l.ink);
            Put(p, mono, px + vw + 8, 134, ShortHours(today_ms), l.muted);
            const int lit = std::clamp(pct * 18 / 100, 0, 18);
            p.save();
            p.setPen(Qt::NoPen);
            for (int i = 0; i < 18; ++i) {
                p.setBrush(i < lit ? l.accent : Faded(l.muted, 0.26));
                p.drawEllipse(QPointF(px + 3 + i * 10.5, 152), 3.0, 3.0);
            }
            p.restore();
            PaintGoal(p, 1, px, 174);
        } else if (c == 2) {
            Put(p, tiny, px, 96, QStringLiteral("STREAK"), l.muted);
            Put(p, tiny, px + pw, 96, QStringLiteral("%1 DAYS").arg(library.days.size()), l.muted, 1);
            const double vw = Put(p, hero, px, 134, QString::number(streak), l.ink);
            Put(p, mono, px + vw + 8, 134, QStringLiteral("DAYS"), l.muted);
            Put(p, tiny, px, 174, QStringLiteral("%1 TITLES").arg(titles_read), l.muted);
        } else {
            Put(p, tiny, px, 96, QStringLiteral("PACE"), l.muted);
            Put(p, tiny, px + pw, 96, QStringLiteral("SPEED"), l.muted, 1);
            const double vw = Put(p, hero, px, 134, CountText(avg_day_chars), l.ink);
            Put(p, mono, px + vw + 6, 134, QStringLiteral("/D"), l.muted);
            Put(p, tiny, px, 174, QStringLiteral("%1H / DAY").arg(avg_day_hrs, 0, 'f', 1), l.muted);
            Put(p, tiny, px + pw, 174, chars_per_hr > 0 ? QStringLiteral("%1 / H").arg(ShortCount(chars_per_hr)) : QStringLiteral("—"), l.muted, 1);
        }
        cx += col_w[c];
    }
    p.fillRect(QRectF(ShelfLeft, 192, w_all, 1), l.rule);

    // ---- Band 2: Milestone Ruler (y = 200..268) ----
    static constexpr qint64 Milestones[7] = {500000, 1000000, 2000000, 5000000, 8000000, 15000000, 30000000};
    static const char* const MilestoneLabels[7] = {"500K", "1M", "2M", "5M", "8M", "15M", "30M"};
    int next_ms = 6;
    for (int i = 0; i < 7; ++i) {
        if (library.chars < Milestones[i]) {
            next_ms = i;
            break;
        }
    }
    Put(p, tiny, ShelfLeft, 224, QStringLiteral("MILESTONE"), l.muted);
    Put(p, ShelfFont(l.mono, 15, 0.05), ShelfLeft, 246, CountText(library.chars) + QStringLiteral(" CH"), l.ink);

    const double tr_l = ShelfLeft + 185, tr_r = ShelfRight - 185, tr_w = tr_r - tr_l, tr_y = 228;
    p.fillRect(QRectF(tr_l, tr_y, tr_w, 6), Faded(l.muted, 0.24));
    // Compute piecewise progress along the 7 evenly spaced ticks.
    double frac = 0.0;
    const double seg = 1.0 / 7.0;
    qint64 prev_m = 0;
    for (int i = 0; i < 7; ++i) {
        if (library.chars <= Milestones[i]) {
            const double in_seg = Milestones[i] > prev_m ? static_cast<double>(library.chars - prev_m) / (Milestones[i] - prev_m) : 0.0;
            frac = (i + std::clamp(in_seg, 0.0, 1.0)) * seg;
            break;
        }
        frac = (i + 1) * seg;
        prev_m = Milestones[i];
    }
    p.fillRect(QRectF(tr_l, tr_y, tr_w * std::clamp(frac, 0.0, 1.0), 6), l.ink);
    for (int i = 0; i < 7; ++i) {
        const double tx = tr_l + tr_w * ((i + 1) * seg);
        const bool is_next = i == next_ms && library.chars < Milestones[i];
        const bool passed = library.chars >= Milestones[i];
        if (is_next) {
            p.fillRect(QRectF(tx - 1.5, tr_y - 7, 3, 20), l.accent);
        } else {
            p.fillRect(QRectF(tx - 0.75, tr_y - 5, 1.5, 16), passed ? l.ink : Faded(l.muted, 0.55));
        }
        Put(p, tiny, tx, tr_y + 26, QString::fromLatin1(MilestoneLabels[i]), is_next ? l.ink : l.muted, 2);
    }

    QString eta_str = QStringLiteral("—");
    const qint64 rem = std::max<qint64>(0, Milestones[next_ms] - library.chars);
    const double pace30 = recent30_chars > 0 ? recent30_chars / 30.0 : static_cast<double>(avg_day_chars);
    if (rem == 0) {
        eta_str = QStringLiteral("REACHED");
    } else if (pace30 > 50.0) {
        const qint64 days_needed = std::min<qint64>(36500, static_cast<qint64>(std::ceil(rem / pace30)));
        eta_str = today.addDays(days_needed).toString(QStringLiteral("d MMM yyyy")).toUpper();
    }
    PutHalo(p, eta_face, ShelfRight, 232, eta_str, l.ink, l.accent, dots_time, light_ground, 1);
    Put(p, tiny, ShelfRight, 250, QStringLiteral("NEXT · %1").arg(CountText(Milestones[next_ms])), l.accent, 1);
    p.fillRect(QRectF(ShelfLeft, 268, w_all, 1), l.rule);

    // ---- Band 3: 52-Week Dot Heatmap + Daily Seismograph (y = 282..486, dot-free backdrop!) ----
    const int weeks = 52;
    const QDate first_day = today.addDays(-(today.dayOfWeek() - 1) - 7 * (weeks - 1));
    qint64 max_day = 1;
    for (QDate d = first_day; d <= today; d = d.addDays(1)) {
        max_day = std::max(max_day, day_chars.value(d.toString(Qt::ISODate)));
    }
    const double col_gap = w_all / weeks;
    const double dot_top = 304.0, row_gap = 14.2;
    int last_month = -1;
    QPolygonF wave;
    p.save();
    for (int w = 0; w < weeks; ++w) {
        const QDate wstart = first_day.addDays(w * 7);
        if (wstart.month() != last_month) {
            last_month = wstart.month();
            Put(p, tiny, ShelfLeft + w * col_gap, 294, wstart.toString(QStringLiteral("MMM")).toUpper(), l.muted);
        }
        for (int d = 0; d < 7; ++d) {
            const QDate dt = wstart.addDays(d);
            if (dt > today) {
                continue;
            }
            const qint64 ch = day_chars.value(dt.toString(Qt::ISODate));
            const double norm = std::clamp(static_cast<double>(ch) / max_day, 0.0, 1.0);
            const double dx = ShelfLeft + (w + 0.5) * col_gap;
            const double dy = dot_top + (d + 0.5) * row_gap;
            if (dt == today) {
                p.setPen(QPen(l.accent, 2.0));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(QPointF(dx, dy), 5.0, 5.0);
            } else if (ch <= 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(Faded(l.muted, 0.18));
                p.drawEllipse(QPointF(dx, dy), 2.1, 2.1);
            } else {
                const bool hit_goal = daily_goal > 0 ? ch >= daily_goal : norm >= 0.72;
                p.setPen(Qt::NoPen);
                p.setBrush(hit_goal ? Faded(l.accent, 0.78 + norm * 0.22) : Faded(l.ink, 0.24 + norm * 0.68));
                const double rad = 2.5 + std::sqrt(norm) * 2.5;
                p.drawEllipse(QPointF(dx, dy), rad, rad);
            }
            const int day_idx = w * 7 + d;
            const double wx = ShelfLeft + (static_cast<double>(day_idx) / (weeks * 7)) * w_all;
            const double wy = 474.0 - norm * 48.0;
            wave.append(QPointF(wx, wy));
        }
    }
    p.fillRect(QRectF(ShelfLeft, 475.0, w_all, 1), l.rule);
    if (wave.size() > 1) {
        QPolygonF fill_poly = wave;
        fill_poly.append(QPointF(wave.last().x(), 475.0));
        fill_poly.append(QPointF(wave.first().x(), 475.0));
        p.setPen(Qt::NoPen);
        p.setBrush(Faded(l.ink, 0.07));
        p.drawPolygon(fill_poly);
        p.setPen(QPen(Faded(l.ink, 0.88), 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPolyline(wave);
    }
    p.restore();
    p.fillRect(QRectF(ShelfLeft, 488, w_all, 1), l.rule);

    // ---- Band 4: Bottom Split — Arc Graph (left, with Week/Month/Year/All Time) + Peak Days (right) ----
    const double arc_left = ShelfLeft, arc_w = w_all * 0.61;
    const double peak_left = ShelfLeft + arc_w + 34, peak_w = ShelfRight - peak_left;

    // Range selector pills (ONLY affects the Arc Graph)
    static const char* const RangeNames[4] = {"WEEK", "MONTH", "YEAR", "ALL TIME"};
    Put(p, tiny, arc_left, 510, QStringLiteral("CHARACTERS ARC"), l.muted);
    double rx = arc_left + arc_w;
    for (int r = 3; r >= 0; --r) {
        const QString lbl = QString::fromLatin1(RangeNames[r]);
        const double rw = Advance(tiny, lbl) + 14;
        rx -= rw;
        const bool on = stats_range == r;
        const bool lit = ShelfHovered(21, r);
        if (on) {
            p.fillRect(QRectF(rx, 496, rw, 20), Faded(l.accent, stats_focus == 0 ? 0.20 : 0.10));
            p.setPen(QPen(stats_focus == 0 ? l.accent : l.rule, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(rx + 0.5, 496.5, rw - 1, 19));
            if (stats_focus == 0) {
                PutHalo(p, tiny, rx + rw / 2, 510, lbl, l.ink, l.accent, dots_time, light_ground, 2);
            } else {
                Put(p, tiny, rx + rw / 2, 510, lbl, l.ink, 2);
            }
        } else {
            Put(p, tiny, rx + rw / 2, 510, lbl, lit ? l.ink : l.muted, 2);
        }
        shelf_hits.append({QRectF(rx, 494, rw, 24), 21, r});
        rx -= 6;
    }

    // Build Arc Graph points for the chosen range (characters only)
    struct ArcPt {
        QString label;
        qint64 value{};
    };
    QList<ArcPt> pts;
    if (stats_range == 0) { // Week: last 7 days
        for (int i = 6; i >= 0; --i) {
            const QDate d = today.addDays(-i);
            pts.append({d.toString(QStringLiteral("ddd")).toUpper(), day_chars.value(d.toString(Qt::ISODate))});
        }
    } else if (stats_range == 1) { // Month: last 4 weeks
        for (int w = 3; w >= 0; --w) {
            qint64 sum = 0;
            for (int d = 0; d < 7; ++d) {
                const QDate dt = today.addDays(-(w * 7 + d));
                sum += day_chars.value(dt.toString(Qt::ISODate));
            }
            pts.append({QStringLiteral("WK %1").arg(4 - w), sum});
        }
    } else if (stats_range == 2) { // Year: last 12 months
        for (int m = 11; m >= 0; --m) {
            const QDate first = QDate(today.year(), today.month(), 1).addMonths(-m);
            const int days_in_m = first.daysInMonth();
            qint64 sum = 0;
            for (int d = 0; d < days_in_m; ++d) {
                sum += day_chars.value(first.addDays(d).toString(Qt::ISODate));
            }
            pts.append({first.toString(QStringLiteral("MMM")).toUpper(), sum});
        }
    } else { // All time: up to 8 recent quarters
        const int cur_q = (today.month() - 1) / 3;
        for (int q = 7; q >= 0; --q) {
            int yy = today.year();
            int qq = cur_q - q;
            while (qq < 0) {
                qq += 4;
                --yy;
            }
            const QDate qstart(yy, qq * 3 + 1, 1);
            const QDate qend = qstart.addMonths(3).addDays(-1);
            qint64 sum = 0;
            for (QDate d = qstart; d <= qend && d <= today; d = d.addDays(1)) {
                sum += day_chars.value(d.toString(Qt::ISODate));
            }
            pts.append({QStringLiteral("Q%1 '%2").arg(qq + 1).arg(yy % 100, 2, 10, QLatin1Char('0')), sum});
        }
    }

    // Trim leading empty periods so the arc starts where reading activity began.
    int first_active = -1;
    for (int i = 0; i < pts.size(); ++i) {
        if (pts[i].value > 0) {
            first_active = i;
            break;
        }
    }
    if (first_active < 0) {
        pts.clear();
    } else if (first_active > 0) {
        pts = pts.mid(first_active);
    }

    qint64 raw_max = 1;
    for (const ArcPt& pt : pts) {
        raw_max = std::max(raw_max, pt.value);
    }
    // Round ceiling cleanly so the 3 Y-axis labels are tidy numbers (e.g. 5K, 10K, 15K).
    auto nice_top = [](qint64 v) -> qint64 {
        if (v <= 300) {
            return 300;
        }
        const double step = std::pow(10.0, std::floor(std::log10(static_cast<double>(v) / 3.0)));
        double unit = step;
        if (v / 3.0 > unit * 5.0) {
            unit *= 10.0;
        } else if (v / 3.0 > unit * 2.0) {
            unit *= 5.0;
        } else if (v / 3.0 > unit) {
            unit *= 2.0;
        }
        return static_cast<qint64>(std::ceil((v / 3.0) / unit) * unit * 3.0);
    };
    const qint64 y_top = nice_top(raw_max);
    const double g_left = arc_left + 44, g_right = arc_left + arc_w - 6;
    const double g_top = 528.0, g_bot = 610.0;

    // 3 Y-axis labels at 1/3, 2/3, 3/3 of y_top + whisper-thin guide lines
    if (!pts.isEmpty()) {
        for (int tick = 1; tick <= 3; ++tick) {
            const double ty = g_bot - (g_bot - g_top) * (tick / 3.0);
            p.fillRect(QRectF(g_left, ty, g_right - g_left, 1), Faded(l.rule, 0.45));
            Put(p, tiny, g_left - 8, ty + 3.5, ShortCount(y_top * tick / 3), l.muted, 1);
        }
    }
    p.fillRect(QRectF(g_left, g_bot, g_right - g_left, 1), l.rule);

    if (pts.isEmpty()) {
        Put(p, tiny, (g_left + g_right) * 0.5, (g_top + g_bot) * 0.5 + 4, QStringLiteral("NO ACTIVITY YET"), l.muted, 2);
    } else if (pts.size() == 1) {
        const double px = (g_left + g_right) * 0.5;
        const double py = g_bot - (static_cast<double>(pts[0].value) / std::max<qint64>(1, y_top)) * (g_bot - g_top);
        p.save();
        p.setPen(QPen(l.accent, 2.2));
        p.setBrush(l.ground);
        p.drawEllipse(QPointF(px, py), 4.2, 4.2);
        Put(p, tiny, px, g_bot + 16, pts[0].label, l.muted, 2);
        p.restore();
    } else {
        QList<QPointF> coords;
        for (int i = 0; i < pts.size(); ++i) {
            const double px = g_left + 8 + (static_cast<double>(i) / (pts.size() - 1)) * (g_right - g_left - 16);
            const double py = g_bot - (static_cast<double>(pts[i].value) / std::max<qint64>(1, y_top)) * (g_bot - g_top);
            coords.append(QPointF(px, py));
        }
        p.save();
        if (coords.size() > 2) {
            QPolygonF solid;
            for (int i = 0; i < coords.size() - 1; ++i) {
                solid.append(coords[i]);
            }
            p.setPen(QPen(l.ink, 1.8));
            p.drawPolyline(solid);
        }
        QPen dashed(l.accent, 1.8, Qt::DashLine);
        p.setPen(dashed);
        p.drawLine(coords[coords.size() - 2], coords.last());

        for (int i = 0; i < coords.size(); ++i) {
            if (i == coords.size() - 1) {
                p.setPen(QPen(l.accent, 2.2));
                p.setBrush(l.ground);
                p.drawEllipse(coords[i], 4.2, 4.2);
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(l.ink);
                p.drawEllipse(coords[i], 2.8, 2.8);
            }
            if (pts.size() <= 8 || i % 2 == 0 || i == pts.size() - 1) {
                Put(p, tiny, coords[i].x(), g_bot + 16, pts[i].label, l.muted, 2);
            }
        }
        p.restore();
    }

    // Right side: Peak Days (top 3 reading days)
    Put(p, tiny, peak_left, 510, QStringLiteral("PEAK DAYS"), l.muted);
    Put(p, tiny, ShelfRight, 510, QStringLiteral("ALL TIME"), l.ink, 1);
    const qint64 best_peak = peaks.isEmpty() ? 1 : std::max<qint64>(1, peaks.first().chars);
    for (int i = 0; i < 3; ++i) {
        const double py = 536.0 + i * 32.0;
        if (i < peaks.size()) {
            const PeakDay& pk = peaks[i];
            const QString left_lbl = QStringLiteral("%1 · %2").arg(i + 1, 2, 10, QLatin1Char('0')).arg(pk.date.toString(QStringLiteral("d MMM")).toUpper());
            const QString right_lbl = QStringLiteral("%1 CH · %2").arg(CountText(pk.chars), ShortHours(pk.ms));
            Put(p, mono, peak_left, py, left_lbl, Faded(l.ink, 0.78));
            Put(p, mono, ShelfRight, py, right_lbl, l.ink, 1);
            p.fillRect(QRectF(peak_left, py + 7, peak_w, 4), Faded(l.muted, 0.20));
            const double fill_w = peak_w * (static_cast<double>(pk.chars) / best_peak);
            p.fillRect(QRectF(peak_left, py + 7, fill_w, 4), i == 0 ? l.accent : l.ink);
        } else {
            Put(p, mono, peak_left, py, QStringLiteral("%1 · —").arg(i + 1, 2, 10, QLatin1Char('0')), l.muted);
            p.fillRect(QRectF(peak_left, py + 7, peak_w, 4), Faded(l.muted, 0.14));
        }
    }

    p.restore();
    p.fillRect(QRectF(ShelfLeft, 646, w_all, 1), l.rule);
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ FOCUS"), Qt::Key_Down},
             {QStringLiteral("←"), Qt::Key_Left}, {QStringLiteral("→ CHANGE"), Qt::Key_Right},
             {QStringLiteral("Q"), Qt::Key_Q}, {QStringLiteral("E TAB"), Qt::Key_E},
             {QStringLiteral("ESC LIBRARY"), Qt::Key_Escape}});
}

void HomeWindow::PaintLog(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const QList<ShelfTitle>& titles = ShelfTitles();
    if (shelf_choice >= titles.size()) {
        return;
    }
    const Game& game = titles[shelf_choice].game;
    const TitleReading& reading = ReadingOf(game.id);
    const int count = static_cast<int>(reading.lines.size());
    const int chosen = std::clamp(log_line < 0 ? count - 1 : log_line, 0, std::max(0, count - 1));
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);

    double x = ShelfLeft;
    x += Put(p, small, x, 49, QStringLiteral("‹  "), ShelfHovered(8, 0) ? l.ink : l.muted);
    // Its name: in the text face where it is Japanese (the mono face has none).
    const QString said = ShelfTitleOf(game.id, game.title);
    x += HasJapanese(said) ? Put(p, ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 13), x, 49, said, ShelfHovered(8, 0) ? l.ink : l.muted)
                           : Put(p, small, x, 49, said.toUpper(), ShelfHovered(8, 0) ? l.ink : l.muted);
    Put(p, small, x, 49, QStringLiteral(" · BACKLOG"), l.ink);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    Put(p, small, right, 49, QStringLiteral("%1 OF %2 LINES · ").arg(chosen + 1).arg(CountText(reading.line_count)), l.muted, 1);
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 240, 30), 8, 0});

    // The lines as they were read, the one in hand two thirds of the way down, in ink; those
    // before it above, those after it below, each with when it came and who said it.
    const double text_left = ShelfLeft + 190, text_width = ShelfRight - text_left - 40;
    const QFont face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 19);
    const QFontMetricsF metrics(face);
    const auto tall = [&](int i) {
        return metrics.boundingRect(QRectF(0, 0, text_width, 2000), Qt::TextWordWrap | Qt::AlignLeft, reading.lines[i].text).height() + 18;
    };
    const auto draw = [&](int i, double top, bool on) {
        const TitleReading::Line& line = reading.lines[i];
        const double height = tall(i);
        if (on) {
            p.fillRect(QRectF(ShelfLeft, top + 4, 3, height - 8), l.accent);
        }
        const QDateTime when = QDateTime::fromMSecsSinceEpoch(line.time);
        Put(p, tiny, ShelfLeft + 16, top + 26, when.toString(QStringLiteral("yyyy/MM/dd HH:mm")), l.muted);
        if (!line.speaker.isEmpty()) {
            const QFont who = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 12.5);
            Put(p, who, ShelfLeft + 16, top + 46, Paint::Fit(line.speaker, who, 160), l.muted);
        }
        p.setFont(face);
        p.setPen(on ? l.ink : Faded(l.ink, ShelfHovered(9, i) ? 0.8 : 0.5));
        p.drawText(QRectF(text_left, top + 8, text_width, height), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, line.text);
        shelf_hits.append({QRectF(ShelfLeft, top, ShelfRight - ShelfLeft, height), 9, i});
    };
    p.save();
    p.setClipRect(QRectF(0, ShelfTop - 6, SheetW, ShelfBottom - ShelfTop + 14));
    if (count > 0) {
        const double anchor = ShelfTop + (ShelfBottom - ShelfTop) * 0.62;
        double top = anchor - tall(chosen) / 2;
        draw(chosen, top, true);
        double above = top;
        for (int i = chosen - 1; i >= 0 && above > ShelfTop - 10; --i) {
            above -= tall(i);
            draw(i, above, false);
        }
        double below = top + tall(chosen);
        for (int i = chosen + 1; i < count && below < ShelfBottom; ++i) {
            draw(i, below, false);
            below += tall(i);
        }
    }
    p.restore();

    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ LINE"), Qt::Key_Down}, {QStringLiteral("Q"), Qt::Key_Q},
             {QStringLiteral("E PAGE"), Qt::Key_E}, {QStringLiteral("ENTER COPY"), Qt::Key_Return}, {QStringLiteral("ESC BACK"), Qt::Key_Escape}});
}

void HomeWindow::PaintPages(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const QList<Item> items = Items();
    axis_choice = std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1);
    const Item& item = items[axis_choice];
    const QList<Row> rows = Rows(item);
    const int chosen = std::clamp(RowChoice(item), 0, std::max(0, static_cast<int>(rows.size()) - 1));
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont value = ShelfFont(l.mono, 12.5, 0.05);
    // What the page's rows are called under: a settings page's own name, else its label.
    const QString page_name = item.kind == Item::Settings || item.kind == Item::Dictionaries || item.kind == Item::Scripts ? item.system
                              : item.kind == Item::Consoles ? QStringLiteral("Consoles") : QStringLiteral("Statistics");
    const auto page_label = [&](const Item& one) { return Labelled(QStringLiteral("Page"), one.label).toUpper(); };

    p.save();
    p.translate(tab_body_dx, 0);

    // The sections, as words: halo alone shows selection (plus horizontal beam on Nocturne).
    const bool light_ground = l.ground.lightness() > 140;
    int sections = 0;
    for (const Item& one : items) {
        sections += (one.kind == Item::System || one.kind == Item::Statistics) ? 0 : 1;
    }
    const double pitch = std::min(50.0, (ShelfBottom - ShelfTop - 40) / std::max(1, sections - 1));
    const QFont section_face = ShelfFont(l.display, pitch < 50 ? 24 : 27, 0.02);
    double y = ShelfTop + 44;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::System || items[i].kind == Item::Statistics) {
            continue;
        }
        const double lit = Ease(5, i, ShelfHovered(5, i));
        const bool here = i == axis_choice;
        const QString lbl = page_label(items[i]);
        double width = 0;
        if (here) {
            if (l.beam) {
                PaintBeam(p, ShelfLeft - 10, std::min(ShelfLeft + Advance(section_face, lbl) + 28, 420.0), y - 11, l.ink, l.accent, dots_time);
            }
            width = PutHalo(p, section_face, ShelfLeft, y, lbl, pages_rows ? Faded(l.ink, 0.85) : l.ink, l.accent, dots_time, light_ground);
        } else {
            width = Put(p, section_face, ShelfLeft + 4 * lit, y, lbl, Mix(Faded(l.muted, 0.72), l.ink, lit));
        }
        shelf_hits.append({QRectF(ShelfLeft, y - 34, std::min(width + 20, 380.0), pitch - 4), 5, i});
        y += pitch;
    }

    const double left = 440, right = ShelfRight, row_height = 34;
    const QFont name = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 15.5);
    if (item.kind == Item::Statistics) {
        // The last 26 weeks, a square a day, a week a column, today at the bottom right: the
        // more read, the more ink; a day that met the goal in the accent. Under the figures.
        const QDate today = QDate::currentDate();
        QHash<QString, qint64> read;
        qint64 most = 1;
        for (const Day& day : library.days) {
            read.insert(day.day, day.chars);
        }
        const int weeks = 26;
        const QDate first_day = today.addDays(-(today.dayOfWeek() - 1) - 7 * (weeks - 1));
        for (QDate d = first_day; d <= today; d = d.addDays(1)) {
            most = std::max(most, read.value(d.toString(Qt::ISODate)));
        }
        const double cell = 9, gap = 2, top = ShelfBottom - 7 * (cell + gap) + gap;
        const int goal = DailyGoal();
        for (QDate d = first_day; d <= today; d = d.addDays(1)) {
            const int column = static_cast<int>(first_day.daysTo(d) / 7);
            const qint64 chars = read.value(d.toString(Qt::ISODate));
            const QRectF box(right - (weeks - column) * (cell + gap) + gap, top + (d.dayOfWeek() - 1) * (cell + gap), cell, cell);
            if (chars <= 0) {
                p.fillRect(box, Faded(l.rule, 0.55));
            } else if (goal > 0 && chars >= goal) {
                p.fillRect(box, l.accent);
            } else {
                p.fillRect(box, Faded(l.ink, 0.25 + 0.75 * std::sqrt(static_cast<double>(chars) / most)));
            }
        }
        Put(p, tiny, right - weeks * (cell + gap) - 14, ShelfBottom - 2, QStringLiteral("26 WEEKS"), l.muted, 1);
    }

    if (!find_text.isNull()) {
        // What is typed, and the settings found: each with its page, the one in hand marked.
        const QFont typed = ShelfFont(l.display, 22, 0.02);
        const QList<int> found = FoundSettings(find_text);
        Put(p, typed, left, ShelfTop + 18, find_text.isEmpty() ? QStringLiteral("SEARCH SETTINGS") : find_text.toUpper(),
            find_text.isEmpty() ? l.muted : l.ink);
        double row_y = ShelfTop + 30 + row_height;
        p.fillRect(QRectF(left, row_y - 22, right - left, 1), l.rule);
        for (int i = 0; i < std::min<int>(found.size(), 11); ++i) {
            const SettingRow& row = SettingRows[found[i]];
            const bool on = i == std::clamp(find_choice, 0, static_cast<int>(found.size()) - 1);
            const double lit = Ease(19, found[i], ShelfHovered(19, found[i]));
            if (on || lit > 0) {
                p.fillRect(QRectF(left, row_y - 21, 3, row_height - 1), on ? l.accent : Faded(l.accent, 0.45 * lit));
            }
            Put(p, name, left + 56, row_y, Labelled(QString::fromLatin1(row.section), QString::fromLatin1(row.name)),
                on ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
            Put(p, value, right, row_y - 1, Labelled(QStringLiteral("Page"), QString::fromLatin1(row.section).toUpper()).toUpper(), on ? l.ink : l.muted, 1);
            p.fillRect(QRectF(left, row_y + row_height - 22, right - left, 1), l.rule);
            shelf_hits.append({QRectF(left, row_y - 22, right - left, row_height), 19, found[i]});
            row_y += row_height;
        }
        if (found.isEmpty() && !find_text.isEmpty()) {
            Put(p, name, left + 56, row_y, QStringLiteral("No results"), l.muted);
        }
        p.restore();
        p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
        PutKeys(p, tiny, ShelfRight, 674,
                {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ CHOOSE"), Qt::Key_Down}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return},
                 {QStringLiteral("ESC BACK"), Qt::Key_Escape}});
        return;
    }
    if (item.kind == Item::Dictionaries || item.kind == Item::Scripts) {
        // A list of its own stands here (dictionary_panel.h, placed by PlaceDictionaries): only
        // the foot is the page's.
        p.restore();
        p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
        if (!notice.isEmpty()) {
            const QFont say = ShelfFont(l.text, 14);
            Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        } else if (!pages_rows) {
            PutKeys(p, tiny, ShelfRight, 674,
                    {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ SECTION"), Qt::Key_Down}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return},
                     {pages_back ? QStringLiteral("ESC BACK TO GAME") : QStringLiteral("ESC LIBRARY"), Qt::Key_Escape}});
        } else if (item.kind == Item::Dictionaries ? static_cast<bool>(dictionary_panel) : static_cast<bool>(scripts_panel)) {
            PutKeys(p, tiny, ShelfRight, 674,
                    item.kind == Item::Dictionaries ? dictionary_panel->KeyHints() : scripts_panel->KeyHints());
        }
        return;
    }
    // Its rows: what each is, and what it is set to; the one in hand marked. The list stands
    // where the wheel left it; a row taken in hand by the keys is brought into it.
    const int count = static_cast<int>(rows.size());
    const bool popup_page = item.kind == Item::Settings && item.system == QStringLiteral("Popup");
    const int shown = popup_page ? 7 : 12;
    const int last_first = std::max(0, count - shown);
    if (pages_seen != item.label) {
        // Another page: from its top, or where its row in hand is; its rows come in.
        pages_seen = item.label;
        pages_first = std::clamp(chosen - shown + 1, 0, last_first);
        pages_first_at = pages_first;
        pages_bar_at = chosen;
        pages_seen_row = chosen;
        page_t = shelf_still ? 1 : 0;
    } else if (pages_seen_row != chosen) {
        pages_seen_row = chosen;
        pages_first = std::clamp(pages_first, chosen - shown + 1, chosen);
    }
    pages_first = std::clamp(pages_first, 0, last_first);
    GlideTo(pages_bar_at, chosen, 0.035);
    shelf_busy = shelf_busy || pages_first_at != pages_first || page_t != 1;

    const double list_top = ShelfTop + 8, list_tall = shown * row_height;
    if (popup_page) {
        // The room under the 7-row Popup list is permanently reserved for the live popup preview.
        const double top = list_top + shown * row_height + 16;
        if (ShelfBottom - 12 - top > 110) {
            popup_preview_area = QRectF(left, top, right - left, ShelfBottom - 12 - top);
        }
    }
    p.fillRect(QRectF(left, list_top, right - left, 1), l.rule);
    p.save();
    p.setClipRect(QRectF(left - 2, list_top + 1, right - left + 4, list_tall));
    p.setOpacity(p.opacity() * page_t);
    p.translate(12 * (1 - page_t) * (1 - page_t), 0);
    // Soft atmospheric wash on the active row (no left selection bar); Nocturne adds its horizontal beam.
    const double bar_y = list_top + 1 + (pages_bar_at - pages_first_at) * row_height;
    if (pages_rows) {
        QLinearGradient wash(left, bar_y, right, bar_y);
        wash.setColorAt(0.0, Faded(l.accent, 0.11));
        wash.setColorAt(0.65, Faded(l.accent, 0.04));
        wash.setColorAt(1.0, Faded(l.accent, 0.0));
        p.fillRect(QRectF(left, bar_y, right - left, row_height - 1), wash);
        if (l.beam) {
            PaintBeam(p, left - 8, right + 8, bar_y + (row_height - 1) * 0.5, l.ink, l.accent, dots_time);
        }
    }
    const int from = std::max(0, static_cast<int>(std::floor(pages_first_at)));
    for (int i = from; i < std::min(count, from + shown + 1); ++i) {
        const Row& row = rows[i];
        const bool on = i == chosen;
        const bool active_row = on && pages_rows;
        const double base = list_top + 22 + (i - pages_first_at) * row_height;
        const bool over = ShelfHovered(6, i) || ShelfHovered(15, i) || ShelfHovered(16, i);
        const double lit = Ease(6, i, over);
        const QString num_str = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        const double room = right - left - 56 - 330;
        const QString label_str = Paint::Fit(Labelled(page_name, row.title), name, room);
        if (active_row) {
            PutHalo(p, tiny, left + 16, base - 1, num_str, l.ink, l.accent, dots_time, light_ground);
            PutHalo(p, name, left + 56, base, label_str, l.ink, l.accent, dots_time, light_ground);
        } else {
            Put(p, tiny, left + 16, base - 1, num_str, on ? l.ink : l.muted);
            Put(p, name, left + 56, base, label_str, on ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
        }
        if (row.yours) {
            // Set by the user: a small square in the accent; Backspace puts the row back.
            p.fillRect(QRectF(left + 42, base - 9, 5, 5), l.accent);
        }
        const QRectF area(left, base - 22, right - left, row_height);
        shelf_hits.append({area.intersected(QRectF(left, list_top, right - left, list_tall)), 6, i});
        const QString said = item.kind == Item::Settings && row.setting >= 0 && IsSetting(row.setting, "Look") ? l.name : row.value;
        // The value, with room at its right for the arrow of a row that left and right change.
        const double value_right = right - 22;
        if (row.steps && !renaming) {
            // Fixed slot width so ‹ and › stay aligned across every setting regardless of label length.
            const double slot_wide = 230;
            const double arrow_left = value_right - slot_wide - 20;
            const double slot_center = (arrow_left + 12 + value_right) / 2.0;
            if (!said.isEmpty()) {
                const QString fit_val = Paint::Fit(Capitals(said), value, slot_wide);
                if (active_row) {
                    PutHalo(p, value, slot_center, base - 1, fit_val, l.ink, l.accent, dots_time, light_ground, 2);
                } else {
                    Put(p, value, slot_center, base - 1, fit_val,
                        row.dim && !on ? l.muted : on ? l.ink : Mix(l.muted, l.ink, lit), 2);
                }
            }
            // Its two arrows, for the mouse: there while it is in hand or under the pointer.
            const double show = std::max(on ? 1.0 : 0.0, lit);
            const double before = Ease(15, i, ShelfHovered(15, i)), after = Ease(16, i, ShelfHovered(16, i));
            if (show > 0) {
                Put(p, value, arrow_left, base - 1, QStringLiteral("‹"), Faded(Mix(l.muted, l.accent, before), show));
                Put(p, value, right - 2, base - 1, QStringLiteral("›"), Faded(Mix(l.muted, l.accent, after), show), 1);
            }
            const QRectF list(left, list_top, right - left, list_tall);
            shelf_hits.append({QRectF(arrow_left - 14, base - 22, slot_center - (arrow_left - 14), row_height).intersected(list), 15, i});
            shelf_hits.append({QRectF(slot_center, base - 22, right - slot_center, row_height).intersected(list), 16, i});
        } else if (!said.isEmpty()) {
            const QString fit_val = Paint::Fit(Capitals(said), value, 290);
            if (active_row) {
                PutHalo(p, value, value_right, base - 1, fit_val, l.ink, l.accent, dots_time, light_ground, 1);
            } else {
                Put(p, value, value_right, base - 1, fit_val,
                    row.dim && !on ? l.muted : on ? l.ink : Mix(l.muted, l.ink, lit), 1);
            }
        }
        p.fillRect(QRectF(left, base + row_height - 22, right - left, 1), Faded(l.rule, 0.65));
    }
    p.restore();
    if (count > shown) {
        // Where the list stands in its rows: a hairline with the part shown in ink, which the
        // pointer takes hold of.
        const QRectF track(right + 12, list_top + 1, 2, list_tall - 1);
        const double lit = Ease(18, 0, ShelfHovered(18, 0) || pages_drag == 2);
        p.fillRect(track, Faded(l.rule, 0.7));
        p.fillRect(QRectF(track.left() - lit, track.top() + track.height() * pages_first_at / count, 2 + 2 * lit, track.height() * shown / count),
                   Mix(l.muted, l.ink, lit));
        shelf_hits.append({track.adjusted(-10, 0, 10, 0), 18, 0});
    }
    // The row in hand, a slider: a rule with a tick where it stands, which the pointer moves.
    if (chosen < count) {
        const Row& row = rows[chosen];
        if (row.setting >= 0 && SettingRows[row.setting].step != 0) {
            const SettingRow& setting = SettingRows[row.setting];
            const double share = std::clamp((Slide(row.setting, 0) - setting.low) / std::max(1e-9, setting.high - setting.low), 0.0, 1.0);
            const QRectF bar(left, ShelfBottom - 68, right - left - (item.kind == Item::Statistics ? 320 : 0), 2);
            const double lit = Ease(17, 0, ShelfHovered(17, 0) || pages_drag == 1);
            p.fillRect(bar, l.rule);
            p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * share, 2), l.ink);
            p.fillRect(QRectF(bar.left() + bar.width() * share - 1 - lit, bar.top() - 7 - 2 * lit, 3 + 2 * lit, 16 + 4 * lit), l.accent);
            shelf_hits.append({bar.adjusted(0, -14, 0, 14), 17, 0});
        }
        // No words about the row: its name and its value say it (the maintainer, 2026-11-10: "the
        // design should be explanation enough").
    }

    p.restore();
    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    if (!pages_rows && !renaming) {
        PutKeys(p, tiny, ShelfRight, 674,
                {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ SECTION"), Qt::Key_Down}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return},
                 {QStringLiteral("/ SEARCH"), Qt::Key_Slash},
                 {pages_back ? QStringLiteral("ESC BACK TO GAME") : QStringLiteral("ESC LIBRARY"), Qt::Key_Escape}});
        return;
    }
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ ROW"), Qt::Key_Down}, {QStringLiteral("←"), Qt::Key_Left},
             {QStringLiteral("→ CHANGE"), Qt::Key_Right}, {QStringLiteral("Q"), Qt::Key_Q}, {QStringLiteral("E SECTION"), Qt::Key_E},
             {QStringLiteral("ENTER SELECT"), Qt::Key_Return}, {QStringLiteral("BACKSPACE RESET"), Qt::Key_Backspace},
             {QStringLiteral("/ SEARCH"), Qt::Key_Slash}, {QStringLiteral("ESC SECTIONS"), Qt::Key_Escape}});
}

void HomeWindow::PaintStage(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const QList<ShelfTitle>& titles = ShelfTitles();
    if (shelf_choice >= titles.size()) {
        return;
    }
    const ShelfTitle& title = titles[shelf_choice];
    const Game& game = title.game;
    const TitleRecord record = RecordOf(game.id);
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont value = ShelfFont(l.mono, 12.5, 0.05);

    // The head: the way back, which title of how many, and its ID.
    double x = ShelfLeft;
    x += Put(p, small, x, 49, QStringLiteral("‹  LIBRARY"), ShelfHovered(4, 0) ? l.ink : l.muted);
    x += Put(p, small, x, 49, QStringLiteral(" · "), l.muted);
    x += Put(p, small, x, 49, QStringLiteral("%1").arg(shelf_choice + 1, 2, 10, QLatin1Char('0')), l.accent);
    Put(p, small, x, 49, QStringLiteral(" / %1").arg(titles.size()), l.muted);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    Put(p, small, right, 49, game.id.toUpper() + QStringLiteral(" · "), l.muted, 1);
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 140, 30), 4, 0});

    // Its cover at the right, whole and at its own shape (a Switch case, a DVD case, a jewel
    // case...): as tall as the page lets, or as wide as the right half does; never cut.
    const QPixmap cover = TitleCover(game);
    QRectF plate(ShelfRight - 330, ShelfTop, 330, ShelfBottom - ShelfTop);
    if (cover.isNull()) {
        Paint::PaintFill(p, plate, SystemArt(title.system));
    } else {
        const double shape = static_cast<double>(cover.width()) / std::max(1, cover.height());
        const double tall = ShelfBottom - ShelfTop, most = 500;
        const QSizeF size = tall * shape <= most ? QSizeF(tall * shape, tall) : QSizeF(most, most / shape);
        plate = QRectF(ShelfRight - size.width(), ShelfTop, size.width(), size.height());
        p.drawPixmap(plate, cover, QRectF(0, 0, cover.width(), cover.height()));
    }
    p.setPen(QPen(l.rule, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plate.adjusted(0.5, 0.5, -0.5, -0.5));
    PaintCornerTicks(p, plate, l.accent, 8.0);

    // Its name, as large as three lines of the column let it be.
    const double column = plate.left() - 48 - ShelfLeft;
    const QString said = ShelfTitleOf(game.id, game.title);
    const bool japanese = HasJapanese(said);
    const QString name = japanese ? said : said.toUpper();
    const QRectF name_box(ShelfLeft, ShelfTop + 6, column, 168);
    int size = japanese ? 60 : 54;
    for (; size > 18; size -= 2) {
        const QFont face = japanese ? ShelfFont(l.text, size) : ShelfFont(l.display, size, 0.01);
        const QRectF need = QFontMetricsF(face).boundingRect(QRectF(0, 0, column, 2000), Qt::TextWordWrap | Qt::AlignLeft, name);
        if (need.height() <= name_box.height() && need.width() <= column + 1) {
            break;
        }
    }
    p.setFont(japanese ? ShelfFont(l.text, size) : ShelfFont(l.display, size, 0.01));
    p.setPen(l.ink);
    p.drawText(name_box, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, name);
    if (!japanese && HasJapanese(game.title)) {
        // Its own name, as the game writes it, small under the English.
        const QFont own = ShelfFont(l.text, 15);
        Put(p, own, ShelfLeft, name_box.bottom() + 22, Paint::Fit(ShelfName(game.title), own, column), l.muted);
    }

    // The one filled thing: the key that plays it.
    const QRectF key(ShelfLeft, ShelfTop + 214, 280, 44);
    p.fillRect(key, ShelfHovered(3, 0) ? Faded(l.ink, 0.82) : l.ink);
    const TrackerTitle* tracked = TrackerOf(game.id);
    const qint64 read = tracked != nullptr ? tracked->read : game.chars;
    const QString day = tracked != nullptr ? std::max(tracked->last, game.last_day) : game.last_day;
    Put(p, ShelfFont(l.display, 13, 0.16), key.center().x(), key.center().y() + 4.8, read > 0 ? QStringLiteral("CONTINUE") : QStringLiteral("START"),
        l.ground, 2);
    shelf_hits.append({key, 3, 0});
    Put(p, small, key.right() + 22, key.center().y() + 4,
        day.isEmpty() ? QStringLiteral("NOT STARTED") : QStringLiteral("LAST READ  ") + DayText(day, true), l.muted);
    if (tracked != nullptr && tracked->total > 0) {
        // How far through it: a hairline, filled to what was read.
        const QRectF bar(ShelfLeft, key.bottom() + 16, column, 2);
        p.fillRect(bar, l.rule);
        p.fillRect(QRectF(bar.left(), bar.top(), bar.width() * std::clamp(static_cast<double>(read) / tracked->total, 0.0, 1.0), bar.height()), l.ink);
    }

    // What is known of it, as a table of real things.
    const App* app = AppOf(game.app);
    QList<QPair<QString, QString>> facts;
    // What reads it: a core and its console, or lines and how they come.
    const QString way = Lines::WayOf(game.path);
    Q_UNUSED(app);
    facts.append({QStringLiteral("TEXT FROM"),
                  !way.isEmpty() ? Lines::ReadByOf(game.path)
                                 // A game no script reads is read from the emulator's own picture.
                                 : QStringLiteral("%1 · %2").arg(ocr_games.contains(game.id) ? QStringLiteral("OCR") : QStringLiteral("GAME SCRIPT"),
                                                                   SystemLabel(library, title.system).toUpper())});
    QString from; // whose figures the rows under it are
    if (tracked != nullptr) {
        from = QStringLiteral("STUDY TRACKER");
        facts.append({QStringLiteral("READ"), tracked->total > 0 ? QStringLiteral("%1 OF %2 CHARACTERS · %3%").arg(CountText(read), CountText(tracked->total))
                                                                       .arg(static_cast<int>(std::lround(100.0 * read / tracked->total)))
                                                                 : QStringLiteral("%1 CHARACTERS").arg(CountText(read))});
        if (tracked->hours > 0) {
            facts.append({QStringLiteral("READING TIME"), QStringLiteral("%1%2%3").arg(PlayClock(static_cast<qint64>(tracked->hours * 3.6e6)),
                                                                              tracked->speed > 0 ? QStringLiteral(" · %1 AN HOUR").arg(CountText(tracked->speed)) : QString(),
                                                                              tracked->sessions > 0 ? QStringLiteral(" · %1 SESSIONS").arg(tracked->sessions) : QString())});
        }
        QStringList jiten;
        if (tracked->difficulty > 0) {
            jiten.append(QStringLiteral("DIFFICULTY %1").arg(tracked->difficulty, 0, 'f', 2));
        }
        if (tracked->unique > 0) {
            jiten.append(QStringLiteral("%1 DIFFERENT WORDS").arg(CountText(tracked->unique)));
        }
        if (tracked->coverage > 0) {
            jiten.append(QStringLiteral("%1% KNOWN").arg(static_cast<int>(std::lround(tracked->coverage))));
        }
        if (!jiten.isEmpty()) {
            facts.append({QStringLiteral("JITEN"), jiten.join(QStringLiteral(" · "))});
        }
    } else if (game.chars > 0) {
        from = QStringLiteral("THIS DEVICE");
        const double hours = game.active_ms / 3.6e6;
        facts.append({QStringLiteral("READ"), QStringLiteral("%1 CHARACTERS · %2%3").arg(CountText(game.chars), PlayClock(game.active_ms),
                                                                                              hours > 0.05 ? QStringLiteral(" · %1 AN HOUR").arg(CountText(static_cast<qint64>(game.chars / hours))) : QString())});
    }
    if (const auto other = elsewhere.constFind(game.id); other != elsewhere.constEnd() && other->second > game.last_day) {
        // Read on another device since it was read here: its save is fetched as the game starts.
        facts.append({QStringLiteral("OTHER DEVICE"), QStringLiteral("%1 · %2").arg(other->first.toUpper(), DayText(other->second, true).toUpper())});
    }
#ifndef READER_NO_SAVE_SYNC // the guides are the dev line's
    if (!record.guide.isEmpty()) {
        facts.append({QStringLiteral("GUIDE"), record.guide.toUpper().replace(QLatin1Char('-'), QLatin1Char(' '))});
    }
#endif
    const bool art_missing = ArtMissing(game);
    if (art_missing || art_said_of == game.id) {
        const bool looking = art_finder != nullptr && art_finder->Busy() && art_finder->GameId() == game.id;
        facts.append({QStringLiteral("BOX ART"), looking ? QStringLiteral("SEARCHING…") : art_said_of == game.id && !art_said.isEmpty() ? art_said
                                                         : QStringLiteral("MISSING · F TO FIND")});
    }
    const TitleReading& reading = ReadingOf(game.id);
    if (!reading.days.isEmpty()) {
        facts.append({QStringLiteral("30 DAYS"), QString()});
    }
    if (reading.line_count > 0) {
        facts.append({QStringLiteral("BACKLOG"), QStringLiteral("%1 LINES ON THIS DEVICE · ↓ OPEN").arg(CountText(reading.line_count))});
    }
    double y = ShelfTop + 312;
    for (const auto& fact : facts) {
        p.fillRect(QRectF(ShelfLeft, y - 19, column, 1), l.rule);
        Put(p, tiny, ShelfLeft, y, fact.first, l.muted);
        Put(p, value, ShelfLeft + 150, y, Paint::Fit(fact.second, value, column - 150 - 130), l.ink);
        if (fact.first == QStringLiteral("READ")) {
            Put(p, tiny, ShelfLeft + column, y, from, l.muted, 1);
        }
        if (fact.first == QStringLiteral("30 DAYS")) {
            // What was read of it here on each of its last days, oldest at the left: bars to scale.
            qint64 most = 1;
            for (const auto& day : reading.days) {
                most = std::max(most, day.second);
            }
            const int count = static_cast<int>(reading.days.size());
            for (int d = 0; d < count; ++d) {
                const double tall = std::max(1.0, 16.0 * reading.days[count - 1 - d].second / most);
                p.fillRect(QRectF(ShelfLeft + 150 + d * 9, y + 2 - tall, 6, tall), l.ink);
            }
            Put(p, tiny, ShelfLeft + column, y, QStringLiteral("BEST DAY %1").arg(CountText(most)), l.muted, 1);
        }
        y += 30;
    }
    // Its people: as the game writes them, and how they are said.
    int more = 0;
    const int room = static_cast<int>((ShelfBottom - 8 - y) / 30) - 0;
    const QList<CastMember> cast = CastOf(record.key, std::clamp(room, 0, 5), &more);
    const QFont person = ShelfFont(l.text, 15.5);
    for (int i = 0; i < cast.size(); ++i) {
        p.fillRect(QRectF(ShelfLeft, y - 19, column, 1), l.rule);
        if (i == 0) {
            Put(p, tiny, ShelfLeft, y, QStringLiteral("NAMES"), l.muted);
        }
        const double at = ShelfLeft + 150 + Put(p, person, ShelfLeft + 150, y + 1, cast[i].name, l.ink);
        Put(p, tiny, at + 16, y, cast[i].romaji.toUpper(), l.muted);
        if (i == cast.size() - 1 && more > 0) {
            Put(p, tiny, ShelfLeft + column, y, QStringLiteral("+ %1 MORE").arg(more), l.muted, 1);
        }
        y += 30;
    }
    p.fillRect(QRectF(ShelfLeft, y - 19, column, 1), l.rule);

    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    Put(p, small, ShelfLeft, 674, SystemLabel(library, title.system).toUpper(), l.ink);
    QList<QPair<QString, int>> stage_keys{{read > 0 ? QStringLiteral("ENTER CONTINUE") : QStringLiteral("ENTER START"), Qt::Key_Return},
                                          {QStringLiteral("←"), Qt::Key_Left}, {QStringLiteral("→ NEXT GAME"), Qt::Key_Right},
                                          {QStringLiteral("↑ LIBRARY"), Qt::Key_Up}, {QStringLiteral("↓ BACKLOG"), Qt::Key_Down}};
    if (art_missing) {
        stage_keys.append({QStringLiteral("F FIND BOX ART"), Qt::Key_F});
    }
    stage_keys.append({QStringLiteral("F2 SETTINGS"), Qt::Key_F2});
    PutKeys(p, tiny, ShelfRight, 674, stage_keys);
}

void HomeWindow::PaintShelfBody(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const QList<ShelfTitle>& titles = ShelfTitles();
    const int count = static_cast<int>(titles.size());

    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);

    if (shelf_layout == 1) {
        PaintLibraryCaseStage(p);
        return;
    }
    if (shelf_layout == 2) {
        PaintLibraryCoverFlow(p);
        return;
    }

    p.save();
    p.translate(tab_body_dx, 0);

    // The shelves as floors (the maintainer chose "B · Lift", 2026-11-10): this one fills the page
    // but for a column of every shelf at the right; going to another, this one leaves upward or
    // downward and that one comes in from the other side (shelf_shift, shelf_fade, PaintShelf).
    const double shelf_right = ShelfRight - FloorColumn;
    const auto tint = [&](const Game& game) {
        const QString key = game.id + QLatin1Char('\n') + game.cover;
        if (!shelf_tints.contains(key)) {
            const QPixmap cover = TitleCover(game);
            shelf_tints.insert(key, cover.isNull() ? l.muted : cover.toImage().scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0));
        }
        return shelf_tints.value(key);
    };
    p.save();
    p.setClipRect(QRectF(0, 66, shelf_right + 1, 578));
    p.translate(0, shelf_shift);
    p.setOpacity(p.opacity() * shelf_fade);
    // The shelves above and below, as a thin line of their titles' colours at the edges.
    for (const int other : {shelf_section - 1, shelf_section + 1}) {
        const QList<Game> games = SectionGames(other);
        const double y = other < shelf_section ? ShelfTop - 13 : ShelfBottom + 8;
        double at = ShelfLeft;
        for (int g = 0; g < games.size() && at < shelf_right; ++g) {
            p.fillRect(QRectF(at, y, std::min(40.0, shelf_right - at), 5), Faded(tint(games[g]), 0.6));
            at += 46;
        }
    }

    const double height = ShelfBottom - ShelfTop;
    // After the last title stands one more spine, which adds a title (wizard_page.inc).
    const int spines = count + 1;
    // The spines' widths: closed ones share what the open one leaves, within their bounds;
    // more than fit go off the side, the open one kept in sight.
    const double room = shelf_right - ShelfLeft;
    const double closed = spines > 1 ? std::clamp((room - SpineOpen - SpineGap * (spines - 1)) / (spines - 1), SpineMin, SpineMax) : 0;
    // A title with a real spine stands as wide as its box's own spine is to its height.
    // Open, a title is as wide as its cover at the shelf's height (the maintainer, 2026-11-10: "the
    // front covers should be visible in the correct dimension"), and its column of words beside it.
    QList<double> widths, lefts, plates;
    double total = 0;
    for (int i = 0; i < spines; ++i) {
        const QPixmap spine = i < count ? ShelfSpine(titles[i].game) : QPixmap();
        const double own = spine.isNull() ? closed : std::clamp((height - SpinePlate) * spine.width() / std::max(1, spine.height()), 26.0, 92.0);
        double open = std::max(0.0, 1 - std::fabs(i - shelf_at));
        open = open * open * (3 - 2 * open);
        double plate = SpineOpen * PlateShare;
        if (i < count && open > 0) {
            const QPixmap cover = TitleCover(titles[i].game);
            if (!cover.isNull()) {
                plate = std::clamp(height * cover.width() / std::max(1, cover.height()), 180.0, 430.0);
            }
        }
        const double whole = i < count ? plate + SpineOpen * (1 - PlateShare) : SpineOpen;
        plates.append(plate);
        lefts.append(total);
        widths.append(own + (whole - own) * open);
        total += widths.last() + SpineGap;
    }
    total -= SpineGap;
    const int before = std::clamp(static_cast<int>(std::floor(shelf_at)), 0, spines - 1);
    const int after = std::min(before + 1, spines - 1);
    const double part = std::clamp(shelf_at - before, 0.0, 1.0);
    const double centre = (lefts[before] + widths[before] / 2) * (1 - part) + (lefts[after] + widths[after] / 2) * part;
    const double camera = total > room ? std::clamp(centre - room / 2, 0.0, total - room) : 0;
    double left = ShelfLeft - camera;
    static QHash<QString, int> title_sizes;
    for (int i = 0; i < count; ++i) {
        double open = std::max(0.0, 1 - std::fabs(i - shelf_at));
        open = open * open * (3 - 2 * open);
        const double width = widths[i];
        const double lift = spine_lift.value(i) * (1 - open);
        const QRectF box(left, ShelfTop - 10 * lift, width, height);
        left += width + SpineGap;
        if (box.right() < 0 || box.left() > shelf_right) {
            continue;
        }
        const ShelfTitle& title = titles[i];
        const Game& game = title.game;
        shelf_hits.append({box.adjusted(0, 0, 0, 10 * lift), 1, i});
        p.fillRect(box, l.raised);
        const QString number = QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0'));
        const QString console = SystemLabel(library, title.system).toUpper();

        if (open < 0.98) {
            // Closed: its cover's own colour as a band, its number, its name set down it.
            const double seen = 1 - open;
            const QColor band = tint(game);
            const QPixmap spine = ShelfSpine(game);
            if (!spine.isNull()) {
                // Its real spine, as its box has it, filling the spine here; its number on a
                // small plate of the ground so that it can be read on any picture.
                p.save();
                p.setClipRect(box);
                p.setOpacity(p.opacity() * seen);
                const QRectF picture = box.adjusted(0, SpinePlate, 0, 0);
                const double scale = std::max(picture.width() / spine.width(), picture.height() / spine.height());
                const QSizeF part(picture.width() / scale, picture.height() / scale);
                p.drawPixmap(picture, spine, QRectF((spine.width() - part.width()) / 2, (spine.height() - part.height()) / 2, part.width(), part.height()));
                p.fillRect(QRectF(box.left(), box.top(), box.width(), SpinePlate), l.raised);
                Put(p, tiny, box.center().x() + 0.7, box.top() + 24, number, l.muted, 2);
                p.restore();
            }
            p.save();
            p.setClipRect(box);
            if (!spine.isNull()) {
                p.setOpacity(0); // drawn above; what follows is the drawn spine's
            }
            p.fillRect(QRectF(box.left(), box.top(), box.width(), 5), Faded(band, seen));
            Put(p, tiny, box.center().x() + 0.7, box.top() + 28, number, Faded(l.muted, seen), 2);
            const double from = box.top() + 46, until = box.bottom() - 16 - Advance(tiny, console) - 14;
            const QColor ink = Faded(l.ink, seen * (0.86 + 0.14 * spine_lift.value(i)));
            const QString name = ShelfTitleOf(game.id, game.title);
            if (HasJapanese(name)) {
                const QFont face = ShelfFont(l.text, 17);
                const double cell = 18.6;
                p.setFont(face);
                p.setPen(ink);
                double y = from;
                for (int c = 0; c < name.size(); ++c) {
                    const QChar ch = name[c];
                    if (ch.isSpace()) {
                        y += cell * 0.45;
                        continue;
                    }
                    // A run of Latin letters or figures: one or two stand upright in a cell,
                    // more lie on their side, as in any book set downwards.
                    int run = 0;
                    while (c + run < name.size() && (IsLatin(name[c + run]) || (run > 0 && name[c + run] == QLatin1Char(' ')))) {
                        ++run;
                    }
                    if (run > 0 && !(run == 1 && LiesDown(ch))) {
                        const QString word = name.mid(c, run).trimmed();
                        const double length = word.size() <= 2 ? cell : Advance(face, word) + 4;
                        if (y + length > until) {
                            p.drawText(QRectF(box.center().x() - cell / 2, y, cell, cell), Qt::AlignCenter, QStringLiteral("⋮"));
                            break;
                        }
                        if (word.size() <= 2) {
                            p.drawText(QRectF(box.center().x() - cell, y, cell * 2, cell), Qt::AlignCenter, word);
                        } else {
                            p.save();
                            p.translate(box.center().x(), y + 2);
                            p.rotate(90);
                            p.drawText(QPointF(0, 6.2), word);
                            p.restore();
                        }
                        y += length;
                        c += run - 1;
                        continue;
                    }
                    const bool last_fits = y + cell * 2 > until && c < name.size() - 1;
                    const QRectF at(box.center().x() - cell / 2, y, cell, cell);
                    if (last_fits) {
                        p.drawText(at, Qt::AlignCenter, QStringLiteral("⋮"));
                        break;
                    }
                    if (LiesDown(ch)) {
                        p.save();
                        p.translate(at.center());
                        p.rotate(90);
                        p.drawText(QRectF(-cell / 2, -cell / 2, cell, cell), Qt::AlignCenter, QString(ch));
                        p.restore();
                    } else {
                        p.drawText(at, Qt::AlignCenter, QString(ch));
                    }
                    y += cell;
                }
            } else {
                const QFont face = ShelfFont(l.display, 12.5, 0.06);
                p.save();
                p.translate(box.center().x(), from);
                p.rotate(90);
                Put(p, face, 0, 4.4, Paint::Fit(name.toUpper(), face, until - from), ink);
                p.restore();
            }
            p.save();
            p.translate(box.center().x(), box.bottom() - 16);
            p.rotate(90);
            Put(p, tiny, 0, 3.4, console, Faded(l.muted, seen), 1);
            p.restore();
            p.restore();
        }
        if (open > 0.02) {
            // Open: laid out at its whole width from its left edge, seen through what is open of it.
            p.save();
            p.setClipRect(box);
            p.setOpacity(p.opacity() * open);
            const QRectF plate(box.left(), box.top(), plates[i], height);
            const QPixmap cover = TitleCover(game);
            if (cover.isNull()) {
                Paint::PaintFill(p, plate, SystemArt(title.system));
            } else {
                // The whole cover at its own shape: a box wider than the plate lets stands in the
                // middle of it, on the ground; never cut, never stretched.
                const double scale = std::min(plate.width() / cover.width(), plate.height() / cover.height());
                const QSizeF shown(cover.width() * scale, cover.height() * scale);
                p.fillRect(plate, l.ground);
                const QRectF shown_rect(plate.left() + (plate.width() - shown.width()) / 2, plate.top() + (plate.height() - shown.height()) / 2, shown.width(), shown.height());
                p.drawPixmap(shown_rect, cover, QRectF(0, 0, cover.width(), cover.height()));
                PaintCornerTicks(p, shown_rect, l.accent, 7.0);
            }
            p.fillRect(QRectF(plate.right(), box.top(), 1, height), l.rule);
            const double tx = plate.right() + 28, tw = box.left() + plates[i] + SpineOpen * (1 - PlateShare) - 28 - tx;
            double at = tx + Put(p, small, tx, box.top() + 36, number, l.accent);
            // What reads it: a core and its console, or lines and how they come.
            const QString way = Lines::WayOf(game.path);
            Put(p, small, at, box.top() + 36, way.isEmpty() ? QStringLiteral(" · ") + console + (ocr_games.contains(game.id) ? QStringLiteral(" · OCR") : QString()) : QStringLiteral(" · ") + Lines::ReadByOf(game.path), l.muted);

            // Its name, as large as the column and two lines let it be, with Strong Halo.
            const bool light_ground = l.ground.lightness() > 140;
            if (l.beam) {
                PaintBeam(p, box.left() - 4, box.right() + 4, box.top() + 1, l.ink, l.accent, dots_time);
            }
            const QString said = ShelfTitleOf(game.id, game.title);
            const bool japanese = HasJapanese(said);
            const QString name = japanese ? said : said.toUpper();
            const QRectF name_box(tx, box.top() + 52, tw, 86);
            const QString size_key = l.display + QStringLiteral(":a:") + name;
            int size = title_sizes.value(size_key);
            if (size == 0) {
                for (size = japanese ? 34 : 26; size > 14; size -= 2) {
                    const QFont face = japanese ? ShelfFont(l.text, size) : ShelfFont(l.display, size, 0.02);
                    const QRectF need = QFontMetricsF(face).boundingRect(QRectF(0, 0, tw, 2000), Qt::TextWordWrap | Qt::AlignLeft, name);
                    if (need.height() <= name_box.height() && need.width() <= tw + 1) {
                        break;
                    }
                }
                title_sizes.insert(size_key, size);
            }
            const QFont title_face = japanese ? ShelfFont(l.text, size) : ShelfFont(l.display, size, 0.02);
            const QString single_line = Paint::Fit(name, title_face, tw);
            if (single_line == name) {
                PutHalo(p, title_face, tx, box.top() + 56 + size, name, l.ink, l.accent, dots_time, light_ground);
            } else {
                p.setFont(title_face);
                p.setPen(l.ink);
                p.drawText(name_box, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, name);
            }
            if (!japanese && HasJapanese(game.title)) {
                const QFont sub = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 12.5);
                Put(p, sub, tx, box.top() + 124, Paint::Fit(ShelfName(game.title), sub, tw), l.muted);
            }

            // Story Bookmark: last read line from the title's backlog (omitted when none yet).
            const TitleReading& reading = ReadingOf(game.id);
            if (!reading.lines.isEmpty()) {
                const double bm_top = box.top() + 146;
                const double bm_h = 74;
                QLinearGradient bm_bg(tx, bm_top, tx + tw, bm_top);
                bm_bg.setColorAt(0.0, Faded(l.accent, 0.09));
                bm_bg.setColorAt(1.0, Faded(l.accent, 0.0));
                p.fillRect(QRectF(tx, bm_top, tw, bm_h), bm_bg);
                p.fillRect(QRectF(tx, bm_top, 2.5, bm_h), l.accent);
                const auto& last_line = reading.lines.last();
                const QString who = last_line.speaker.isEmpty() ? QStringLiteral("LAST LINE")
                                                                : QStringLiteral("LAST LINE · ") + last_line.speaker;
                Put(p, tiny, tx + 12, bm_top + 16, Paint::Fit(who, tiny, tw - 90), l.muted);
                Put(p, tiny, tx + tw - 4, bm_top + 16, QStringLiteral("BOOKMARK"), l.muted, 1);
                const QFont jp_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 13.5);
                p.setFont(jp_face);
                p.setPen(l.ink);
                p.drawText(QRectF(tx + 12, bm_top + 24, tw - 16, bm_h - 28), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, last_line.text);
            }

            // 3-column telemetry + segmented 24-dot progress bar.
            const QFont value = ShelfFont(l.mono, 13.5, 0.04);
            const TrackerTitle* tracked = TrackerOf(game.id);
            const qint64 read = tracked != nullptr ? tracked->read : game.chars;
            const qint64 spent = tracked != nullptr && tracked->hours > 0 ? static_cast<qint64>(tracked->hours * 3.6e6) : game.active_ms;
            const QString day = tracked != nullptr ? std::max(tracked->last, game.last_day) : game.last_day;
            const int pct = tracked != nullptr && tracked->total > 0
                                ? std::clamp(static_cast<int>(std::lround(100.0 * read / tracked->total)), 0, 100)
                                : -1;
            const double tele_top = box.top() + 242;
            p.fillRect(QRectF(tx, tele_top, tw, 1), l.rule);
            p.fillRect(QRectF(tx, tele_top + 52, tw, 1), l.rule);
            const double c1 = tx, c2 = tx + tw * 0.44, c3 = tx + tw * 0.74;
            Put(p, tiny, c1, tele_top + 18, QStringLiteral("CHARACTERS"), l.muted);
            Put(p, value, c1, tele_top + 38, read > 0 ? (pct >= 0 ? QStringLiteral("%1 · %2%").arg(ShortCount(read)).arg(pct) : CountText(read)) : QStringLiteral("0"), l.ink);
            Put(p, tiny, c2, tele_top + 18, QStringLiteral("PLAY TIME"), l.muted);
            Put(p, value, c2, tele_top + 38, PlayClock(spent), l.ink);
            Put(p, tiny, c3, tele_top + 18, QStringLiteral("LAST READ"), l.muted);
            Put(p, value, c3, tele_top + 38, day.isEmpty() ? QStringLiteral("—") : DayText(day, true).toUpper(), l.ink);

            // Segmented dot progress bar (24 dots).
            const int dots_n = 24;
            const double dot_step = std::min(11.0, (tw - 6.0) / std::max(1, dots_n - 1));
            const int lit_dots = pct >= 0 ? std::clamp(pct * dots_n / 100, 0, dots_n)
                                          : (read > 0 ? std::clamp(static_cast<int>(std::min<qint64>(read / 10000 + 1, dots_n)), 1, dots_n) : 0);
            p.save();
            p.setPen(Qt::NoPen);
            for (int d = 0; d < dots_n; ++d) {
                p.setBrush(d < lit_dots ? (d >= std::max(0, lit_dots - 3) ? l.accent : l.ink) : Faded(l.muted, 0.26));
                p.drawEllipse(QPointF(tx + 3 + d * dot_step, tele_top + 69), 2.7, 2.7);
            }
            p.restore();

            // Recent 14-day session sparkline for this game.
            const double sp_top = tele_top + 96;
            qint64 sp_max = 1;
            const int sp_n = 14;
            QList<qint64> sp_vals(sp_n, 0);
            const int have_days = std::min<int>(sp_n, static_cast<int>(reading.days.size()));
            for (int d = 0; d < have_days; ++d) {
                const qint64 v = reading.days[have_days - 1 - d].second;
                sp_vals[sp_n - have_days + d] = v;
                sp_max = std::max(sp_max, v);
            }
            Put(p, tiny, tx, sp_top, QStringLiteral("RECENT 14 DAYS"), l.muted);
            Put(p, tiny, tx + tw, sp_top, have_days > 0 ? QStringLiteral("PEAK · %1").arg(ShortCount(sp_max)) : QStringLiteral("NO SESSIONS"), l.muted, 1);
            const double bar_gap = 4.0;
            const double bar_w = (tw - bar_gap * (sp_n - 1)) / sp_n;
            const double bar_base = sp_top + 38;
            for (int d = 0; d < sp_n; ++d) {
                const double bx = tx + d * (bar_w + bar_gap);
                if (sp_vals[d] <= 0) {
                    p.fillRect(QRectF(bx, bar_base - 2, bar_w, 2), Faded(l.muted, 0.22));
                } else {
                    const double bh = std::max(4.0, 28.0 * sp_vals[d] / sp_max);
                    p.fillRect(QRectF(bx, bar_base - bh, bar_w, bh), sp_vals[d] == sp_max ? l.accent : Faded(l.ink, 0.56));
                }
            }

            // Actions: CONTINUE (accent) + BACKLOG (ghost, when lines exist).
            const bool has_log = reading.line_count > 0;
            const double btn_w = has_log ? (tw - 10) * 0.56 : tw;
            const QRectF key(tx, box.bottom() - 54, btn_w, 38);
            p.fillRect(key, ShelfHovered(3, 0) ? Faded(l.accent, 0.84) : l.accent);
            if (open > 0.5) {
                shelf_hits.append({key, 3, 0});
            }
            const QFont key_face = ShelfFont(l.display, 11, 0.16);
            Put(p, key_face, key.center().x(), key.center().y() + 4.0, read > 0 ? QStringLiteral("CONTINUE") : QStringLiteral("START"), Qt::white, 2);
            if (has_log) {
                const QRectF bkey(key.right() + 10, box.bottom() - 54, tw - btn_w - 10, 38);
                p.setPen(QPen(ShelfHovered(24, 0) ? l.ink : l.rule, 1));
                p.setBrush(Qt::NoBrush);
                p.drawRect(bkey.adjusted(0.5, 0.5, -0.5, -0.5));
                if (open > 0.5) {
                    shelf_hits.append({bkey, 24, 0});
                }
                Put(p, key_face, bkey.center().x(), bkey.center().y() + 4.0, QStringLiteral("BACKLOG"), l.ink, 2);
            }
            p.restore();
        }
        // Its edge: a hairline, the open one's in ink.
        p.setPen(QPen(open > 0.5 ? l.ink : l.rule, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    }

    // The spine that adds a title: a plus and those words down it; open, the three ways in
    // and the key that begins.
    if (const QRectF box(left, ShelfTop, widths[count], height); box.right() >= 0 && box.left() <= shelf_right) {
        double open = std::max(0.0, 1 - std::fabs(count - shelf_at));
        open = open * open * (3 - 2 * open);
        shelf_hits.append({box, 1, count});
        if (open < 0.98) {
            p.save();
            p.setClipRect(box);
            const double seen = 1 - open;
            Put(p, ShelfFont(l.text, 17), box.center().x(), box.top() + 31, QStringLiteral("＋"), Faded(l.ink, seen), 2);
            const QFont face = ShelfFont(l.display, 12.5, 0.06);
            p.translate(box.center().x(), box.top() + 46);
            p.rotate(90);
            Put(p, face, 0, 4.4, QStringLiteral("ADD GAME"), Faded(l.muted, seen));
            p.restore();
        }
        if (open > 0.02) {
            p.save();
            p.setClipRect(box);
            p.setOpacity(p.opacity() * open);
            const double tx = box.left() + 28, tw = SpineOpen - 56;
            const double at = tx + Put(p, small, tx, box.top() + 36, QStringLiteral("＋"), l.accent);
            Put(p, small, at, box.top() + 36, count == 0 ? QStringLiteral(" · LIBRARY IS EMPTY") : QStringLiteral(" · NEW"), l.muted);
            Put(p, ShelfFont(l.display, 32, 0.02), tx, box.top() + 104, QStringLiteral("ADD GAME"), l.ink);
            // Games found in the game folders that no script reads are not on the shelf: say so,
            // and where scripts come from (an empty library said nothing, 2026-11-21).
            QString hint;
            if (!unread_games.isEmpty()) {
                hint = unread_games.size() == 1
                           ? QStringLiteral("1 game in your game folders has no game script yet.")
                           : QStringLiteral("%1 games in your game folders have no game script yet.").arg(unread_games.size());
                hint += QStringLiteral(" Install game scripts in Settings › Game scripts, or add a game to read by OCR.");
            } else if (count == 0) {
                hint = QStringLiteral("Install game scripts in Settings › Game scripts, then choose the folder "
                                      "your games are in.");
            }
            if (!hint.isEmpty()) {
                p.setFont(ShelfFont(l.mono, 11.5, 0.03));
                p.setPen(l.muted);
                p.drawText(QRectF(tx, box.top() + 132, tw, box.bottom() - 190 - (box.top() + 132)),
                           Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, hint);
            }
            const QList<QPair<QString, QString>> doors = {{QStringLiteral("DISC OR FILE"), QStringLiteral("EMULATOR")},
                                                          {QStringLiteral("GAME WINDOW"), QStringLiteral("TEXT HOOK · OCR")},
                                                          {QStringLiteral("TEXTHOOKER"), QStringLiteral("OR CLIPBOARD")}};
            double y = box.bottom() - 150;
            for (int d = 0; d < doors.size(); ++d) {
                p.fillRect(QRectF(tx, y - 17, tw, 1), l.rule);
                const double name = tx + Put(p, tiny, tx, y, QString(QChar('A' + d)), l.accent);
                Put(p, ShelfFont(l.display, 10.5, 0.08), name + 18, y, doors[d].first, l.ink);
                Put(p, tiny, tx + tw, y, doors[d].second, l.muted, 1);
                y += 26;
            }
            const QRectF key(tx, box.bottom() - 62, tw, 36);
            p.fillRect(key, l.ink);
            Put(p, ShelfFont(l.display, 12, 0.16), key.center().x(), key.center().y() + 4.4, QStringLiteral("ADD"), l.ground, 2);
            p.restore();
        }
        p.setPen(QPen(open > 0.5 ? l.ink : l.rule, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(box.adjusted(0.5, 0.5, -0.5, -0.5));
    }

    p.restore();

    // The floors: halo-only console rail on the right (no hard vertical bar).
    const QStringList sections = ShelfSections();
    const QFont floor_font = ShelfFont(l.display, 10.5, 0.11);
    const double floor_left = ShelfRight - FloorColumn + 22;
    const bool light_ground = l.ground.lightness() > 140;
    for (int i = 0; i <= sections.size(); ++i) {
        const double y = ShelfTop + 10 + i * FloorPitch;
        const bool here = i == shelf_section;
        const QString name = i == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections[i - 1]).toUpper();
        const QString fit_name = Paint::Fit(name, floor_font, FloorColumn - 52);
        if (here) {
            PutHalo(p, floor_font, ShelfRight - 4, y + 22, fit_name, l.ink, l.accent, dots_time, light_ground, 1);
        } else {
            Put(p, floor_font, ShelfRight - 4, y + 22, fit_name, ShelfHovered(10, i) ? l.ink : Faded(l.muted, 0.72), 1);
        }
        Put(p, tiny, floor_left, y + 22, QString::number(SectionGames(i).size()), Faded(here ? l.ink : l.muted, 0.65));
        shelf_hits.append({QRectF(floor_left - 8, y, ShelfRight - floor_left + 8, FloorPitch - 2), 10, i});
    }

    p.restore();
    // The foot: which shelf this is, and the keys; or what the page has to say.
    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    if (shelf_sorting) {
        PaintSortLine(p, 674);
        return;
    }
    double x = ShelfLeft;
    x += Put(p, small, x, 674, shelf_section == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections.value(shelf_section - 1)).toUpper(), l.ink);
    Put(p, small, x, 674, QStringLiteral(" · %1 OF %2").arg(shelf_section + 1).arg(sections.size() + 1), l.muted);
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ CONSOLE"), Qt::Key_Down}, {QStringLiteral("←"), Qt::Key_Left},
             {QStringLiteral("→ BROWSE"), Qt::Key_Right}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return}, {QStringLiteral("Q / E TAB"), Qt::Key_E},
             {QStringLiteral("ESC QUIT"), Qt::Key_Escape}});
}

void HomeWindow::PaintLibraryCaseStage(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const bool light_ground = l.ground.lightness() > 140;
    const QList<ShelfTitle>& titles = ShelfTitles();
    const int count = static_cast<int>(titles.size());
    const QStringList sections = ShelfSections();
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont mono = ShelfFont(l.mono, 11.5, 0.08);
    const QFont row_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 14.5);
    const QFont key_face = ShelfFont(l.display, 11, 0.16);
    const double w_all = ShelfRight - ShelfLeft;

    p.save();
    p.translate(tab_body_dx, 0);

    // ---- Top Hero Stage (y = 84..292) ----
    if (shelf_choice < count) {
        const ShelfTitle& title = titles[shelf_choice];
        const Game& game = title.game;
        const QString console = SystemLabel(library, title.system).toUpper();
        const QPixmap cover = TitleCover(game);
        const QRectF plate(ShelfLeft, 86, 174, 198);
        if (cover.isNull()) {
            Paint::PaintFill(p, plate, SystemArt(title.system));
        } else {
            const double sc = std::min(plate.width() / cover.width(), plate.height() / cover.height());
            const QSizeF sz(cover.width() * sc, cover.height() * sc);
            const QRectF dst(plate.left() + (plate.width() - sz.width()) / 2, plate.top() + (plate.height() - sz.height()) / 2, sz.width(), sz.height());
            p.drawPixmap(dst, cover, QRectF(0, 0, cover.width(), cover.height()));
            PaintCornerTicks(p, dst, l.accent, 6.0);
        }
        p.setPen(QPen(l.rule, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(plate.adjusted(0.5, 0.5, -0.5, -0.5));

        const double ix = plate.right() + 28;
        const double iw = ShelfRight - 220 - ix - 20;
        const TrackerTitle* tracked = TrackerOf(game.id);
        const qint64 read = tracked != nullptr ? tracked->read : game.chars;
        const qint64 spent = tracked != nullptr && tracked->hours > 0 ? static_cast<qint64>(tracked->hours * 3.6e6) : game.active_ms;
        const QString day = tracked != nullptr ? std::max(tracked->last, game.last_day) : game.last_day;
        const int pct = tracked != nullptr && tracked->total > 0 ? std::clamp(static_cast<int>(std::lround(100.0 * read / tracked->total)), 0, 100) : -1;

        Put(p, tiny, ix, 104, QStringLiteral("%1 / %2 · %3 · %4").arg(shelf_choice + 1, 2, 10, QLatin1Char('0')).arg(count, 2, 10, QLatin1Char('0')).arg(console, game.id.toUpper()), l.muted);
        const QString said = ShelfTitleOf(game.id, game.title);
        const bool jp = HasJapanese(said);
        const QFont tface = jp ? ShelfFont(l.text, 28) : ShelfFont(l.display, 26, 0.03);
        PutHalo(p, tface, ix, 146, Paint::Fit(jp ? said : said.toUpper(), tface, iw), l.ink, l.accent, dots_time, light_ground);
        if (!jp && HasJapanese(game.title)) {
            const QFont sub = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 13);
            Put(p, sub, ix, 170, Paint::Fit(ShelfName(game.title), sub, iw), l.muted);
        }

        const QString meta = QStringLiteral("%1 CH%2 · %3 · LAST %4")
                                 .arg(CountText(read), pct >= 0 ? QStringLiteral(" (%1%)").arg(pct) : QString(), PlayClock(spent),
                                      day.isEmpty() ? QStringLiteral("—") : DayText(day, true).toUpper());
        Put(p, mono, ix, 208, Paint::Fit(meta, mono, iw), l.ink);
        const int dots_n = 28;
        const int lit_dots = pct >= 0 ? std::clamp(pct * dots_n / 100, 0, dots_n)
                                      : (read > 0 ? std::clamp(static_cast<int>(std::min<qint64>(read / 10000 + 1, dots_n)), 1, dots_n) : 0);
        p.save();
        p.setPen(Qt::NoPen);
        for (int d = 0; d < dots_n; ++d) {
            p.setBrush(d < lit_dots ? (d >= std::max(0, lit_dots - 4) ? l.accent : l.ink) : Faded(l.muted, 0.26));
            p.drawEllipse(QPointF(ix + 3 + d * 11.0, 232), 2.8, 2.8);
        }
        p.restore();
        const TitleReading& reading = ReadingOf(game.id);
        if (!reading.lines.isEmpty()) {
            const QFont jp_face = ShelfFont(QStringLiteral("ZenKakuGothicNew-Medium.ttf"), 12.5);
            Put(p, jp_face, ix, 268, Paint::Fit(reading.lines.last().text, jp_face, iw), Faded(l.ink, 0.72));
        }

        // Right CTA stack
        const QRectF ckey(ShelfRight - 210, 134, 210, 42);
        p.fillRect(ckey, ShelfHovered(3, 0) ? Faded(l.accent, 0.84) : l.accent);
        shelf_hits.append({ckey, 3, 0});
        Put(p, key_face, ckey.center().x(), ckey.center().y() + 4.0, read > 0 ? QStringLiteral("CONTINUE") : QStringLiteral("START"), Qt::white, 2);
        if (reading.line_count > 0) {
            const QRectF bkey(ShelfRight - 210, 188, 210, 40);
            p.setPen(QPen(ShelfHovered(24, 0) ? l.ink : l.rule, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(bkey.adjusted(0.5, 0.5, -0.5, -0.5));
            shelf_hits.append({bkey, 24, 0});
            Put(p, key_face, bkey.center().x(), bkey.center().y() + 4.0, QStringLiteral("BACKLOG"), l.ink, 2);
        }
    } else {
        PutHalo(p, ShelfFont(l.display, 26, 0.03), ShelfLeft, 152, QStringLiteral("ADD GAME"), l.ink, l.accent, dots_time, light_ground);
        Put(p, mono, ShelfLeft, 184, QStringLiteral("Press Enter to add a game from file, window, or texthooker"), l.muted);
    }
    p.fillRect(QRectF(ShelfLeft, 298, w_all, 1), l.rule);

    // ---- Horizontal Studio-Case List (y = 308..632, up to 7 visible rows) ----
    const int total = count + 1; // +1 for Add Game
    const int shown = 7;
    const int first = std::clamp(shelf_choice - shown / 2, 0, std::max(0, total - shown));
    const double row_h = 44.0;
    for (int r = 0; r < shown && first + r < total; ++r) {
        const int idx = first + r;
        const double ry = 310.0 + r * row_h;
        const bool on = idx == shelf_choice;
        const QRectF area(ShelfLeft, ry, w_all, row_h);
        if (on) {
            QLinearGradient wash(ShelfLeft, ry, ShelfRight, ry);
            wash.setColorAt(0.0, Faded(l.accent, 0.13));
            wash.setColorAt(0.65, Faded(l.accent, 0.04));
            wash.setColorAt(1.0, Faded(l.accent, 0.0));
            p.fillRect(area, wash);
            if (l.beam) {
                PaintBeam(p, ShelfLeft - 8, ShelfRight + 8, ry + row_h * 0.5, l.ink, l.accent, dots_time);
            }
        }
        const QString num = QStringLiteral("%1").arg(idx + 1, 2, 10, QLatin1Char('0'));
        if (on) {
            PutHalo(p, tiny, ShelfLeft + 10, ry + 26, num, l.ink, l.accent, dots_time, light_ground);
        } else {
            Put(p, tiny, ShelfLeft + 10, ry + 26, num, l.muted);
        }
        if (idx < count) {
            const ShelfTitle& t = titles[idx];
            const Game& g = t.game;
            const QString sys = SystemLabel(library, t.system).toUpper();
            Put(p, ShelfFont(l.display, 9.5, 0.08), ShelfLeft + 48, ry + 26, sys, l.muted);
            const QRectF pill(ShelfLeft + 110, ry + 8, w_all - 410, 28);
            p.fillRect(pill, Faded(l.raised, 0.65));
            p.setPen(QPen(Faded(l.rule, 0.85), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(pill.adjusted(0.5, 0.5, -0.5, -0.5));
            const SpineStyle* st = SpineStyleOf(t.system);
            p.fillRect(QRectF(pill.left(), pill.top(), 3.5, pill.height()), st != nullptr ? st->body : l.accent);
            const QString label = Paint::Fit(ShelfTitleOf(g.id, g.title), row_face, pill.width() - 120);
            if (on) {
                PutHalo(p, row_face, pill.left() + 14, ry + 27, label, l.ink, l.accent, dots_time, light_ground);
            } else {
                Put(p, row_face, pill.left() + 14, ry + 27, label, Faded(l.ink, 0.80));
            }
            Put(p, tiny, pill.right() - 10, ry + 26, g.id.toUpper(), l.muted, 1);

            const TrackerTitle* tr = TrackerOf(g.id);
            const qint64 ch = tr != nullptr ? tr->read : g.chars;
            const qint64 ms = tr != nullptr && tr->hours > 0 ? static_cast<qint64>(tr->hours * 3.6e6) : g.active_ms;
            const QString dy = tr != nullptr ? std::max(tr->last, g.last_day) : g.last_day;
            Put(p, mono, ShelfRight - 170, ry + 26, ch > 0 ? CountText(ch) + QStringLiteral(" CH") : QStringLiteral("—"), on ? l.ink : l.muted, 1);
            Put(p, mono, ShelfRight - 6, ry + 26, dy.isEmpty() ? PlayClock(ms) : QStringLiteral("%1 · %2").arg(DayText(dy, true).toUpper(), PlayClock(ms)), l.muted, 1);
        } else {
            const QString add_lbl = QStringLiteral("＋ ADD GAME");
            if (on) {
                PutHalo(p, row_face, ShelfLeft + 110, ry + 27, add_lbl, l.ink, l.accent, dots_time, light_ground);
            } else {
                Put(p, row_face, ShelfLeft + 110, ry + 27, add_lbl, l.muted);
            }
        }
        p.fillRect(QRectF(ShelfLeft, ry + row_h - 1, w_all, 1), Faded(l.rule, 0.6));
        shelf_hits.append({area, 1, idx});
    }

    p.restore();
    p.fillRect(QRectF(ShelfLeft, 646, w_all, 1), l.rule);
    double x = ShelfLeft;
    x += Put(p, small, x, 674, shelf_section == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections.value(shelf_section - 1)).toUpper(), l.ink);
    Put(p, small, x, 674, QStringLiteral(" · %1 OF %2").arg(shelf_section + 1).arg(sections.size() + 1), l.muted);
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ GAME"), Qt::Key_Down}, {QStringLiteral("←"), Qt::Key_Left},
             {QStringLiteral("→ CONSOLE"), Qt::Key_Right}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return}, {QStringLiteral("Q / E TAB"), Qt::Key_E},
             {QStringLiteral("ESC QUIT"), Qt::Key_Escape}});
}

void HomeWindow::PaintLibraryCoverFlow(QPainter& p) {
    const ShelfLook& l = *shelf_look;
    const bool light_ground = l.ground.lightness() > 140;
    const QList<ShelfTitle>& titles = ShelfTitles();
    const int count = static_cast<int>(titles.size());
    const QStringList sections = ShelfSections();
    const QFont small = ShelfFont(l.mono, 11, 0.14);
    const QFont tiny = ShelfFont(l.mono, 9.5, 0.14);
    const QFont mono = ShelfFont(l.mono, 11.5, 0.08);
    const QFont key_face = ShelfFont(l.display, 11, 0.16);
    const double area_w = ShelfRight - FloorColumn - ShelfLeft;
    const double center_x = ShelfLeft + area_w * 0.5;

    p.save();
    p.translate(tab_body_dx, 0);

    // ---- Floating Monolith Cover Cards (y = 96..456) ----
    const int total = count + 1;
    for (int offset : {-2, 2, -1, 1, 0}) {
        const int idx = shelf_choice + offset;
        if (idx < 0 || idx >= total) {
            continue;
        }
        const bool active = offset == 0;
        const double cw = active ? 244.0 : 172.0;
        const double ch = active ? 350.0 : 246.0;
        const double cx = center_x + offset * (active ? 0.0 : (std::abs(offset) == 1 ? 228.0 : 412.0));
        const double cy = 274.0;
        const QRectF card(cx - cw * 0.5, cy - ch * 0.5, cw, ch);
        p.save();
        p.setOpacity(p.opacity() * (active ? 1.0 : (std::abs(offset) == 1 ? 0.58 : 0.30)));
        if (idx < count) {
            const Game& g = titles[idx].game;
            const QPixmap cover = TitleCover(g);
            p.fillRect(card, l.raised);
            if (cover.isNull()) {
                Paint::PaintFill(p, card, SystemArt(titles[idx].system));
            } else {
                const double sc = std::min(card.width() / cover.width(), card.height() / cover.height());
                const QSizeF sz(cover.width() * sc, cover.height() * sc);
                const QRectF dst(card.left() + (card.width() - sz.width()) / 2, card.top() + (card.height() - sz.height()) / 2, sz.width(), sz.height());
                p.drawPixmap(dst, cover, QRectF(0, 0, cover.width(), cover.height()));
            }
            p.setPen(QPen(active ? l.accent : l.rule, active ? 1.5 : 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(card.adjusted(0.5, 0.5, -0.5, -0.5));
            if (active) {
                PaintCornerTicks(p, card, l.accent, 9.0);
            }
        } else {
            p.fillRect(card, Faded(l.raised, 0.72));
            p.setPen(QPen(active ? l.accent : l.rule, 1));
            p.drawRect(card.adjusted(0.5, 0.5, -0.5, -0.5));
            Put(p, ShelfFont(l.display, active ? 16 : 12, 0.12), card.center().x(), card.center().y() + 4, QStringLiteral("＋ ADD GAME"), l.ink, 2);
        }
        p.restore();
        shelf_hits.append({card, 1, idx});
    }

    // ---- Minimal Telemetry Strip Below (y = 482..626) ----
    p.fillRect(QRectF(ShelfLeft, 480, area_w, 1), l.rule);
    if (l.beam) {
        PaintBeam(p, ShelfLeft, ShelfLeft + area_w, 480.5, l.ink, l.accent, dots_time);
    }
    if (shelf_choice < count) {
        const ShelfTitle& t = titles[shelf_choice];
        const Game& g = t.game;
        const TrackerTitle* tr = TrackerOf(g.id);
        const qint64 read = tr != nullptr ? tr->read : g.chars;
        const qint64 spent = tr != nullptr && tr->hours > 0 ? static_cast<qint64>(tr->hours * 3.6e6) : g.active_ms;
        const int pct = tr != nullptr && tr->total > 0 ? std::clamp(static_cast<int>(std::lround(100.0 * read / tr->total)), 0, 100) : -1;
        const QString sys = SystemLabel(library, t.system).toUpper();
        Put(p, tiny, ShelfLeft, 504, QStringLiteral("%1 / %2 · %3 · %4").arg(shelf_choice + 1, 2, 10, QLatin1Char('0')).arg(count, 2, 10, QLatin1Char('0')).arg(sys, g.id.toUpper()), l.muted);
        const QString said = ShelfTitleOf(g.id, g.title);
        const bool jp = HasJapanese(said);
        const QFont tface = jp ? ShelfFont(l.text, 28) : ShelfFont(l.display, 26, 0.03);
        PutHalo(p, tface, ShelfLeft, 546, Paint::Fit(jp ? said : said.toUpper(), tface, area_w - 360), l.ink, l.accent, dots_time, light_ground);

        const int dots_n = 20;
        const int lit_dots = pct >= 0 ? std::clamp(pct * dots_n / 100, 0, dots_n)
                                      : (read > 0 ? std::clamp(static_cast<int>(std::min<qint64>(read / 10000 + 1, dots_n)), 1, dots_n) : 0);
        p.save();
        p.setPen(Qt::NoPen);
        for (int d = 0; d < dots_n; ++d) {
            p.setBrush(d < lit_dots ? (d >= std::max(0, lit_dots - 3) ? l.accent : l.ink) : Faded(l.muted, 0.26));
            p.drawEllipse(QPointF(ShelfLeft + 3 + d * 10.5, 578), 2.7, 2.7);
        }
        p.restore();
        Put(p, mono, ShelfLeft + dots_n * 10.5 + 16, 582,
            QStringLiteral("%1 CH%2 · %3").arg(CountText(read), pct >= 0 ? QStringLiteral(" · %1%").arg(pct) : QString(), PlayClock(spent)), l.muted);

        const TitleReading& reading = ReadingOf(g.id);
        const bool has_log = reading.line_count > 0;
        const QRectF ckey(ShelfLeft + area_w - (has_log ? 316 : 186), 532, 180, 42);
        p.fillRect(ckey, ShelfHovered(3, 0) ? Faded(l.accent, 0.84) : l.accent);
        shelf_hits.append({ckey, 3, 0});
        Put(p, key_face, ckey.center().x(), ckey.center().y() + 4.0, read > 0 ? QStringLiteral("CONTINUE") : QStringLiteral("START"), Qt::white, 2);
        if (has_log) {
            const QRectF bkey(ckey.right() + 10, 532, 126, 42);
            p.setPen(QPen(ShelfHovered(24, 0) ? l.ink : l.rule, 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(bkey.adjusted(0.5, 0.5, -0.5, -0.5));
            shelf_hits.append({bkey, 24, 0});
            Put(p, key_face, bkey.center().x(), bkey.center().y() + 4.0, QStringLiteral("BACKLOG"), l.ink, 2);
        }
    } else {
        PutHalo(p, ShelfFont(l.display, 26, 0.03), ShelfLeft, 546, QStringLiteral("ADD GAME"), l.ink, l.accent, dots_time, light_ground);
    }

    // Halo-only console rail on the right
    const QFont floor_font = ShelfFont(l.display, 10.5, 0.11);
    const double floor_left = ShelfRight - FloorColumn + 22;
    for (int i = 0; i <= sections.size(); ++i) {
        const double y = ShelfTop + 10 + i * FloorPitch;
        const bool here = i == shelf_section;
        const QString name = i == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections[i - 1]).toUpper();
        const QString fit_name = Paint::Fit(name, floor_font, FloorColumn - 52);
        if (here) {
            PutHalo(p, floor_font, ShelfRight - 4, y + 22, fit_name, l.ink, l.accent, dots_time, light_ground, 1);
        } else {
            Put(p, floor_font, ShelfRight - 4, y + 22, fit_name, ShelfHovered(10, i) ? l.ink : Faded(l.muted, 0.72), 1);
        }
        Put(p, tiny, floor_left, y + 22, QString::number(SectionGames(i).size()), Faded(here ? l.ink : l.muted, 0.65));
        shelf_hits.append({QRectF(floor_left - 8, y, ShelfRight - floor_left + 8, FloorPitch - 2), 10, i});
    }

    p.restore();
    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    double x = ShelfLeft;
    x += Put(p, small, x, 674, shelf_section == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections.value(shelf_section - 1)).toUpper(), l.ink);
    Put(p, small, x, 674, QStringLiteral(" · %1 OF %2").arg(shelf_section + 1).arg(sections.size() + 1), l.muted);
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("←"), Qt::Key_Left}, {QStringLiteral("→ GAME"), Qt::Key_Right}, {QStringLiteral("↑"), Qt::Key_Up},
             {QStringLiteral("↓ CONSOLE"), Qt::Key_Down}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return}, {QStringLiteral("Q / E TAB"), Qt::Key_E},
             {QStringLiteral("ESC QUIT"), Qt::Key_Escape}});
}
