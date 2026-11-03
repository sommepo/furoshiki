// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the Flycast Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

package org.readers.app.reader

import androidx.core.content.FileProvider

/**
 * Hands the reader's files to other apps: card media for AnkiDroid and statistics exports. A
 * class of its own because the emulator already declares a FileProvider, and a manifest can
 * name a class only once.
 */
class ReaderFileProvider : FileProvider()
