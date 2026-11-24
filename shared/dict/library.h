// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Furoshiki's dictionary engine (docs/dictionary-engine.md): Yomitan dictionaries imported into
// files of its own, a folder each, and looked up through one index of the whole library.
// Plain C++ (no Qt), for the desktop, Android (through JNI) and the PS Vita alike. Every text
// is UTF-8.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace Dict {

namespace Detail {
struct Written;
}

struct DictionaryInfo {
    std::int64_t id = 0;
    std::string folder;
    std::string title;
    std::string revision;
    std::string author;
    std::string description;
    std::string frequency_mode;
    /// "|ID|ID|" (Furoshiki's game IDs, upper case) for a names dictionary, "" for every game.
    std::string scope;
    std::string styles; ///< its styles.css
    int priority = 0;   ///< its place in the library's order, from 0
    bool enabled = true;
    std::int64_t terms = 0;
    std::int64_t metas = 0;
    std::int64_t media = 0;
    std::int64_t kanji = 0; ///< characters of a kanji bank
};

struct ImportOptions {
    /// Threads that read, parse and compress (besides the one that writes); 0: the machine's.
    int threads = 0;
    /// Bank text read ahead of the writer at most, in bytes.
    std::int64_t ahead_bytes = 128ll * 1024 * 1024;
    std::function<void(int done, int total)> progress;
    /// Asked between banks; false cancels the import.
    std::function<bool()> keep_going;
};

struct ImportResult {
    bool ok = false;
    bool already_there = false;
    bool not_dictionary = false; ///< the zip has no index.json
    std::string error;
    std::string title;
    std::int64_t id = 0;
    std::int64_t terms = 0;
    std::int64_t metas = 0;
    std::int64_t media = 0;
    std::int64_t kanji = 0;
};

/// A term found, without its definition (Library::Definition reads that).
struct Term {
    std::int64_t dictionary_id = 0;
    std::uint32_t entry = 0;
    std::string dictionary;
    int priority = 0;
    std::string expression;
    std::string reading; ///< "" when it is the expression
    std::string definition_tags;
    std::string rules;
    int score = 0;
    std::int64_t sequence = 0;
    std::string term_tags;
    bool scoped = false; ///< from a dictionary for the running game only
};

/// A character of a kanji dictionary.
struct Kanji {
    std::int64_t dictionary_id = 0;
    std::string dictionary;
    int priority = 0;
    std::string character;
    std::string onyomi;   ///< space-separated, as the bank has them
    std::string kunyomi;
    std::string tags;
    std::string meanings; ///< a JSON array
    std::string stats;    ///< a JSON object
};

struct Meta {
    std::string dictionary;
    std::string frequency_mode;
    std::string expression;
    std::string mode;
    std::string data; ///< its value as JSON
};

/**
 * Where an import takes a dictionary from: a Yomitan zip (Library::Import makes one), or the
 * rows of another store (the SQLite file a library is converted from). Read is called from
 * several threads at once.
 */
class Source {
public:
    virtual ~Source() = default;
    /// index.json's text.
    virtual std::string Index() = 0;
    /// The banks' names, in their order.
    virtual std::vector<std::string> TermBanks() = 0;
    virtual std::vector<std::string> MetaBanks() = 0;
    /// A kanji dictionary's banks (none, unless the source has them).
    virtual std::vector<std::string> KanjiBanks() {
        return {};
    }
    virtual std::vector<std::string> KanjiMetaBanks() {
        return {};
    }
    /// The pictures' names, and the path glossaries name each by.
    virtual std::vector<std::string> Media() = 0;
    virtual std::string MediaPath(const std::string& name) = 0;
    /// About how big a bank or picture is (its text, unpacked), to bound what is read ahead.
    virtual std::uint64_t Size(const std::string& name) = 0;
    /// A bank's JSON (an array of rows, as Yomitan writes them) or a picture's bytes. Throws
    /// std::runtime_error when it cannot.
    virtual std::string Read(const std::string& name) = 0;
    /// The dictionary's styles.css, or "".
    virtual std::string Styles() = 0;
};

