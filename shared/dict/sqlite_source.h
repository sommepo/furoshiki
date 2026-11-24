// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include "dict/library.h"

namespace Dict {

/**
 * One dictionary of an earlier version's SQLite file (reader_dict.db: its dictionaries, terms,
 * term_meta and media tables, glossaries packed as the apps pack them) as a Source the library
 * imports from: the move of that file into the engine's library (docs/dictionary-engine.md) read
 * straight from SQLite, with no Qt, for Furoshiki for Android, where it was written out as a zip
 * in Kotlin first (38 s for the Y700's 8 dictionaries). The file is opened read-only; the caller
 * makes sure nothing is left in a write-ahead log (a checkpoint) before.
 */
class SqliteSource : public Source {
public:
    /// Throws std::runtime_error when the file or the dictionary cannot be read.
    SqliteSource(const std::string& path, std::int64_t dictionary_id);
    ~SqliteSource() override;

    std::string Index() override;
    std::vector<std::string> TermBanks() override;
    std::vector<std::string> MetaBanks() override;
    std::vector<std::string> Media() override;
    std::string MediaPath(const std::string& name) override;
    std::uint64_t Size(const std::string& name) override;
    std::string Read(const std::string& name) override;
    std::string Styles() override;

    /// A glossary as stored (0 and the text, or 1 and a zlib stream) back to its JSON.
    static std::string Unpack(const void* data, std::size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Dict
