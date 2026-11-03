// SPDX-FileCopyrightText: Flycast Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Flycast's RetroAchievements notifier (core/ui/gui_achievements.h) for the core of Furoshiki,
// which has no GUI: a message is handed to the program (reader_core_host.notify), which shows it
// over the game; the challenge and leaderboard indicators are not drawn. flycast_core.cpp has it.

#pragma once

#include <string>
#include "types.h"

namespace achievements
{

class Notification
{
public:
	enum Type {
		None,
		Login,
		GameLoaded,
		Unlocked,
		Progress,
		Mastery,
		Challenge,   // internal use
		Leaderboard, // internal use
		Error
	};
	void notify(Type type, const std::string& image, const std::string& text1,
			const std::string& text2 = {}, const std::string& text3 = {});
	void showChallenge(const std::string&) {}
	void hideChallenge(const std::string&) {}
	void showLeaderboard(u32, const std::string&) {}
	void hideLeaderboard(u32) {}
};

extern Notification notifier;

}
