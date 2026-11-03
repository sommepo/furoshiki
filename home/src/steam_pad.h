// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Valve's Steam Controller (2026) in Furoshiki itself, without Steam: the same bridge the
// reader apps play (steam_bridge.h), so that the pads feel here as they will in a game and a
// slider is felt the moment it moves. The right pad is the pointer, the left the wheel, the
// buttons are the menu's keys. Windows only (elsewhere this does nothing), only while Steam
// is not running, and only while Furoshiki is the window in use: a reader app that was
// started plays the controller itself.

#include <memory>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include "steam_bridge.h"

namespace Home {

/// The shared settings' "steam" as the bridge's tuning (what the reader apps make of it).
[[nodiscard]] ReaderApp::SteamBridge::Tuning TuningOf(const QJsonObject& steam);

class SteamPad : public QObject {
    Q_OBJECT

public:
    explicit SteamPad(QObject* parent = nullptr);
    ~SteamPad() override;

    /// Whether the controller is Furoshiki's to play now (its window is the one in use
    /// and no app it started is running).
    void SetActive(bool active);
    void SetTuning(const ReaderApp::SteamBridge::Tuning& tuning);
    /// While a game plays in this program's window: the controller's buttons and sticks are
    /// the game's (ButtonChanged, AxisChanged), not the menu's keys; its pads still move the
    /// pointer.
    void SetGameMode(bool on);
    /// What it is doing, in a sentence (the Settings page shows it).
    [[nodiscard]] QString Describe() const;

signals:
    /// A button of the controller as a key of the menu (Qt::Key), from the reading thread.
    void KeyPressed(int key);
    /// In game mode: a button (ReaderApp::SteamBridge::Button, SDL's order) went down or up, or
    /// an axis (SteamBridge::Axis) moved; from the reading thread.
    void ButtonChanged(int button, bool down);
    void AxisChanged(int axis, int value);

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace Home
