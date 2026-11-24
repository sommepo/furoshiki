// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QObject>

namespace ReaderUi {

/**
 * A list's choice shown before it is taken (the maintainer, 2026-11-24: "Lots of popup themes but
 * without a preview of them ... impossible to know the difference"): while the drop-down of [box]
 * is open, [preview] hears the index of the item the pointer or the keys are on; when it closes,
 * whether a choice was taken or not, it hears -1 (the setting's own again).
 */
inline void PreviewWhileOpen(QComboBox* box, std::function<void(int index)> preview) {
    class Closed : public QObject {
    public:
        Closed(QObject* parent, std::function<void()> told) : QObject(parent), told(std::move(told)) {}
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type() == QEvent::Hide) {
                told();
            }
            return false;
        }

    private:
        std::function<void()> told;
    };
    QObject::connect(box, &QComboBox::highlighted, box, [box, preview](int index) {
        if (box->view() != nullptr && box->view()->isVisible()) {
            preview(index);
        }
    });
    if (box->view() != nullptr) {
        QWidget* list = box->view()->window(); // the drop-down's own window
        list->installEventFilter(new Closed(box, [preview] { preview(-1); }));
    }
}

} // namespace ReaderUi
