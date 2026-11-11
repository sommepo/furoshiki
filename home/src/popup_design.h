// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The dictionary popup made by hand without CSS (the maintainer, 2026-11-10: "move away from CSS
// altogether now and just have sliders for every possible element, also the ability to move
// elements around graphically and resize elements within a popup customiser"). A design belongs to
// one look (popup.theme) and holds what differs from it: a value for any of the elements' settings
// below, and the order of an entry's blocks and of its headline's parts. It is drawn by the page as
// the user's stylesheet always was (Reader.setStyle "user"), made from the design here: the same
// on every engine, every look and the Android apps, and nothing of CSS for the user to see.
//
// A design is a file: <program>/popup-styles/<name>.json,
//   { "format": 1, "kind": "reader-popup-design", "name": "yomitan-dark-mine", "base": "yomitan-dark",
//     "values": { "headword.size": 30, "glosses.colour": "#f2f2f2" },
//     "order": { "entry": ["head", "senses", "card", "forms", "buttons", "lookups"], "head": [...] } }
// The program's popup.design names the one in use on a layer; one made for another look than the
// layer's is not drawn (and changing the look leaves the design).

#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace Home::PopupDesign {

struct Property {
    enum Kind { Size, Number, Colour, Toggle, Choice };
    QString key;     ///< "headword.size"
    QString label;   ///< "Size"
    Kind kind{Size};
    double low{}, high{}, step{1};
    QString unit;    ///< "px", "%", ""
    QList<QPair<QString, QString>> choices; ///< value, label
    double start{};  ///< Where a slider stands before it is moved: about what most looks have
};

struct Element {
    QString id;      ///< "headword"
    QString label;   ///< "Headword"
    QString pick;    ///< The page's elements it is, for a click in the preview (CSS selectors)
    QString size;    ///< The property a handle dragged in the preview changes ("headword.size"), or empty
    QList<Property> properties;
};

/// Every element the popup has, and what can be set of each, in the order they are listed.
[[nodiscard]] const QList<Element>& Elements();
[[nodiscard]] const Property* PropertyOf(const QString& key);

/// An entry's blocks and its headline's parts, as the page draws them unless a design says otherwise.
[[nodiscard]] const QList<QPair<QString, QString>>& Blocks();    ///< id, label
[[nodiscard]] const QList<QPair<QString, QString>>& HeadParts(); ///< id, label

struct Design {
    QString name;   ///< Its file's name
    QString base;   ///< The look it is made over
    QJsonObject values;
    QStringList entry_order, head_order; ///< Empty: as the page has them
    [[nodiscard]] bool Empty() const {
        return values.isEmpty() && entry_order.isEmpty() && head_order.isEmpty();
    }
};

/// The stylesheet the page draws a design with: every value, the orders; empty for an empty design.
[[nodiscard]] QString Css(const Design& design);

/// The designs of a folder, all, or those made over `base` only.
[[nodiscard]] QList<Design> InFolder(const QString& folder, const QString& base = {});
[[nodiscard]] std::optional<Design> Load(const QString& folder, const QString& name);
bool Save(const QString& folder, const Design& design);
bool Remove(const QString& folder, const QString& name);
/// A free name for a new design over `base`: "<base>-mine", then "<base>-mine-2"...
[[nodiscard]] QString FreeName(const QString& folder, const QString& base);
/// A design's name as the window says it: "Yomitan dark · mine".
[[nodiscard]] QString Shown(const QString& name, const QString& base);
/// A design from a style file's "design", or from its JSON form.
[[nodiscard]] Design FromJson(const QJsonObject& json);
[[nodiscard]] QJsonObject ToJson(const Design& design);
/// A value set within its property's bounds (a number), or as it was (the rest); null when it is no
/// value of that property.
[[nodiscard]] QJsonValue Clean(const Property& property, const QJsonValue& value);

/// What the preview's design mode is told: the elements to pick by a click, the blocks and parts
/// to drag, the properties a handle resizes (popup.js, Reader.design).
[[nodiscard]] QJsonObject PageConfig();

} // namespace Home::PopupDesign
