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
    font.setWeight(static_cast<QFont::Weight>(face.weight));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(px))));
    font.setHintingPreference(QFont::PreferNoHinting); // it is drawn at the window's scale
    font.setStyleStrategy(QFont::NoSubpixelAntialias); // turned on its side, a screen's colour fringes show
    if (spacing != 0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, px * spacing);
    }
    return font;
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
    static const QRegularExpression tail(QStringLiteral("\\s*[（(][^（()）]*[)）]\\s*$"));
    for (int i = 0; i < 3; ++i) {
        const QString shorter = QString(name).remove(tail).trimmed();
        if (shorter.isEmpty() || shorter == name) {
            break;
        }
        name = shorter;
    }
    return name;
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
    const auto starts = [&](const char* pattern) { return QRegularExpression(QStringLiteral("^(?:%1)").arg(QLatin1String(pattern))).match(i).hasMatch(); };
    const auto whole = [&](const char* pattern) { return QRegularExpression(QStringLiteral("^(?:%1)$").arg(QLatin1String(pattern))).match(i).hasMatch(); };
    if (starts("ULJ|NPJ|UCJ|ULA")) return QStringLiteral("psp");
    if (starts("PCS")) return QStringLiteral("psv");
    if (whole("0004[0-9A-F]{12}")) return QStringLiteral("n3d");
    if (whole("[0-9A-F]{16}")) return QStringLiteral("swi");
    if (starts("SLPS-?2|SLPM-?[5-7]|SCPS-?[15-7]|SLPS-?7")) return QStringLiteral("ps2");
    if (starts("SLPS-?[0189]|SLPM-?8|SCPS-?[14]0")) return QStringLiteral("ps1");
    if (starts("T-?\\d|HDR-|MK-") || whole("\\d{7}")) return QStringLiteral("drc");
    return {};
}

