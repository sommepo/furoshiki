// Furoshiki's icon, drawn from icon/icon.json: one mark, in the colours chosen in Settings.

#include "app_icon.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPixmap>
#include <QPolygonF>
#include <QSettings>

namespace Home {

namespace {

constexpr int Square = 512; ///< The side of the square the mark is written on
constexpr int Sizes[] = {16, 24, 32, 48, 64, 128, 256};

struct IconFile {
    QList<IconColours> colours;
    QList<QPolygonF> mark;
    QString default_key;
};

const IconFile& File() {
    static const IconFile file = [] {
        IconFile read;
        QFile source(QStringLiteral(":/icon/icon.json"));
        if (!source.open(QIODevice::ReadOnly)) {
            return read;
        }
        const QJsonObject root = QJsonDocument::fromJson(source.readAll()).object();
        read.default_key = root[QStringLiteral("default")].toString();
        for (const QJsonValue& shape : root[QStringLiteral("mark")].toArray()) {
            QPolygonF polygon;
            for (const QJsonValue& point : shape.toArray()) {
                polygon << QPointF(point.toArray()[0].toDouble(), point.toArray()[1].toDouble());
            }
            read.mark.append(polygon);
        }
        for (const QJsonValue& entry : root[QStringLiteral("colours")].toArray()) {
            const QJsonObject colour = entry.toObject();
            read.colours.append({colour[QStringLiteral("key")].toString(), colour[QStringLiteral("name")].toString(),
                                 QColor(colour[QStringLiteral("ground")].toString()),
                                 QColor(colour[QStringLiteral("ink")].toString())});
        }
        return read;
    }();
    return file;
}

const IconColours* Find(const QString& key) {
    for (const IconColours& colours : File().colours) {
        if (colours.key == key) {
            return &colours;
        }
    }
    return nullptr;
}

} // namespace

const QList<IconColours>& IconChoices() {
    return File().colours;
}

QString ChosenIcon() {
    const QString kept = QSettings().value(QStringLiteral("look/icon")).toString();
    return Find(kept) != nullptr ? kept : File().default_key;
}

void ChooseIcon(const QString& key) {
    QSettings().setValue(QStringLiteral("look/icon"), key);
    ApplyChosenIcon();
}

void ApplyChosenIcon() {
    QGuiApplication::setWindowIcon(IconPicture(ChosenIcon()));
}

QString IconLabel(const QString& key) {
    const IconColours* colours = Find(key);
    return colours != nullptr ? QStringLiteral("%1 %2").arg(colours->key, colours->name) : key;
}

QImage IconImage(const QString& key, int size) {
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    const IconColours* colours = Find(key);
    if (colours == nullptr) {
        image.fill(Qt::transparent);
        return image;
    }
    image.fill(colours->ground);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(double(size) / Square, double(size) / Square);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colours->ink);
    for (const QPolygonF& polygon : File().mark) {
        painter.drawPolygon(polygon);
    }
    return image;
}

QIcon IconPicture(const QString& key) {
    QIcon icon;
    for (const int size : Sizes) {
        icon.addPixmap(QPixmap::fromImage(IconImage(key, size)));
    }
    return icon;
}

} // namespace Home
