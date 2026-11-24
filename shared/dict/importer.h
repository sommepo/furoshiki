// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>
#include "dict/library.h"

namespace Dict::Detail {

/// What an import wrote, for the library to take in.
struct Written {
    bool ok = false;
    bool already_there = false;
    bool not_dictionary = false;
    std::string error;
    std::string title;
    std::string revision;
    std::string author;
    std::string description;
    std::string frequency_mode;
    std::string scope;
    std::string styles;
    std::int64_t terms = 0;
    std::int64_t metas = 0;
    std::int64_t media = 0;
    std::int64_t kanji = 0;
};

/// Imports the dictionary at `zip_path` into the (new) folder `out_folder`. `title_taken`
/// says whether a dictionary of that title is in the library already.
Written WriteDictionary(const std::string& zip_path, const std::string& out_folder, const ImportOptions& options,
                        const std::function<bool(const std::string&)>& title_taken);
/// The same from any source.
Written WriteDictionaryFrom(Source& source, const std::string& out_folder, const ImportOptions& options,
                            const std::function<bool(const std::string&)>& title_taken);

/// The info.json of a dictionary written.
std::string InfoJson(const Written& written, std::int64_t id);

} // namespace Dict::Detail