/// A title's box spine for the console of this game (`<key>.<platform>.jpg`, tools/titles.py spines).
QPixmap SpineOf(const QString& key, const QString& game_id) {
    static QHash<QString, QPixmap> spines;
    const QString platform = ArtPlatform(game_id);
    const QString name = key + QLatin1Char('.') + platform;
    auto found = spines.find(name);
    if (found == spines.end()) {
        found = spines.insert(name, key.isEmpty() || platform.isEmpty() ? QPixmap() : QPixmap(QStringLiteral(":/spines/%1.jpg").arg(name)));
    }
    return *found;
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

/// How a console's Japanese boxes set their spines, for the spines made here where no picture of
/// the real one is had (the maintainer, 2026-11-10: "generic spine art matching the look of that
/// console's spine art"). Their colours, bands and type only: nothing of a console maker's (no
/// logo, wordmark or mark of a rating board) is drawn, as everywhere in Furoshiki.
struct SpineStyle {
    double shape;        ///< Width to height, as the real case's spine
    QColor body, ink;    ///< The spine, and the title on it
    QColor band, band_ink; ///< The band at its head with the console's name in plain type
    double band_share;   ///< How much of the height the band takes (0: none)
    const char* name;    ///< Said in the band, or at the head where there is none
    QColor foot;         ///< A band at its foot (alpha 0: none), for the serial
    QColor foot_ink;
    bool serial;         ///< The game's ID at the foot, as the real ones print it
};

const SpineStyle* SpineStyleOf(const QString& platform) {
    static const QHash<QString, SpineStyle> styles = {
        // A red case's red insert: the title in white, no band.
        {QStringLiteral("swi"), {0.066, QColor(0xe6, 0x00, 0x12), Qt::white, QColor(0xe6, 0x00, 0x12), Qt::white, 0.0, "SWITCH", QColor(0, 0, 0, 0), Qt::white, false}},
        // The black head over a white spine, the serial at the foot.
        {QStringLiteral("ps2"), {0.075, QColor(0xf4, 0xf4, 0xf2), QColor(0x14, 0x14, 0x14), QColor(0x0c, 0x0c, 0x0c), Qt::white, 0.16, "PlayStation 2", QColor(0xf4, 0xf4, 0xf2), QColor(0x14, 0x14, 0x14), true}},
        {QStringLiteral("ps1"), {0.05, QColor(0xf2, 0xf2, 0xf0), QColor(0x14, 0x14, 0x14), QColor(0x0c, 0x0c, 0x0c), Qt::white, 0.17, "PlayStation", QColor(0xf2, 0xf2, 0xf0), QColor(0x14, 0x14, 0x14), true}},
        {QStringLiteral("psp"), {0.082, QColor(0xf4, 0xf4, 0xf2), QColor(0x14, 0x14, 0x14), QColor(0x0c, 0x0c, 0x0c), Qt::white, 0.13, "PSP", QColor(0x1c, 0x2f, 0x6b), Qt::white, true}},
        {QStringLiteral("psv"), {0.08, QColor(0xf4, 0xf4, 0xf2), QColor(0x14, 0x14, 0x14), QColor(0x0c, 0x0c, 0x0c), Qt::white, 0.13, "PS VITA", QColor(0xf4, 0xf4, 0xf2), QColor(0x14, 0x14, 0x14), true}},
        // White with the orange head of Japan's Dreamcast boxes.
        {QStringLiteral("drc"), {0.06, QColor(0xf6, 0xf5, 0xf1), QColor(0x14, 0x14, 0x14), QColor(0xf2, 0x6b, 0x1d), Qt::white, 0.15, "Dreamcast", QColor(0xf6, 0xf5, 0xf1), QColor(0x14, 0x14, 0x14), true}},
        // White, the console's name in red at the head.
        {QStringLiteral("n3d"), {0.07, QColor(0xf6, 0xf6, 0xf4), QColor(0x14, 0x14, 0x14), QColor(0xf6, 0xf6, 0xf4), QColor(0xd7, 0x0f, 0x1f), 0.12, "3DS", QColor(0xf6, 0xf6, 0xf4), QColor(0x14, 0x14, 0x14), true}},
        // A Japanese PC game's box: a dark spine, the title in white, the maker's band left plain.
        {QStringLiteral("pc"), {0.11, QColor(0x1c, 0x1e, 0x24), Qt::white, QColor(0x0c, 0x0d, 0x10), QColor(0xd8, 0xd8, 0xd4), 0.08, "PC", QColor(0, 0, 0, 0), Qt::white, false}},
        {QStringLiteral("ps3"), {0.075, QColor(0x14, 0x14, 0x16), Qt::white, QColor(0x05, 0x05, 0x06), Qt::white, 0.12, "PS3", QColor(0, 0, 0, 0), Qt::white, false}},
    };
    const auto found = styles.constFind(platform);
    return found == styles.constEnd() ? nullptr : &*found;
}

/// A spine in its console's look: the band, the title set down it as a Japanese book's is, the
/// serial at its foot. 1000 high, as the fetched ones are kept.
QImage MadeSpine(const SpineStyle& style, const QString& title, const QString& serial) {
    const int height = 1000;
    const int width = std::max(40, static_cast<int>(std::lround(height * style.shape)));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(style.body);
    QPainter p(&image);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    double top = 0;
    // The console's name, lying down along the band, in plain type.
    const double band = height * style.band_share;
    if (band > 0) {
        p.fillRect(QRectF(0, 0, width, band), style.band);
        p.save();
        p.translate(width / 2.0, band / 2.0);
        p.rotate(90);
        p.setFont(ShelfFont(QStringLiteral("ZenKakuGothicNew-Bold.ttf"), width * 0.42));
        p.setPen(style.band_ink);
        p.drawText(QRectF(-band / 2 + 6, -width / 2.0, band - 12, width), Qt::AlignCenter, QLatin1String(style.name));
        p.restore();
        top = band;
    } else {
        // No band: the console's name small at the head, in the title's colour.
        p.save();
        p.translate(width / 2.0, 22);
        p.rotate(90);
        p.setFont(ShelfFont(QStringLiteral("ZenKakuGothicNew-Bold.ttf"), width * 0.3));
        p.setPen(style.ink);
        p.drawText(QPointF(0, width * 0.11), QLatin1String(style.name));
        p.restore();
        top = 22 + Advance(ShelfFont(QStringLiteral("ZenKakuGothicNew-Bold.ttf"), width * 0.3), QLatin1String(style.name));
    }
    // The foot: the serial, as on the real ones.
    const double foot = style.serial ? height * 0.12 : height * 0.05;
    if (style.foot.alpha() > 0) {
        p.fillRect(QRectF(0, height - foot, width, foot), style.foot);
    }
    if (style.serial && !serial.isEmpty()) {
        p.setFont(ShelfFont(QStringLiteral("IBMPlexMono-Medium.ttf"), width * 0.2));
        p.setPen(style.foot_ink);
        QString printed = serial.toUpper();
        printed.replace(QLatin1Char('-'), QLatin1Char('\n'));
        p.drawText(QRectF(0, height - foot, width, foot), Qt::AlignCenter, printed);
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
    // The case's edges, a little darker than the spine.
    p.fillRect(QRectF(0, 0, 1.5, height), QColor(0, 0, 0, 40));
    p.fillRect(QRectF(width - 1.5, 0, 1.5, height), QColor(0, 0, 0, 40));
    return image;
}

/// The art code of a console as the library names it (a title read from lines has no ID that says).
QString PlatformOfSystem(const QString& system) {
    static const QHash<QString, QString> known{{QStringLiteral("PC"), QStringLiteral("pc")},
                                               {QStringLiteral("Nintendo Switch"), QStringLiteral("swi")},
                                               {QStringLiteral("Nintendo 3DS"), QStringLiteral("n3d")},
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

QString HomeWindow::AddLinesTitle(const QString& by, const QString& title) {
    const QString folder = SharedFolder();
    Lines::Sources sources = Lines::Load(folder);
    const QString id = Lines::Add(sources, title, by, RecordNamed(title.trimmed()));
    if (id.isEmpty() || !Lines::Save(folder, sources)) {
        sounds.Play(Sound::Refuse);
        Say(id.isEmpty() ? QStringLiteral("A title read from lines wants a name, and to be read by WebSocket or from the clipboard")
                         : QStringLiteral("The titles read from lines could not be written"));
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
    Say(QStringLiteral("%1 is on the shelf, read %2").arg(title.trimmed(), by == QStringLiteral("clipboard") ? QStringLiteral("from the clipboard")
                                                                                                                 : QStringLiteral("from ") + sources.address));
    return id;
}

void HomeWindow::SetLinesAddress(const QString& address) {
    const QUrl url(address.trimmed());
    if (!url.isValid() || url.scheme() != QStringLiteral("ws") || url.host().isEmpty()) {
        sounds.Play(Sound::Refuse);
        Say(QStringLiteral("\"%1\" is no WebSocket address: ws://host:port").arg(address));
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
    shelf_on = settings.value(QStringLiteral("look/shape")).toString() != QStringLiteral("save");
    SetShelfTheme(settings.value(QStringLiteral("shelf/theme"), QStringLiteral("mono")).toString(), false);
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
    look->mono = values.value(QStringLiteral("mono")).toString(QStringLiteral("IBMPlexMono-Medium.ttf"));
    shelf_look = look;
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
    {"PLAY TIME", true, "LEAST", "MOST"},
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
    PutKeys(p, tiny, ShelfRight, baseline, {{QStringLiteral("DONE"), Qt::Key_Escape}});
}

double HomeWindow::PutKeys(QPainter& p, const QFont& font, double right, double baseline, const QList<QPair<QString, int>>& keys) {
    const ShelfLook& l = *shelf_look;
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

QPixmap HomeWindow::ShelfSpine(const Game& game) {
    const QPixmap real = SpineOf(RecordOf(game.id).key, game.id);
    if (!real.isNull()) {
        return real;
    }
    const QPixmap found = Cover(Art::SpineFile(SharedFolder(), game.id));
    if (!found.isNull()) {
        return found;
    }
    static QHash<QString, QPixmap> made;
    QString platform = ArtPlatform(game.id);
    if (platform.isEmpty()) {
        platform = PlatformOfSystem(game.system);
    }
    const SpineStyle* style = SpineStyleOf(platform);
    if (style == nullptr) {
        return {};
    }
    const QString key = platform + QLatin1Char('\n') + game.id + QLatin1Char('\n') + game.title;
    auto at = made.find(key);
    if (at == made.end()) {
        at = made.insert(key, QPixmap::fromImage(MadeSpine(*style, ShelfName(game.title), platform == QStringLiteral("swi") || game.id.startsWith(QStringLiteral("LINES-")) ? QString() : game.id)));
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
    art_said = QStringLiteral("LOOKING");
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
            Say(QStringLiteral("No lines of this title have been read on this device yet"));
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
    shelf_pages = on;
    find_text = QString();
    if (!on && pages_back) {
        // They stood over a game: back to it.
        const std::function<void()> back = std::move(pages_back);
        pages_back = nullptr;
        back();
    }
    if (on) {
        // On the page it was on, if that is one of them; else the program's settings.
        const QList<Item> items = Items();
        if (items[std::clamp(axis_choice, 0, static_cast<int>(items.size()) - 1)].kind == Item::System) {
            axis_choice = static_cast<int>(items.size()) - 1;
        }
    }
    sounds.Play(on ? Sound::Select : Sound::Back);
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
        } else if ((called + QLatin1Char(' ') + name + QLatin1Char(' ') + QLatin1String(row.section) + QLatin1Char(' ') + QLatin1String(row.about))
                       .contains(wanted, Qt::CaseInsensitive)) {
            holds.append(i);
        }
    }
    return starts + holds;
}

void HomeWindow::GoToSetting(int index) {
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
    if (shelf_pages) {
        if (key == Qt::Key_Escape) {
            SetPages(false);
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
            Rename(page.kind == Item::Settings ? page.system : page.kind == Item::Consoles ? QStringLiteral("Consoles") : QStringLiteral("Statistics"),
                   page_rows[page_row].title);
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
        ShelfMove(-1);
        return true;
    case Qt::Key_Right:
        ShelfMove(1);
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
        } else if (shelf_section < ShelfSections().size()) {
            SetShelfSection(shelf_section + 1); // the shelf below
        } else {
            sounds.Play(Sound::Refuse);
        }
        return true;
    case Qt::Key_Up:
        if (shelf_stage) {
            SetStage(false);
        } else if (shelf_section > 0) {
            SetShelfSection(shelf_section - 1); // the shelf above
        } else {
            SetPages(true); // above every shelf: statistics and the settings
        }
        return true;
    case Qt::Key_Escape:
    case Qt::Key_Backspace:
        if (shelf_stage) {
            SetStage(false);
            return true;
        }
        return false;
    case Qt::Key_Q:
    case Qt::Key_E:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
        // Every title, then each console's.
        SetShelfSection(shelf_section + (key == Qt::Key_Q || key == Qt::Key_PageUp ? -1 : 1));
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
                sounds.Play(Sound::Tab);
                update();
            } else if (hit.kind == 6 || hit.kind == 15 || hit.kind == 16) {
                // A click takes a row at once, as Enter does on the row in hand; on the arrows
                // beside its value, as left and right do.
                const QList<Item> items = Items();
                RowChoice(items[axis_choice]) = hit.index;
                pages_seen_row = hit.index; // chosen where it stands: the list does not jump to it
                ShelfKey(hit.kind == 6 ? Qt::Key_Return : hit.kind == 15 ? Qt::Key_Left : Qt::Key_Right);
                update();
            } else if (hit.kind == 17 || hit.kind == 18) {
                pages_drag = hit.kind == 17 ? 1 : 2;
                pages_drag_area = hit.area.adjusted(hit.kind == 17 ? 0 : 0, hit.kind == 17 ? 0 : 0, 0, 0);
                PagesDrag(at);
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
    if (std::fabs(pages_at - (shelf_pages ? 1 : 0)) > 0.001 || std::fabs(log_at - (shelf_log ? 1 : 0)) > 0.001 ||
        std::fabs(wizard_at - (wizard_on ? 1 : 0)) > 0.001) {
        update();
    }
    if (shelf_busy) {
        update();
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
    glide(log_at, shelf_log ? 1 : 0, 0.085);
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
    shelf_minute = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    shelf_hits.clear();
    // The shelf goes up and out as the stage comes up into its place; both go down and out as
    // the pages come down from above.
    // The wizard that adds a title comes over whichever of them it was opened from.
    const double behind = 1 - wizard_at;
    const double under = (1 - pages_at) * behind;
    if (stage_at < 0.999 && under > 0.001) {
        p.save();
        p.setOpacity((1 - stage_at) * under);
        p.translate(0, -14 * stage_at + 16 * pages_at);
        if (!lift_old.isNull() && leave < 1) {
            p.save();
            p.setClipRect(QRectF(0, 66, ShelfRight - FloorColumn + 1, 578));
            p.setOpacity(p.opacity() * (1 - leave));
            p.translate(0, -64 * lift_dir * leave * (2 - leave));
            p.drawImage(QRectF(0, 0, SheetW, SheetH), lift_old, QRectF(0, 0, lift_old.width(), lift_old.height()));
            p.restore();
        }
        PaintShelfBody(p);
        p.restore();
    }
    if (stage_at > 0.001 && under > 0.001 && log_at < 0.999) {
        if (stage_at > 0.5) {
            shelf_hits.clear(); // the stage's own
        }
        p.save();
        p.setOpacity(stage_at * under * (1 - log_at));
        p.translate(0, 18 * (1 - stage_at) + 16 * pages_at - 14 * log_at);
        PaintStage(p);
        p.restore();
    }
    if (log_at > 0.001 && under > 0.001) {
        if (log_at > 0.5) {
            shelf_hits.clear();
        }
        p.save();
        p.setOpacity(log_at * under);
        p.translate(0, 18 * (1 - log_at));
        PaintLog(p);
        p.restore();
    }
    if (pages_at > 0.001 && behind > 0.001) {
        if (pages_at > 0.5) {
            shelf_hits.clear();
        }
        p.save();
        p.setOpacity(pages_at * behind);
        p.translate(0, -18 * (1 - pages_at));
        PaintPages(p);
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
    const QString said = ShelfTitleOf(game.id, game.title).toUpper();
    x += Put(p, small, x, 49, HasJapanese(said) ? QStringLiteral("THE TITLE") : said, ShelfHovered(8, 0) ? l.ink : l.muted);
    Put(p, small, x, 49, QStringLiteral("  ·  LOG"), l.ink);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    Put(p, small, right, 49, QStringLiteral("%1 OF %2 LINES  ·  ").arg(chosen + 1).arg(CountText(reading.line_count)), l.muted, 1);
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
        Put(p, tiny, ShelfLeft + 16, top + 26, when.toString(QStringLiteral("MM/dd  HH:mm")), l.muted);
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
             {QStringLiteral("E EIGHT LINES"), Qt::Key_E}, {QStringLiteral("ENTER COPY"), Qt::Key_Return}, {QStringLiteral("ESC THE TITLE"), Qt::Key_Escape}});
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
    const QString page_name = item.kind == Item::Settings ? item.system
                              : item.kind == Item::Consoles ? QStringLiteral("Consoles") : QStringLiteral("Statistics");
    const auto page_label = [&](const Item& one) { return Labelled(QStringLiteral("Page"), one.label).toUpper(); };

    double x = ShelfLeft;
    x += Put(p, small, x, 49, pages_back ? QStringLiteral("‹  THE GAME") : QStringLiteral("‹  LIBRARY"), Mix(l.muted, l.ink, Ease(7, 0, ShelfHovered(7, 0))));
    Put(p, small, x, 49, QStringLiteral("  ·  ") + page_label(item), l.ink);
    double head_right = ShelfRight;
    head_right -= Put(p, small, head_right, 49, shelf_minute, l.ink, 1);
    if (renaming) {
        Put(p, small, head_right, 49, QStringLiteral("RENAMING LABELS  ·  CLICK A ROW, OR ENTER  ·  "), l.accent, 1);
    }
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);
    shelf_hits.append({QRectF(ShelfLeft, 30, 140, 30), 7, 0});

    // The sections, as words: the one shown in ink, the one under the pointer coming up to it.
    int sections = 0;
    for (const Item& one : items) {
        sections += one.kind == Item::System ? 0 : 1;
    }
    const double pitch = std::min(50.0, (ShelfBottom - ShelfTop - 40) / std::max(1, sections - 1));
    const QFont section_face = ShelfFont(l.display, pitch < 50 ? 24 : 27, 0.02);
    double y = ShelfTop + 44;
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].kind == Item::System) {
            continue;
        }
        const double lit = Ease(5, i, ShelfHovered(5, i));
        const double width = Put(p, section_face, ShelfLeft + 4 * lit * (i == axis_choice ? 0 : 1), y, page_label(items[i]),
                                 i == axis_choice ? l.ink : Mix(Faded(l.muted, 0.7), l.ink, lit));
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
        Put(p, typed, left, ShelfTop + 18, find_text.isEmpty() ? QStringLiteral("FIND A SETTING") : find_text.toUpper(),
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
            Put(p, name, left + 56, row_y, QStringLiteral("Nothing is called that"), l.muted);
        }
        p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
        PutKeys(p, tiny, ShelfRight, 674,
                {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ CHOOSE"), Qt::Key_Down}, {QStringLiteral("ENTER GO TO IT"), Qt::Key_Return},
                 {QStringLiteral("ESC BACK"), Qt::Key_Escape}});
        return;
    }
    // Its rows: what each is, and what it is set to; the one in hand marked. The list stands
    // where the wheel left it; a row taken in hand by the keys is brought into it.
    const int count = static_cast<int>(rows.size());
    const int shown = 12;
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
    p.fillRect(QRectF(left, list_top, right - left, 1), l.rule);
    p.save();
    p.setClipRect(QRectF(left - 2, list_top + 1, right - left + 4, list_tall));
    p.setOpacity(p.opacity() * page_t);
    p.translate(12 * (1 - page_t) * (1 - page_t), 0);
    // The mark of the row in hand, which glides from row to row.
    p.fillRect(QRectF(left, list_top + 1 + (pages_bar_at - pages_first_at) * row_height, 3, row_height - 1), l.accent);
    const int from = std::max(0, static_cast<int>(std::floor(pages_first_at)));
    for (int i = from; i < std::min(count, from + shown + 1); ++i) {
        const Row& row = rows[i];
        const bool on = i == chosen;
        const double base = list_top + 22 + (i - pages_first_at) * row_height;
        const bool over = ShelfHovered(6, i) || ShelfHovered(15, i) || ShelfHovered(16, i);
        const double lit = Ease(6, i, over);
        if (!on && lit > 0) {
            p.fillRect(QRectF(left, base - 21, 3, row_height - 1), Faded(l.accent, 0.4 * lit));
        }
        Put(p, tiny, left + 16, base - 1, QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), on ? l.ink : l.muted);
        const double room = right - left - 56 - 330;
        Put(p, name, left + 56, base, Paint::Fit(Labelled(page_name, row.title), name, room), on ? l.ink : Mix(Faded(l.ink, 0.62), l.ink, lit));
        if (row.yours) {
            // Set by the user: a small square in the accent; Backspace puts the row back.
            p.fillRect(QRectF(left + 42, base - 9, 5, 5), l.accent);
        }
        const QRectF area(left, base - 22, right - left, row_height);
        shelf_hits.append({area.intersected(QRectF(left, list_top, right - left, list_tall)), 6, i});
        const QString said = item.kind == Item::Settings && row.setting >= 0 && IsSetting(row.setting, "Look") ? l.name : row.value;
        // The value, with room at its right for the arrow of a row that left and right change.
        const double value_right = right - 22;
        double value_wide = 0;
        if (!said.isEmpty()) {
            value_wide = Put(p, value, value_right, base - 1, Paint::Fit(said.toUpper(), value, 290), row.dim && !on ? l.muted : on ? l.ink : Mix(l.muted, l.ink, lit), 1);
        }
        if (row.steps && !renaming) {
            // Its two arrows, for the mouse: there while it is in hand or under the pointer.
            const double show = std::max(on ? 1.0 : 0.0, lit);
            const double before = Ease(15, i, ShelfHovered(15, i)), after = Ease(16, i, ShelfHovered(16, i));
            const double arrow_left = value_right - value_wide - 20;
            if (show > 0) {
                Put(p, value, arrow_left, base - 1, QStringLiteral("‹"), Faded(Mix(l.muted, l.accent, before), show));
                Put(p, value, right - 2, base - 1, QStringLiteral("›"), Faded(Mix(l.muted, l.accent, after), show), 1);
            }
            const QRectF list(left, list_top, right - left, list_tall);
            shelf_hits.append({QRectF(arrow_left - 14, base - 22, 14 + 20 + value_wide / 2, row_height).intersected(list), 15, i});
            shelf_hits.append({QRectF(value_right - value_wide / 2, base - 22, value_wide / 2 + 22, row_height).intersected(list), 16, i});
        }
        p.fillRect(QRectF(left, base + row_height - 22, right - left, 1), l.rule);
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

    p.fillRect(QRectF(ShelfLeft, 646, ShelfRight - ShelfLeft, 1), l.rule);
    if (!notice.isEmpty()) {
        const QFont say = ShelfFont(l.text, 14);
        Put(p, say, SheetW / 2, 676, Paint::Fit(notice, say, ShelfRight - ShelfLeft), l.ink, 2);
        return;
    }
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ ROW"), Qt::Key_Down}, {QStringLiteral("←"), Qt::Key_Left},
             {QStringLiteral("→ CHANGE"), Qt::Key_Right}, {QStringLiteral("Q"), Qt::Key_Q}, {QStringLiteral("E SECTION"), Qt::Key_E},
             {QStringLiteral("ENTER DO"), Qt::Key_Return}, {QStringLiteral("⌫ AS IT COMES"), Qt::Key_Backspace},
             {QStringLiteral("/ FIND"), Qt::Key_Slash},
             {pages_back ? QStringLiteral("ESC THE GAME") : QStringLiteral("ESC THE SHELF"), Qt::Key_Escape}});
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
    x += Put(p, small, x, 49, QStringLiteral("  ·  "), l.muted);
    x += Put(p, small, x, 49, QStringLiteral("%1").arg(shelf_choice + 1, 2, 10, QLatin1Char('0')), l.accent);
    Put(p, small, x, 49, QStringLiteral(" / %1").arg(titles.size()), l.muted);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    Put(p, small, right, 49, game.id.toUpper() + QStringLiteral("  ·  "), l.muted, 1);
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
        day.isEmpty() ? QStringLiteral("NOT READ YET") : QStringLiteral("LAST READ  ") + DayText(day, true), l.muted);
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
    facts.append({QStringLiteral("READ BY"),
                  !way.isEmpty() ? Lines::ReadByOf(game.path)
                                 : QStringLiteral("CORE  ·  %1%2").arg(SystemLabel(library, title.system).toUpper(),
                                                                       app != nullptr && !app->name.isEmpty() ? QStringLiteral("  ·  ") + app->name.toUpper() : QString())});
    QString from; // whose figures the rows under it are
    if (tracked != nullptr) {
        from = QStringLiteral("STUDY TRACKER");
        facts.append({QStringLiteral("READ"), tracked->total > 0 ? QStringLiteral("%1 OF %2 CHARACTERS  ·  %3%").arg(CountText(read), CountText(tracked->total))
                                                                       .arg(static_cast<int>(std::lround(100.0 * read / tracked->total)))
                                                                 : QStringLiteral("%1 CHARACTERS").arg(CountText(read))});
        if (tracked->hours > 0) {
            facts.append({QStringLiteral("TIME"), QStringLiteral("%1%2%3").arg(PlayClock(static_cast<qint64>(tracked->hours * 3.6e6)),
                                                                              tracked->speed > 0 ? QStringLiteral("  ·  %1 AN HOUR").arg(CountText(tracked->speed)) : QString(),
                                                                              tracked->sessions > 0 ? QStringLiteral("  ·  %1 SESSIONS").arg(tracked->sessions) : QString())});
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
            facts.append({QStringLiteral("JITEN"), jiten.join(QStringLiteral("  ·  "))});
        }
    } else if (game.chars > 0) {
        from = QStringLiteral("THIS DEVICE");
        const double hours = game.active_ms / 3.6e6;
        facts.append({QStringLiteral("READ"), QStringLiteral("%1 CHARACTERS  ·  %2%3").arg(CountText(game.chars), PlayClock(game.active_ms),
                                                                                              hours > 0.05 ? QStringLiteral("  ·  %1 AN HOUR").arg(CountText(static_cast<qint64>(game.chars / hours))) : QString())});
    }
    if (const auto other = elsewhere.constFind(game.id); other != elsewhere.constEnd() && other->second > game.last_day) {
        // Read on another device since it was read here: its save is fetched as the game starts.
        facts.append({QStringLiteral("ELSEWHERE"), QStringLiteral("%1  ·  %2").arg(other->first.toUpper(), DayText(other->second, true).toUpper())});
    }
    if (!record.guide.isEmpty()) {
        facts.append({QStringLiteral("GUIDE"), record.guide.toUpper().replace(QLatin1Char('-'), QLatin1Char(' '))});
    }
    const bool art_missing = ArtMissing(game);
    if (art_missing || art_said_of == game.id) {
        const bool looking = art_finder != nullptr && art_finder->Busy() && art_finder->GameId() == game.id;
        facts.append({QStringLiteral("BOX ART"), looking ? QStringLiteral("LOOKING …") : art_said_of == game.id && !art_said.isEmpty() ? art_said
                                                         : QStringLiteral("MISSING  ·  F FIND")});
    }
    const TitleReading& reading = ReadingOf(game.id);
    if (!reading.days.isEmpty()) {
        facts.append({QStringLiteral("30 DAYS"), QString()});
    }
    if (reading.line_count > 0) {
        facts.append({QStringLiteral("LOG"), QStringLiteral("%1 LINES ON THIS DEVICE  ·  ↓ OPEN").arg(CountText(reading.line_count))});
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
            Put(p, tiny, ShelfLeft + column, y, QStringLiteral("MOST %1 A DAY").arg(CountText(most)), l.muted, 1);
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
    QList<QPair<QString, int>> stage_keys{{QStringLiteral("ENTER PLAY"), Qt::Key_Return}, {QStringLiteral("←"), Qt::Key_Left},
                                          {QStringLiteral("→ ANOTHER TITLE"), Qt::Key_Right}, {QStringLiteral("↑ THE SHELF"), Qt::Key_Up},
                                          {QStringLiteral("↓ LOG"), Qt::Key_Down}};
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

    // The head: what this is, how much of it, the time.
    double x = ShelfLeft;
    x += Put(p, ShelfFont(l.text, 15), x, 50, QStringLiteral("ふ"), l.ink) + 8;
    x += Put(p, small, x, 49, QStringLiteral("FUROSHIKI"), l.ink);
    Put(p, small, x, 49, QStringLiteral("  ·  LIBRARY"), l.muted);
    double right = ShelfRight;
    right -= Put(p, small, right, 49, shelf_minute, l.ink, 1);
    Put(p, small, right, 49, QStringLiteral("%1 %2  ·  ").arg(count).arg(count == 1 ? QStringLiteral("TITLE") : QStringLiteral("TITLES")), l.muted, 1);
    p.fillRect(QRectF(ShelfLeft, 64, ShelfRight - ShelfLeft, 1), l.rule);

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
                p.drawPixmap(QRectF(plate.left() + (plate.width() - shown.width()) / 2, plate.top() + (plate.height() - shown.height()) / 2, shown.width(), shown.height()),
                             cover, QRectF(0, 0, cover.width(), cover.height()));
            }
            p.fillRect(QRectF(plate.right(), box.top(), 1, height), l.rule);
            const double tx = plate.right() + 28, tw = box.left() + plates[i] + SpineOpen * (1 - PlateShare) - 28 - tx;
            double at = tx + Put(p, small, tx, box.top() + 36, number, l.accent);
            // What reads it: a core and its console, or lines and how they come.
            const QString way = Lines::WayOf(game.path);
            Put(p, small, at, box.top() + 36, way.isEmpty() ? QStringLiteral("  ·  CORE  ·  ") + console : QStringLiteral("  ·  ") + Lines::ReadByOf(game.path), l.muted);

            // Its name, as large as the column and four lines let it be.
            const QString said = ShelfTitleOf(game.id, game.title);
            const bool japanese = HasJapanese(said);
            const QString name = japanese ? said : said.toUpper();
            const QRectF name_box(tx, box.top() + 62, tw, 232);
            const QString size_key = l.display + name;
            int size = title_sizes.value(size_key);
            if (size == 0) {
                for (size = japanese ? 44 : 32; size > 14; size -= 2) {
                    const QFont face = japanese ? ShelfFont(l.text, size) : ShelfFont(l.display, size, 0.02);
                    const QRectF need = QFontMetricsF(face).boundingRect(QRectF(0, 0, tw, 2000), Qt::TextWordWrap | Qt::AlignLeft, name);
                    if (need.height() <= name_box.height() && need.width() <= tw + 1) {
                        break;
                    }
                }
                title_sizes.insert(size_key, size);
            }
            p.setFont(japanese ? ShelfFont(l.text, size) : ShelfFont(l.display, size, 0.02));
            p.setPen(l.ink);
            p.drawText(name_box, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, name);

            // What was read of it, as a small table; then the one filled thing on the page.
            const QFont value = ShelfFont(l.mono, 13, 0.04);
            // The study tracker's figures where it has the title (every device's reading, and how
            // long the title is), else what was read on this device.
            const TrackerTitle* tracked = TrackerOf(game.id);
            const qint64 read = tracked != nullptr ? tracked->read : game.chars;
            const qint64 spent = tracked != nullptr && tracked->hours > 0 ? static_cast<qint64>(tracked->hours * 3.6e6) : game.active_ms;
            const QString day = tracked != nullptr ? std::max(tracked->last, game.last_day) : game.last_day;
            const QString share = tracked != nullptr && tracked->total > 0
                                      ? QStringLiteral("  ·  %1%").arg(static_cast<int>(std::lround(100.0 * read / tracked->total)))
                                      : QString();
            const QList<QPair<QString, QString>> facts = {
                {QStringLiteral("CHARACTERS"), read > 0 ? CountText(read) + share : QStringLiteral("—")},
                {QStringLiteral("PLAY TIME"), spent > 0 ? PlayClock(spent) : QStringLiteral("—")},
                {QStringLiteral("LAST READ"), day.isEmpty() ? QStringLiteral("—") : DayText(day, true)}};
            double y = box.bottom() - 150;
            for (const auto& fact : facts) {
                p.fillRect(QRectF(tx, y - 17, tw, 1), l.rule);
                Put(p, tiny, tx, y, fact.first, l.muted);
                Put(p, value, tx + tw, y, fact.second, l.ink, 1);
                y += 26;
            }
            const QRectF key(tx, box.bottom() - 62, tw, 36);
            p.fillRect(key, ShelfHovered(3, 0) ? Faded(l.ink, 0.82) : l.ink);
            if (open > 0.5) {
                shelf_hits.append({key, 3, 0});
            }
            const QFont key_face = ShelfFont(l.display, 12, 0.16);
            Put(p, key_face, key.center().x(), key.center().y() + 4.4, read > 0 ? QStringLiteral("CONTINUE") : QStringLiteral("START"), l.ground, 2);
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
            Put(p, face, 0, 4.4, QStringLiteral("ADD A TITLE"), Faded(l.muted, seen));
            p.restore();
        }
        if (open > 0.02) {
            p.save();
            p.setClipRect(box);
            p.setOpacity(p.opacity() * open);
            const double tx = box.left() + 28, tw = SpineOpen - 56;
            const double at = tx + Put(p, small, tx, box.top() + 36, QStringLiteral("＋"), l.accent);
            Put(p, small, at, box.top() + 36, count == 0 ? QStringLiteral("  ·  THE SHELF IS EMPTY") : QStringLiteral("  ·  ONE MORE"), l.muted);
            Put(p, ShelfFont(l.display, 32, 0.02), tx, box.top() + 104, QStringLiteral("ADD A TITLE"), l.ink);
            const QList<QPair<QString, QString>> doors = {{QStringLiteral("A DISC OR FILE"), QStringLiteral("A CORE")},
                                                          {QStringLiteral("A GAME WINDOW"), QStringLiteral("HOOK  ·  OCR")},
                                                          {QStringLiteral("LINES I SEND"), QStringLiteral("LINES")}};
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

    // The floors: every shelf, how many titles it holds, the one here marked; the mark glides.
    const QStringList sections = ShelfSections();
    const QFont floor_font = ShelfFont(l.mono, 10.5, 0.12);
    const double floor_left = ShelfRight - FloorColumn + 28;
    // Above every shelf, the settings: where up from All goes.
    {
        const QColor ink = ShelfHovered(10, -1) ? l.ink : l.muted;
        Put(p, floor_font, ShelfRight - 14, ShelfTop + 22, QStringLiteral("SETTINGS"), ink, 1);
        p.fillRect(QRectF(ShelfRight - 1, ShelfTop, 1, FloorPitch - 2), l.rule);
        shelf_hits.append({QRectF(floor_left - 8, ShelfTop, ShelfRight - floor_left + 8, FloorPitch - 2), 10, -1});
    }
    for (int i = 0; i <= sections.size(); ++i) {
        const double y = ShelfTop + FloorFirst + i * FloorPitch;
        const bool here = i == shelf_section;
        const QString name = i == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections[i - 1]).toUpper();
        const QColor ink = here || ShelfHovered(10, i) ? l.ink : l.muted;
        Put(p, floor_font, ShelfRight - 14, y + 22, Paint::Fit(name, floor_font, FloorColumn - 70), ink, 1);
        Put(p, tiny, floor_left, y + 22, QString::number(SectionGames(i).size()), Faded(ink, 0.7));
        p.fillRect(QRectF(ShelfRight - 1, y, 1, FloorPitch - 2), l.rule);
        shelf_hits.append({QRectF(floor_left - 8, y, ShelfRight - floor_left + 8, FloorPitch - 2), 10, i});
    }
    p.fillRect(QRectF(ShelfRight - 3, ShelfTop + FloorFirst + floor_at * FloorPitch, 3, FloorPitch - 2), l.accent);

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
    x = ShelfLeft;
    x += Put(p, small, x, 674, shelf_section == 0 ? QStringLiteral("ALL") : SystemLabel(library, sections.value(shelf_section - 1)).toUpper(), l.ink);
    Put(p, small, x, 674, QStringLiteral("  ·  SHELF %1 OF %2").arg(shelf_section + 1).arg(sections.size() + 1), l.muted);
    PutKeys(p, tiny, ShelfRight, 674,
            {{QStringLiteral("↑"), Qt::Key_Up}, {QStringLiteral("↓ SHELF"), Qt::Key_Down}, {QStringLiteral("←"), Qt::Key_Left},
             {QStringLiteral("→ BROWSE"), Qt::Key_Right}, {QStringLiteral("ENTER OPEN"), Qt::Key_Return}, {QStringLiteral("S SORT"), Qt::Key_S},
             {QStringLiteral("F2 SETTINGS"), Qt::Key_F2}, {QStringLiteral("F4 ") + l.name.toUpper(), Qt::Key_F4}});
}
