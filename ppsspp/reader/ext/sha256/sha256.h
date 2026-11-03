// SPDX-FileCopyrightText: PPSSPP Reader fork contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// The reader layer's SHA-256 (sha256_starts, sha256_update, sha256_finish), as the other
// reader forks carry it in this folder: here the emulator's own, which is the same code.
#include "Common/Crypto/sha256.h"