class Library {
public:
    /// Opens (and makes) the library in `folder`. A dictionary left half imported is removed.
    explicit Library(std::string folder);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] const std::string& Folder() const {
        return folder;
    }

    /// The dictionaries in their order.
    [[nodiscard]] std::vector<DictionaryInfo> Dictionaries();
    [[nodiscard]] bool HasEnabled();
    void SetEnabled(std::int64_t id, bool enabled);
    /// Puts the dictionaries in this order (ids it does not name keep theirs, after these).
    void SetOrder(const std::vector<std::int64_t>& ids);
    void Delete(std::int64_t id);
    /// Many at once: one save of the library, each removed dictionary's folder gone.
    void SetEnabled(const std::vector<std::int64_t>& ids, bool enabled);
    void Delete(const std::vector<std::int64_t>& ids);

    /// Imports a Yomitan dictionary (.zip). It is written to a folder of its own beside the
    /// others and only joins the library once whole.
    ImportResult Import(const std::string& zip_path, const ImportOptions& options = {});
    /// The same from any source (another store's rows).
    ImportResult ImportFrom(Source& source, const ImportOptions& options = {});

    /**
     * Terms of enabled dictionaries whose expression or reading is one of `keys`: of the
     * dictionaries for every game, and of those scoped to `game` (a game ID) when one is
     * given. In the order SQLite gave them (each key in byte order, by expression then by
     * reading; within a key, by dictionary and entry), so that ranking stays as it was.
     */
    [[nodiscard]] std::vector<Term> FindTerms(const std::vector<std::string>& keys, const std::string& game = {});
    /// The definition (glossary JSON) of a term FindTerms gave.
    [[nodiscard]] std::string Definition(std::int64_t dictionary_id, std::uint32_t entry);
    /// Frequency, pitch and other meta rows of enabled dictionaries for these expressions.
    [[nodiscard]] std::vector<Meta> FindMeta(const std::vector<std::string>& expressions);
    /// A character in the enabled kanji dictionaries, in their order.
    [[nodiscard]] std::vector<Kanji> FindKanji(const std::string& character);
    /// Frequencies of a character (kanji_meta_bank) in the enabled dictionaries, in their order.
    [[nodiscard]] std::vector<Meta> FindKanjiMeta(const std::string& character);
    /// A picture of a dictionary by its path in the zip.
    [[nodiscard]] std::optional<std::string> Media(std::int64_t dictionary_id, const std::string& path);
    /// (title, styles.css) of the enabled dictionaries that have a stylesheet.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> Styles();

    /// Every term, meta and picture of a dictionary in the order imported (for checks and for
    /// moving a library elsewhere).
    [[nodiscard]] std::uint32_t TermCount(std::int64_t dictionary_id);
    [[nodiscard]] Term TermAt(std::int64_t dictionary_id, std::uint32_t entry);
    [[nodiscard]] std::uint32_t MetaCount(std::int64_t dictionary_id);
    [[nodiscard]] Meta MetaAt(std::int64_t dictionary_id, std::uint32_t item);
    [[nodiscard]] std::vector<std::string> MediaPaths(std::int64_t dictionary_id);

    /**
     * Rebuilds the library's index of every dictionary's keys. An import or a removal does not:
     * a dictionary the index does not cover is asked on its own (and one removed is passed
     * by), which is a little slower. Called once a batch of imports is over.
     */
    void Compact();
    /// How many dictionaries the index does not cover.
    [[nodiscard]] int Uncovered();

    /// For the benches: look keys up in every dictionary's own keys.bin instead of the
    /// library's index.
    void UseLibraryIndex(bool use);

private:
    struct Loaded;
    struct Index;

    ImportResult ImportWith(const ImportOptions& options,
                            const std::function<Detail::Written(const std::string&, const ImportOptions&,
                                                                const std::function<bool(const std::string&)>&)>& write);
    void LoadLocked(bool have_lock = false);
    /// Loads the library again when another process changed it (Android's app and its game
    /// process share one); a few microseconds when nothing changed.
    void RefreshLocked(bool have_lock = false);
    /// What an import that did not finish left (its folder, never listed), unless it may still
    /// be going on in another process.
    void RemoveLeftoversLocked();
    bool SaveLocked();
    void OpenIndexLocked();
    void BuildIndexLocked();
    [[nodiscard]] Loaded* FindLocked(std::int64_t id);

    std::string folder;
    std::mutex mutex;
    std::vector<std::unique_ptr<Loaded>> dictionaries; // in their order
    std::set<std::string> listed_folders;              // all folder names present in library.json
    bool library_valid = false;
    std::int64_t next_id = 1;
    std::unique_ptr<Index> terms_index;
    std::unique_ptr<Index> metas_index;
    std::vector<std::int64_t> covered; // the dictionaries the index was made of, sorted
    bool use_library_index = true;
    std::string seen_state; // library.json and index.stamp as last read (StateOf)
};

/// A dictionary's scope as kept: "|ID|ID|" from its games, "" for every game.
std::string ScopeText(const std::vector<std::string>& games);

} // namespace Dict
