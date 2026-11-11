// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The settings window's CRT tab: the CRT picture's values (the shared table,
// ../shared/reader/core/crt.h) for all consoles, one console or the game playing, kept in the
// program's layers (crt_layers.h). What it changes shows at once in a game playing that the
// layer counts for.

#include <cmath>
#include <cstring>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>
#include "crt_layers.h"
#include "reader/core/crt.h"
#include "reader/qt/reader_crt_page.h"
#include "reader/qt/reader_session.h"
#include "settings_window.h"

namespace Home {

namespace {

QString Base() {
    return ReaderUi::ReaderSession::Instance().Settings().FileValue(CrtLayers::ReaderKey).toString();
}

int Steps(const Reader::Crt::Param& param) {
    return static_cast<int>(std::lround((param.high - param.low) / param.step));
}

/// The ready-made look the values are, or empty: as Reader::Crt::CurrentPreset, the frame's
/// values and the switch aside.
QString PresetOf(const CrtLayers::Values& values) {
    const auto& params = Reader::Crt::Params();
    for (const Reader::Crt::Preset& preset : Reader::Crt::Presets()) {
        CrtLayers::Values wanted = CrtLayers::Initial();
        const CrtLayers::Values named = CrtLayers::Parse(QString::fromLatin1(preset.values));
        for (auto it = named.cbegin(); it != named.cend(); ++it) {
            wanted.insert(it.key(), it.value());
        }
        bool same = true;
        for (std::size_t i = 1; i < params.size() && same; ++i) {
            const QString key = QString::fromLatin1(params[i].key);
            same = params[i].frame || std::fabs(wanted.value(key) - values.value(key)) < 0.001;
        }
        if (same) {
            return QString::fromLatin1(preset.key);
        }
    }
    return {};
}

} // namespace

QWidget* SettingsWindow::CrtTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    crt_layer = new QComboBox(page);
    FillLayers(crt_layer);
    // Over a game, the layer its picture comes from now.
    QString console, game, title;
    if (hooks.playing) {
        hooks.playing(console, game, title);
    }
    if (!console.isEmpty()) {
        const CrtLayers::Layer target = CrtLayers::Target(play, console, game);
        const QStringList wanted = target.console.isEmpty() ? QStringList{}
                                   : target.game.isEmpty()  ? QStringList{target.console}
                                                            : QStringList{target.console, target.game};
        for (int i = 0; i < crt_layer->count(); ++i) {
            if (crt_layer->itemData(i).toStringList() == wanted) {
                crt_layer->setCurrentIndex(i);
            }
        }
    }
    auto* head = new QFormLayout;
    head->addRow(QStringLiteral("Settings for"), crt_layer);
    crt_looks = new QComboBox(page);
    for (const Reader::Crt::Preset& preset : Reader::Crt::Presets()) {
        crt_looks->addItem(QString::fromUtf8(preset.name), QString::fromLatin1(preset.key));
    }
    crt_looks->addItem(QStringLiteral("Your own"), QString());
    head->addRow(QStringLiteral("Look"), crt_looks);
    layout->addLayout(head);
    auto* state = new QHBoxLayout;
    crt_note = new QLabel(page);
    crt_note->setWordWrap(true);
    crt_follow = new QPushButton(page);
    state->addWidget(crt_note, 1);
    state->addWidget(crt_follow);
    layout->addLayout(state);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* form = new QFormLayout(body);
    QString group;
    for (const Reader::Crt::Param& param : Reader::Crt::Params()) {
        const QString key = QString::fromLatin1(param.key);
        const QString heading = QString::fromUtf8(param.group);
        if (heading != group && !heading.isEmpty()) {
            group = heading;
            auto* label = new QLabel(QStringLiteral("<b>%1</b>").arg(heading.toHtmlEscaped()), body);
            form->addRow(label);
        }
        QWidget* control = nullptr;
        switch (param.kind) {
        case Reader::Crt::Kind::Toggle: {
            auto* box = new QCheckBox(QString::fromUtf8(param.hint), body);
            connect(box, &QCheckBox::toggled, this, [this, key](bool on) { SetCrt(key, on ? 1 : 0); });
            crt_refresh.append([box, key](const CrtLayers::Values& values) { box->setChecked(values.value(key) >= 0.5); });
            control = box;
            break;
        }
        case Reader::Crt::Kind::Choice: {
            auto* box = new QComboBox(body);
            for (const QString& entry : QString::fromUtf8(param.choices).split(u'|')) {
                const qsizetype at = entry.indexOf(u'=');
                box->addItem(entry.mid(at + 1), entry.left(at).toDouble());
            }
            connect(box, &QComboBox::currentIndexChanged, this, [this, box, key](int) {
                SetCrt(key, box->currentData().toDouble());
            });
            crt_refresh.append([box, key](const CrtLayers::Values& values) {
                int best = 0;
                for (int i = 0; i < box->count(); ++i) {
                    if (std::fabs(box->itemData(i).toDouble() - values.value(key)) <
                        std::fabs(box->itemData(best).toDouble() - values.value(key))) {
                        best = i;
                    }
                }
                box->setCurrentIndex(best);
            });
            Calm(box);
            control = box;
            break;
        }
        case Reader::Crt::Kind::Slider: {
            auto* row = new QWidget(body);
            auto* row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(0, 0, 0, 0);
            auto* slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(0, Steps(param));
            auto* number = new QLabel(row);
            number->setMinimumWidth(40);
            row_layout->addWidget(slider, 1);
            row_layout->addWidget(number);
            const float low = param.low;
            const float step = param.step;
            connect(slider, &QSlider::valueChanged, this, [this, number, key, low, step](int at) {
                const double value = low + step * at;
                number->setText(QString::number(value, 'f', 2));
                SetCrt(key, value);
            });
            crt_refresh.append([slider, number, key, low, step](const CrtLayers::Values& values) {
                slider->setValue(static_cast<int>(std::lround((values.value(key) - low) / step)));
                number->setText(QString::number(values.value(key), 'f', 2));
            });
            Calm(slider);
            control = row;
            break;
        }
        }
        if (control != nullptr) {
            if (param.hint != nullptr && *param.hint != 0) {
                control->setToolTip(QString::fromUtf8(param.hint));
            }
            form->addRow(QString::fromUtf8(param.name), control);
        }
    }
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);

    connect(crt_layer, &QComboBox::currentIndexChanged, this, [this] { FillCrtRows(); });
    connect(crt_looks, &QComboBox::currentIndexChanged, this, [this](int) {
        if (filling) {
            return;
        }
        const QString key = crt_looks->currentData().toString();
        if (key.isEmpty()) {
            return;
        }
        // As Reader::Crt::ApplyPreset: everything at first but the switch and the frame, then
        // what the look names.
        const Layer layer = LayerOf(crt_layer);
        CrtLayers::Values values = CrtLayers::Resolved(play, Base(), layer.console, layer.game);
        const CrtLayers::Values initial = CrtLayers::Initial();
        for (const Reader::Crt::Param& param : Reader::Crt::Params()) {
            if (!param.frame && std::strcmp(param.key, "enabled") != 0) {
                values.insert(QString::fromLatin1(param.key), initial.value(QString::fromLatin1(param.key)));
            }
        }
        for (const Reader::Crt::Preset& preset : Reader::Crt::Presets()) {
            if (key == QLatin1String(preset.key)) {
                const CrtLayers::Values named = CrtLayers::Parse(QString::fromLatin1(preset.values));
                for (auto it = named.cbegin(); it != named.cend(); ++it) {
                    values.insert(it.key(), it.value());
                }
            }
        }
        StoreCrt(values);
    });
    connect(crt_follow, &QPushButton::clicked, this, [this] {
        const Layer layer = LayerOf(crt_layer);
        if (layer.console.isEmpty()) {
            StoreCrt(CrtLayers::Below(play, Base(), {}));
        } else {
            play.Clear(CrtLayers::Key, layer.console, layer.game);
            play.Save();
            ReaderCrtPage::Load(ReaderUi::ReaderSession::Instance().Settings());
            FillCrtRows();
        }
    });
    FillCrtRows();
    return page;
}

void SettingsWindow::FillCrtRows() {
    if (crt_layer == nullptr) {
        return;
    }
    const Layer layer = LayerOf(crt_layer);
    const CrtLayers::Values values = CrtLayers::Resolved(play, Base(), layer.console, layer.game);
    filling = true;
    for (const auto& refresh : std::as_const(crt_refresh)) {
        refresh(values);
    }
    const int look = crt_looks->findData(PresetOf(values));
    crt_looks->setCurrentIndex(look < 0 ? crt_looks->count() - 1 : look);
    filling = false;
    const bool own = play.IsSet(CrtLayers::Key, layer.console, layer.game);
    if (layer.console.isEmpty()) {
        crt_note->setText(own ? QStringLiteral("Every console's games, unless a console or a game has its own.")
                              : QStringLiteral("Every console's games, unless a console or a game has its own. "
                                               "Nothing changed here yet."));
        crt_follow->setText(QStringLiteral("Reset to defaults"));
        crt_follow->setEnabled(own);
    } else if (layer.game.isEmpty()) {
        crt_note->setText(own ? QStringLiteral("This console has values of its own; the rest are all consoles'.")
                              : QStringLiteral("Same as all consoles: a change here is this console's alone."));
        crt_follow->setText(QStringLiteral("Same as all consoles"));
        crt_follow->setEnabled(own);
    } else {
        crt_note->setText(own ? QStringLiteral("This game has values of its own; the rest are its console's.")
                              : QStringLiteral("Same as its console: a change here is this game's alone."));
        crt_follow->setText(QStringLiteral("Same as its console"));
        crt_follow->setEnabled(own);
    }
}

void SettingsWindow::SetCrt(const QString& key, double value) {
    if (filling) {
        return;
    }
    const Layer layer = LayerOf(crt_layer);
    CrtLayers::Values values = CrtLayers::Resolved(play, Base(), layer.console, layer.game);
    values.insert(key, value);
    StoreCrt(values);
}

void SettingsWindow::StoreCrt(const CrtLayers::Values& values) {
    const Layer layer = LayerOf(crt_layer);
    CrtLayers::Store(play, Base(), values, layer.console, layer.game);
    play.Save();
    // The game playing takes its values again (the layer may be its own, its console's or all's).
    ReaderCrtPage::Load(ReaderUi::ReaderSession::Instance().Settings());
    FillCrtRows();
}

} // namespace Home
