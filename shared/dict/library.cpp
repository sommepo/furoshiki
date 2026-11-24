// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/library.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <zstd.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "dict/format.h"
#include "dict/importer.h"
#include "dict/json.h"
#include "dict/mapped_file.h"

namespace Dict {

namespace fs = std::filesystem;
using namespace Format;

std::string ScopeText(const std::vector<std::string>& games) {
    std::vector<std::string> ids;
    for (std::string game : games) {
        while (!game.empty() && (game.back() == ' ' || game.back() == '\t')) {
            game.pop_back();
        }
        while (!game.empty() && (game.front() == ' ' || game.front() == '\t')) {
            game.erase(game.begin());
        }
        std::transform(game.begin(), game.end(), game.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (!game.empty() && game.find('|') == std::string::npos &&
            std::find(ids.begin(), ids.end(), game) == ids.end()) {
            ids.push_back(game);
        }
    }
    if (ids.empty()) {
        return {};
    }
    std::string out = "|";
    for (const std::string& id : ids) {
        out += id;
        out += '|';
    }
    return out;
}

namespace {

fs::path PathOf(const std::string& utf8) {
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// Writes `text` to a new file beside `path` and puts it in its place, so that `path` is
/// never half written.
bool WriteWhole(const fs::path& path, const std::string& text) {
    fs::path temporary = path;
    temporary += ".new";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            fs::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool ValidFolderName(const std::string& name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (const char c : name) {
        if (c == '/' || c == '\\' || c == '\0') {
            return false;
        }
    }
    const fs::path path = PathOf(name);
    return path.is_relative() && path.filename() == path;
}

bool IsDictionaryFolderName(const std::string& name) {
    if (name.size() < 2 || name[0] != 'd') {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
    }
    return true;
}

std::string U8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

template <typename Record>
struct Table {
    MappedFile file;
    const Record* records = nullptr;
    std::uint32_t count = 0;

    bool Open(const fs::path& path, const char (&magic)[8]) {
        if (!file.Open(U8(path)) || file.Size() < sizeof(Header)) {
            return false;
        }
        Header header{};
        std::memcpy(&header, file.Data(), sizeof header);
        if (std::memcmp(header.magic, magic, 8) != 0 || header.version != Version ||
            sizeof(Header) + static_cast<std::uint64_t>(header.count) * sizeof(Record) > file.Size()) {
            return false;
        }
        records = reinterpret_cast<const Record*>(file.Data() + sizeof(Header));
        count = header.count;
        return true;
    }
};

} // namespace

/// A dictionary of the library, its files mapped.
struct Library::Loaded {
    DictionaryInfo info;
    MappedFile strings;
    Table<EntryRecord> entries;
    Table<DefinitionRecord> definitions;
    MappedFile definition_bytes;
    Table<KeyRecord> keys;
    Table<MetaRecord> metas;
    Table<KeyRecord> meta_keys;
    Table<MediaRecord> media;
    MappedFile media_bytes;
    Table<KanjiRecord> kanji; // none in a dictionary written before kanji were read
    Table<KeyRecord> kanji_keys;
    Table<MetaRecord> kanji_metas;
    Table<KeyRecord> kanji_meta_keys;
    std::string trained; // zdict.bin
    ZSTD_DDict* ddict = nullptr;
    bool ddict_tried = false;

    ~Loaded() {
        ZSTD_freeDDict(ddict);
    }

    [[nodiscard]] std::string_view String(std::uint32_t offset) const {
        return StringAt(strings.Data(), strings.Size(), offset);
    }

    bool Open(const fs::path& folder) {
        const auto file = [&folder](const char* name) { return U8(folder / name); };
        if (!strings.Open(file("strings.bin")) || !entries.Open(folder / "entries.bin", EntriesMagic) ||
            !definitions.Open(folder / "defs.idx", DefinitionsMagic) || !definition_bytes.Open(file("defs.bin")) ||
            !keys.Open(folder / "keys.bin", KeysMagic) || !metas.Open(folder / "metas.bin", MetasMagic) ||
            !meta_keys.Open(folder / "meta_keys.bin", KeysMagic) || !media.Open(folder / "media.idx", MediaMagic)) {
            return false;
        }
        if (media.count > 0 && !media_bytes.Open(file("media.pack"))) {
            return false;
        }
        std::error_code ec;
        if (fs::exists(folder / "kanji.bin", ec) && !ec &&
            (!kanji.Open(folder / "kanji.bin", KanjiMagic) || !kanji_keys.Open(folder / "kanji_keys.bin", KeysMagic) ||
             !kanji_metas.Open(folder / "kanji_metas.bin", MetasMagic) ||
             !kanji_meta_keys.Open(folder / "kanji_meta_keys.bin", KeysMagic))) {
            return false;
        }
        ec.clear();
        if (fs::exists(folder / "zdict.bin", ec) && !ec) {
            trained = ReadFile(folder / "zdict.bin");
        }
        return true;
    }

    /// Items of keys.bin (or meta_keys.bin) with this hash.
    template <typename Each>
    void ItemsOf(const Table<KeyRecord>& table, std::uint64_t hash, Each&& each) const {
        const KeyRecord* begin = table.records;
        const KeyRecord* end = begin + table.count;
        const KeyRecord* at = std::lower_bound(begin, end, hash,
                                               [](const KeyRecord& record, std::uint64_t h) { return record.hash < h; });
        for (; at < end && at->hash == hash; ++at) {
            each(at->item);
        }
    }
};

/// terms.idx or metas.idx: every dictionary's keys in one table.
struct Library::Index {
    MappedFile file;
    const IndexBucket* buckets = nullptr;
    const Posting* postings = nullptr;
    std::uint32_t mask = 0;
    std::uint64_t posting_count = 0;

    bool Open(const fs::path& path) {
        if (!file.Open(U8(path)) || file.Size() < sizeof(IndexHeader)) {
            return false;
        }
        IndexHeader header{};
        std::memcpy(&header, file.Data(), sizeof header);
        if (std::memcmp(header.magic, IndexMagic, 8) != 0 || header.version != Version || header.buckets == 0 ||
            (header.buckets & (header.buckets - 1)) != 0) {
            return false;
        }
        const std::uint64_t bucket_bytes = static_cast<std::uint64_t>(header.buckets) * sizeof(IndexBucket);
        const std::uint64_t available = file.Size() - sizeof(IndexHeader);
        if (bucket_bytes > available || header.postings > (available - bucket_bytes) / sizeof(Posting)) {
            return false;
        }
        buckets = reinterpret_cast<const IndexBucket*>(file.Data() + sizeof(IndexHeader));
        postings = reinterpret_cast<const Posting*>(buckets + header.buckets);
        mask = header.buckets - 1;
        posting_count = header.postings;
        return true;
    }

    template <typename Each>
    void PostingsOf(std::uint64_t hash, Each&& each) const {
        const std::uint32_t limit = mask + 1;
        for (std::uint32_t probe = 0, at = static_cast<std::uint32_t>(hash) & mask; probe < limit;
             ++probe, at = (at + 1) & mask) {
            const IndexBucket& bucket = buckets[at];
            if (bucket.count == 0) {
                return;
            }
            if (bucket.hash == hash) {
                if (bucket.start > posting_count || bucket.count > posting_count - bucket.start) {
                    return;
                }
                for (std::uint32_t i = 0; i < bucket.count; ++i) {
                    each(postings[static_cast<std::uint64_t>(bucket.start) + i]);
                }
                return;
            }
        }
    }
};

namespace {

/**
 * The library's lock between processes (Android's app and its game process both change it: the
 * dictionaries page, and a game's names dictionary as the game starts), held while the library
 * is changed. Within a process the library's mutex is taken first. Nothing on Windows, where one
 * program has the library.
 */
enum class LockMode { Exclusive, Shared };

class ProcessLock {
public:
    explicit ProcessLock(const std::string& folder, LockMode mode = LockMode::Exclusive) {
#ifndef _WIN32
        fd = ::open((folder + "/.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd >= 0) {
            const int op = mode == LockMode::Shared ? LOCK_SH : LOCK_EX;
            while (::flock(fd, op) != 0 && errno == EINTR) {
            }
        }
#else
        (void)folder;
        (void)mode;
#endif
    }
    ~ProcessLock() {
#ifndef _WIN32
        if (fd >= 0) {
            ::flock(fd, LOCK_UN);
            ::close(fd);
        }
#endif
    }
    ProcessLock(const ProcessLock&) = delete;
    ProcessLock& operator=(const ProcessLock&) = delete;

private:
    int fd = -1;
};

/// library.json's and index.stamp's times, sizes and inodes: changed when another process saved either.
std::string StateOf(const fs::path& root) {
    std::string out;
    for (const char* name : {"library.json", "index.stamp"}) {
        const fs::path path = root / name;
        std::error_code error;
        const auto time = fs::last_write_time(path, error);
        const auto size = error ? 0 : fs::file_size(path, error);
        if (error) {
            out += "-;";
            continue;
        }
        out += std::to_string(static_cast<long long>(time.time_since_epoch().count())) + "/" + std::to_string(size);
#ifndef _WIN32
        struct stat st {};
        if (::stat(U8(path).c_str(), &st) == 0) {
            out += "/" + std::to_string(static_cast<unsigned long long>(st.st_ino));
        }
#endif
        out += ';';
    }
    return out;
}

} // namespace

Library::Library(std::string folder_) : folder{std::move(folder_)} {
    std::lock_guard lock(mutex);
    // For the benches: every dictionary asked on its own, as without the library's index.
    use_library_index = std::getenv("FUROSHIKI_DICT_EACH") == nullptr;
    std::error_code error;
    fs::create_directories(PathOf(folder), error);
    ProcessLock shared(folder);
    LoadLocked(true);
    RemoveLeftoversLocked();
}

void Library::RefreshLocked(bool have_lock) {
    if (StateOf(PathOf(folder)) != seen_state) {
        LoadLocked(have_lock);
    }
}

Library::~Library() = default;

void Library::LoadLocked(bool have_lock) {
    std::optional<ProcessLock> read_lock;
    if (!have_lock) {
        read_lock.emplace(folder, LockMode::Shared);
    }
    const fs::path root = PathOf(folder);
    // Read before the files, so that a change made meanwhile is seen at the next call.
    seen_state = StateOf(root);
    // A dictionary's folder never changes once imported: what is open already is kept.
    std::unordered_map<std::string, std::unique_ptr<Loaded>> open;
    for (auto& loaded : dictionaries) {
        open.emplace(loaded->info.folder, std::move(loaded));
    }
    dictionaries.clear();
    listed_folders.clear();
    library_valid = false;
    terms_index.reset();
    metas_index.reset();
    const fs::path manifest = root / "library.json";
    std::error_code exists_ec;
    const bool manifest_exists = fs::exists(manifest, exists_ec);
    if (!exists_ec && !manifest_exists) {
        library_valid = true;
    } else if (!exists_ec) {
        Json::Value library;
        if (Json::Parse(ReadFile(manifest), library) && library.type == Json::Value::Type::Object) {
            library_valid = true;
            next_id = static_cast<std::int64_t>(library.Number("nextId", 1));
            if (const Json::Value* list = library.Get("dictionaries"); list != nullptr) {
                for (const Json::Value& item : list->array) {
                    auto loaded = std::make_unique<Loaded>();
                    loaded->info.id = static_cast<std::int64_t>(item.Number("id"));
                    loaded->info.folder = item.String("folder");
                    loaded->info.enabled = item.Bool("enabled", true);
                    if (!ValidFolderName(loaded->info.folder)) {
                        continue;
                    }
                    listed_folders.insert(loaded->info.folder);
                    next_id = std::max(next_id, loaded->info.id + 1);
                    if (const auto kept = open.find(loaded->info.folder); kept != open.end() && kept->second) {
                        std::unique_ptr<Loaded> same = std::move(kept->second);
                        same->info.id = loaded->info.id;
                        same->info.enabled = loaded->info.enabled;
                        dictionaries.push_back(std::move(same));
                        continue;
                    }
                    Json::Value info;
                    const fs::path path = root / PathOf(loaded->info.folder);
                    if (!Json::Parse(ReadFile(path / "info.json"), info) || !loaded->Open(path)) {
                        continue; // damaged: left out (and its folder kept, for whoever looks)
                    }
                    loaded->info.title = info.String("title");
                    loaded->info.revision = info.String("revision");
                    loaded->info.author = info.String("author");
                    loaded->info.description = info.String("description");
                    loaded->info.frequency_mode = info.String("frequencyMode");
                    loaded->info.scope = info.String("scope");
                    loaded->info.styles = info.String("styles");
                    loaded->info.terms = static_cast<std::int64_t>(info.Number("terms"));
                    loaded->info.metas = static_cast<std::int64_t>(info.Number("metas"));
                    loaded->info.media = static_cast<std::int64_t>(info.Number("media"));
                    loaded->info.kanji = static_cast<std::int64_t>(info.Number("kanji"));
                    dictionaries.push_back(std::move(loaded));
                }
            }
        }
    }
    for (std::size_t i = 0; i < dictionaries.size(); ++i) {
        dictionaries[i]->info.priority = static_cast<int>(i);
    }
    OpenIndexLocked();
}

void Library::RemoveLeftoversLocked() {
    const fs::path root = PathOf(folder);
    std::error_code error;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, error)) {
        const std::string name = U8(entry.path().filename());
        std::error_code dir_ec;
        if (!entry.is_directory(dir_ec) || dir_ec || listed_folders.contains(name)) {
            continue;
        }
        std::error_code ignored;
        if (name.starts_with(".import-")) {
            // Another process may be writing it now: only one a day old is a leftover.
            const auto age = fs::file_time_type::clock::now() - fs::last_write_time(entry.path(), ignored);
            if (!ignored && age > std::chrono::hours(24)) {
                fs::remove_all(entry.path(), ignored);
            }
        } else if (library_valid && IsDictionaryFolderName(name)) {
            // A dictionary joins the library under the lock held here: unlisted, it was removed.
            fs::remove_all(entry.path(), ignored);
        }
    }
}

void Library::OpenIndexLocked() {
    const fs::path root = PathOf(folder);
    covered.clear();
    terms_index = std::make_unique<Index>();
    metas_index = std::make_unique<Index>();
    if (!terms_index->Open(root / "terms.idx") || !metas_index->Open(root / "metas.idx")) {
        terms_index.reset();
        metas_index.reset();
        return;
    }
    std::istringstream stamp(ReadFile(root / "index.stamp"));
    std::string line;
    while (std::getline(stamp, line)) {
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::int64_t id = std::atoll(line.c_str());
        // Only if it is still that folder: a dictionary imported again under the same id is not.
        for (const auto& loaded : dictionaries) {
            if (loaded->info.id == id && loaded->info.folder == line.substr(colon + 1)) {
                covered.push_back(id);
            }
        }
    }
    std::sort(covered.begin(), covered.end());
}

bool Library::SaveLocked() {
    std::string out = "{\n  \"format\": " + std::to_string(Version) + ",\n  \"nextId\": " + std::to_string(next_id) +
                      ",\n  \"dictionaries\": [";
    for (std::size_t i = 0; i < dictionaries.size(); ++i) {
        const DictionaryInfo& info = dictionaries[i]->info;
        out += i == 0 ? "\n" : ",\n";
        out += "    {\"id\": " + std::to_string(info.id) + ", \"folder\": " + Json::Quote(info.folder) +
               ", \"enabled\": " + (info.enabled ? "true" : "false") + ", \"title\": " + Json::Quote(info.title) + "}";
        dictionaries[i]->info.priority = static_cast<int>(i);
    }
    out += "\n  ]\n}\n";
    if (!WriteWhole(PathOf(folder) / "library.json", out)) {
        return false;
    }
    listed_folders.clear();
    for (const auto& loaded : dictionaries) {
        listed_folders.insert(loaded->info.folder);
    }
    library_valid = true;
    seen_state = StateOf(PathOf(folder)); // this process's own change is not read again
    return true;
}

void Library::BuildIndexLocked() {
    const fs::path root = PathOf(folder);
    std::string stamp;
    for (const auto& loaded : dictionaries) {
        stamp += std::to_string(loaded->info.id) + ":" + loaded->info.folder + "\n";
    }
    terms_index.reset();
    metas_index.reset();
    covered.clear();
    std::error_code error;
    fs::remove(root / "index.stamp", error);
    // Every dictionary's keys are sorted by hash: merged in one pass.
    const auto build = [&](const char* name, Table<KeyRecord> Loaded::* member) {
        struct Cursor {
            const KeyRecord* at;
            const KeyRecord* end;
            std::uint32_t dictionary;
        };
        const auto later = [](const Cursor& a, const Cursor& b) {
            return a.at->hash != b.at->hash ? a.at->hash > b.at->hash : a.dictionary > b.dictionary;
        };
        std::priority_queue<Cursor, std::vector<Cursor>, decltype(later)> queue(later);
        std::uint64_t total = 0;
        for (const auto& loaded : dictionaries) {
            const Table<KeyRecord>& keys = (*loaded).*member;
            if (keys.count > 0) {
                queue.push(Cursor{keys.records, keys.records + keys.count, static_cast<std::uint32_t>(loaded->info.id)});
                total += keys.count;
            }
        }
        std::vector<Posting> postings;
        postings.reserve(static_cast<std::size_t>(total));
        std::vector<std::pair<std::uint64_t, std::uint32_t>> groups; // hash, first posting
        while (!queue.empty()) {
            Cursor cursor = queue.top();
            queue.pop();
            if (groups.empty() || groups.back().first != cursor.at->hash) {
                groups.emplace_back(cursor.at->hash, static_cast<std::uint32_t>(postings.size()));
            }
            postings.push_back(Posting{cursor.dictionary, cursor.at->item});
            if (++cursor.at < cursor.end) {
                queue.push(cursor);
            }
        }
        const std::uint64_t target = static_cast<std::uint64_t>(groups.size()) + groups.size() / 2;
        if (target > (1ull << 30)) {
            return false;
        }
        std::uint32_t buckets = 16;
        while (buckets < target) {
            buckets <<= 1;
        }
        std::vector<IndexBucket> table(buckets, IndexBucket{0, 0, 0});
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const std::uint32_t start = groups[g].second;
            const std::uint32_t stop = g + 1 < groups.size() ? groups[g + 1].second : static_cast<std::uint32_t>(postings.size());
            std::uint32_t at = static_cast<std::uint32_t>(groups[g].first) & (buckets - 1);
            while (table[at].count != 0) {
                at = (at + 1) & (buckets - 1);
            }
            table[at] = IndexBucket{groups[g].first, start, stop - start};
        }
        IndexHeader header{};
        std::memcpy(header.magic, IndexMagic, 8);
        header.version = Version;
        header.buckets = buckets;
        header.postings = postings.size();
        std::string bytes(sizeof header + table.size() * sizeof(IndexBucket) + postings.size() * sizeof(Posting), '\0');
        std::memcpy(bytes.data(), &header, sizeof header);
        std::memcpy(bytes.data() + sizeof header, table.data(), table.size() * sizeof(IndexBucket));
        if (!postings.empty()) {
            std::memcpy(bytes.data() + sizeof header + table.size() * sizeof(IndexBucket), postings.data(),
                        postings.size() * sizeof(Posting));
        }
        return WriteWhole(root / name, bytes);
    };
    if (!build("terms.idx", &Loaded::keys) || !build("metas.idx", &Loaded::meta_keys)) {
        return; // each dictionary is then asked on its own
    }
    WriteWhole(root / "index.stamp", stamp);
    seen_state = StateOf(root);
    OpenIndexLocked();
}

void Library::Compact() {
    std::lock_guard lock(mutex);
    ProcessLock shared(folder);
    RefreshLocked(true);
    BuildIndexLocked();
}

int Library::Uncovered() {
    std::lock_guard lock(mutex);
    RefreshLocked();
    int count = 0;
    for (const auto& loaded : dictionaries) {
        if (!std::binary_search(covered.begin(), covered.end(), loaded->info.id)) {
            ++count;
        }
    }
    return count;
}

Library::Loaded* Library::FindLocked(std::int64_t id) {
    for (const auto& loaded : dictionaries) {
        if (loaded->info.id == id) {
            return loaded.get();
        }
    }
    return nullptr;
}

std::vector<DictionaryInfo> Library::Dictionaries() {
    std::lock_guard lock(mutex);
    RefreshLocked();
    std::vector<DictionaryInfo> out;
    for (const auto& loaded : dictionaries) {
        out.push_back(loaded->info);
    }
    return out;
}

bool Library::HasEnabled() {
    std::lock_guard lock(mutex);
    RefreshLocked();
    return std::any_of(dictionaries.begin(), dictionaries.end(),
                       [](const auto& loaded) { return loaded->info.enabled; });
}

void Library::SetEnabled(std::int64_t id, bool enabled) {
    std::lock_guard lock(mutex);
    ProcessLock shared(folder);
    RefreshLocked(true);
    if (Loaded* loaded = FindLocked(id)) {
        loaded->info.enabled = enabled;
        SaveLocked();
    }
}

void Library::SetOrder(const std::vector<std::int64_t>& ids) {
    std::lock_guard lock(mutex);
    ProcessLock shared(folder);
    RefreshLocked(true);
    std::vector<std::unique_ptr<Loaded>> ordered;
    for (const std::int64_t id : ids) {
        for (auto& loaded : dictionaries) {
            if (loaded && loaded->info.id == id) {
                ordered.push_back(std::move(loaded));
            }
        }
    }
    for (auto& loaded : dictionaries) {
        if (loaded) {
            ordered.push_back(std::move(loaded));
        }
    }
    dictionaries = std::move(ordered);
    SaveLocked();
}

void Library::Delete(std::int64_t id) {
    std::lock_guard lock(mutex);
    ProcessLock shared(folder);
    RefreshLocked(true);
    const auto found = std::find_if(dictionaries.begin(), dictionaries.end(),
                                    [id](const auto& loaded) { return loaded->info.id == id; });
    if (found == dictionaries.end()) {
        return;
    }
    const fs::path path = PathOf(folder) / PathOf((*found)->info.folder);
    dictionaries.erase(found);
    SaveLocked();
    std::error_code error;
    fs::remove_all(path, error);
}

void Library::SetEnabled(const std::vector<std::int64_t>& ids, bool enabled) {
    std::lock_guard lock(mutex);
    ProcessLock shared(folder);
    RefreshLocked(true);
    for (const std::int64_t id : ids) {
        if (Loaded* loaded = FindLocked(id)) {
            loaded->info.enabled = enabled;
        }
    }
    SaveLocked();
}

void Library::Delete(const std::vector<std::int64_t>& ids) {
    std::vector<fs::path> gone;
    {
        std::lock_guard lock(mutex);
        ProcessLock shared(folder);
        RefreshLocked(true);
        for (const std::int64_t id : ids) {
            const auto found = std::find_if(dictionaries.begin(), dictionaries.end(),
                                            [id](const auto& loaded) { return loaded->info.id == id; });
            if (found != dictionaries.end()) {
                gone.push_back(PathOf(folder) / PathOf((*found)->info.folder));
                dictionaries.erase(found);
            }
        }
        SaveLocked();
    }
    // The folders go after the library no longer lists them (a crash meanwhile leaves folders
    // that the next opening removes).
    for (const fs::path& path : gone) {
        std::error_code error;
        fs::remove_all(path, error);
    }
}

ImportResult Library::Import(const std::string& zip_path, const ImportOptions& options) {
    return ImportWith(options, [&zip_path](const std::string& folder, const ImportOptions& given,
                                           const std::function<bool(const std::string&)>& taken) {
        return Detail::WriteDictionary(zip_path, folder, given, taken);
    });
}

ImportResult Library::ImportFrom(Source& source, const ImportOptions& options) {
    return ImportWith(options, [&source](const std::string& folder, const ImportOptions& given,
                                         const std::function<bool(const std::string&)>& taken) {
        return Detail::WriteDictionaryFrom(source, folder, given, taken);
    });
}

ImportResult Library::ImportWith(
    const ImportOptions& options,
    const std::function<Detail::Written(const std::string&, const ImportOptions&,
                                        const std::function<bool(const std::string&)>&)>& write) {
    ImportResult result;
    const fs::path root = PathOf(folder);
    std::random_device random;
    // Its id is taken when it joins the library (another process may import meanwhile).
    const std::string temporary = ".import-" + std::to_string(random()) + "-" + std::to_string(random());
    const fs::path work = root / temporary;
    const auto taken = [this](const std::string& title) {
        std::lock_guard lock(mutex);
        RefreshLocked();
        return std::any_of(dictionaries.begin(), dictionaries.end(),
                           [&title](const auto& loaded) { return loaded->info.title == title; });
    };
    ImportOptions given = options;
    if (const char* ahead = std::getenv("FUROSHIKI_DICT_AHEAD_MB")) { // for the benches
        const long long mb = std::atoll(ahead);
        if (mb > 0) {
            given.ahead_bytes = mb * 1024 * 1024;
        }
    }
    Detail::Written written = write(U8(work), given, taken);
    result.title = written.title;
    result.already_there = written.already_there;
    result.not_dictionary = written.not_dictionary;
    std::error_code error;
    if (!written.ok) {
        fs::remove_all(work, error);
        result.error = written.error;
        return result;
    }
    std::lock_guard lock(mutex);
    ProcessLock shared(folder);
    RefreshLocked(true);
    if (std::any_of(dictionaries.begin(), dictionaries.end(),
                    [&](const auto& loaded) { return loaded->info.title == written.title; })) {
        fs::remove_all(work, error);
        result.already_there = true;
        result.error = "\"" + written.title + "\" is already installed";
        return result;
    }
    const std::int64_t id = next_id;
    if (!WriteWhole(work / "info.json", Detail::InfoJson(written, id))) {
        fs::remove_all(work, error);
        result.error = "Couldn't save dictionary files (is the disk full?)";
        return result;
    }
    const std::string name = "d" + std::to_string(id);
    fs::rename(work, root / name, error);
    if (error) {
        fs::remove_all(work, error);
        result.error = "Couldn't move the dictionary into the library";
        return result;
    }
    auto loaded = std::make_unique<Loaded>();
    if (!loaded->Open(root / name)) {
        fs::remove_all(root / name, error);
        result.error = "Couldn't open the dictionary after importing it";
        return result;
    }
    loaded->info.id = id;
    loaded->info.folder = name;
    loaded->info.title = written.title;
    loaded->info.revision = written.revision;
    loaded->info.author = written.author;
    loaded->info.description = written.description;
    loaded->info.frequency_mode = written.frequency_mode;
    loaded->info.scope = written.scope;
    loaded->info.styles = written.styles;
    loaded->info.terms = written.terms;
    loaded->info.metas = written.metas;
    loaded->info.media = written.media;
    loaded->info.kanji = written.kanji;
    dictionaries.push_back(std::move(loaded));
    const std::int64_t saved_next_id = next_id;
    next_id = id + 1;
    if (!SaveLocked()) {
        dictionaries.pop_back();
        next_id = saved_next_id;
        fs::remove_all(root / name, error);
        result.error = "Couldn't save the dictionary list (is the disk full?)";
        return result;
    }
    result.ok = true;
    result.id = id;
    result.terms = written.terms;
    result.metas = written.metas;
    result.media = written.media;
    result.kanji = written.kanji;
    return result;
}

void Library::UseLibraryIndex(bool use) {
    std::lock_guard lock(mutex);
    use_library_index = use;
}

std::vector<Term> Library::FindTerms(const std::vector<std::string>& keys, const std::string& game) {
    std::lock_guard lock(mutex);
    RefreshLocked();
    std::vector<Term> out;
    if (keys.empty()) {
        return out;
    }
    std::string wanted_scope = game;
    std::transform(wanted_scope.begin(), wanted_scope.end(), wanted_scope.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    wanted_scope = "|" + wanted_scope + "|";
    std::unordered_map<std::uint32_t, const Loaded*> usable;
    for (const auto& loaded : dictionaries) {
        const DictionaryInfo& info = loaded->info;
        if (info.enabled && (info.scope.empty() || info.scope.find(wanted_scope) != std::string::npos)) {
            usable.emplace(static_cast<std::uint32_t>(info.id), loaded.get());
        }
    }
    if (usable.empty()) {
        return out;
    }
    std::unordered_set<std::uint64_t> seen;
    const auto term_of = [](const Loaded& loaded, std::uint32_t entry) {
        const EntryRecord& record = loaded.entries.records[entry];
        Term term;
        term.dictionary_id = loaded.info.id;
        term.entry = entry;
        term.dictionary = loaded.info.title;
        term.priority = loaded.info.priority;
        term.expression = loaded.String(record.expression);
        term.reading = loaded.String(record.reading);
        term.definition_tags = loaded.String(record.definition_tags);
        term.rules = loaded.String(record.rules);
        term.score = record.score;
        term.sequence = record.sequence;
        term.term_tags = loaded.String(record.term_tags);
        term.scoped = !loaded.info.scope.empty();
        return term;
    };
    // As SQLite read them: a few hundred keys at a time, each in byte order, by expression
    // and then by reading, a key's rows by dictionary and entry.
    constexpr std::size_t Chunk = 400;
    for (std::size_t from = 0; from < keys.size(); from += Chunk) {
        std::vector<std::string> chunk(keys.begin() + static_cast<std::ptrdiff_t>(from),
                                       keys.begin() + static_cast<std::ptrdiff_t>(std::min(keys.size(), from + Chunk)));
        std::sort(chunk.begin(), chunk.end());
        chunk.erase(std::unique(chunk.begin(), chunk.end()), chunk.end());
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> found(chunk.size());
        for (std::size_t k = 0; k < chunk.size(); ++k) {
            const std::uint64_t hash = Hash(chunk[k]);
            auto& items = found[k];
            const bool indexed = use_library_index && terms_index;
            if (indexed) {
                terms_index->PostingsOf(hash, [&](const Posting& posting) {
                    if (usable.contains(posting.dictionary)) {
                        items.emplace_back(posting.dictionary, posting.item);
                    }
                });
            }
            for (const auto& [id, loaded] : usable) {
                if (!indexed || !std::binary_search(covered.begin(), covered.end(), static_cast<std::int64_t>(id))) {
                    loaded->ItemsOf(loaded->keys, hash, [&, id = id](std::uint32_t item) { items.emplace_back(id, item); });
                }
            }
            std::sort(items.begin(), items.end());
        }
        for (const bool by_reading : {false, true}) {
            for (std::size_t k = 0; k < chunk.size(); ++k) {
                for (const auto& [id, item] : found[k]) {
                    const Loaded& loaded = *usable.at(id);
                    if (item >= loaded.entries.count) {
                        continue;
                    }
                    const EntryRecord& record = loaded.entries.records[item];
                    const std::string_view field = loaded.String(by_reading ? record.reading : record.expression);
                    if (field != chunk[k]) {
                        continue;
                    }
                    if (!seen.insert((static_cast<std::uint64_t>(id) << 32) | item).second) {
                        continue;
                    }
                    out.push_back(term_of(loaded, item));
                }
            }
        }
    }
    return out;
}

std::string Library::Definition(std::int64_t dictionary_id, std::uint32_t entry) {
    std::lock_guard lock(mutex);
    Loaded* loaded = FindLocked(dictionary_id);
    if (loaded == nullptr) {
        RefreshLocked();
        loaded = FindLocked(dictionary_id);
    }
    if (loaded == nullptr || entry >= loaded->entries.count) {
        return "[]";
    }
    const std::uint32_t index = loaded->entries.records[entry].definition;
    if (index >= loaded->definitions.count) {
        return "[]";
    }
    const DefinitionRecord& record = loaded->definitions.records[index];
    const std::size_t file_size = loaded->definition_bytes.Size();
    if (record.offset > file_size || record.stored_size > file_size - record.offset) {
        return "[]";
    }
    const char* bytes = reinterpret_cast<const char*>(loaded->definition_bytes.Data() + record.offset);
    const std::uint32_t size = record.size & ~Compressed;
    if ((record.size & Compressed) == 0) {
        return std::string(bytes, record.stored_size);
    }
    constexpr std::uint32_t MaxDefinitionBytes = 64u * 1024 * 1024;
    if (size > MaxDefinitionBytes) {
        return "[]";
    }
    thread_local std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> context(ZSTD_createDCtx(), ZSTD_freeDCtx);
    if (!context) {
        return "[]";
    }
    if (!loaded->trained.empty() && !loaded->ddict_tried) {
        loaded->ddict_tried = true;
        loaded->ddict = ZSTD_createDDict(loaded->trained.data(), loaded->trained.size());
    }
    std::string out(size, '\0');
    const std::size_t made =
        loaded->ddict != nullptr
            ? ZSTD_decompress_usingDDict(context.get(), out.data(), out.size(), bytes, record.stored_size, loaded->ddict)
            : ZSTD_decompressDCtx(context.get(), out.data(), out.size(), bytes, record.stored_size);
    if (ZSTD_isError(made) || made != size) {
        return "[]";
    }
    return out;
}

std::vector<Meta> Library::FindMeta(const std::vector<std::string>& expressions) {
    std::lock_guard lock(mutex);
    RefreshLocked();
    std::vector<Meta> out;
    std::unordered_map<std::uint32_t, const Loaded*> usable;
    for (const auto& loaded : dictionaries) {
        if (loaded->info.enabled) {
            usable.emplace(static_cast<std::uint32_t>(loaded->info.id), loaded.get());
        }
    }
    if (usable.empty() || expressions.empty()) {
        return out;
    }
    constexpr std::size_t Chunk = 400;
    for (std::size_t from = 0; from < expressions.size(); from += Chunk) {
        std::vector<std::string> chunk(
            expressions.begin() + static_cast<std::ptrdiff_t>(from),
            expressions.begin() + static_cast<std::ptrdiff_t>(std::min(expressions.size(), from + Chunk)));
        std::sort(chunk.begin(), chunk.end());
        chunk.erase(std::unique(chunk.begin(), chunk.end()), chunk.end());
        struct Found {
            int priority;
            std::uint32_t dictionary;
            std::size_t key;
            std::uint32_t item;
        };
        std::vector<Found> found;
        for (std::size_t k = 0; k < chunk.size(); ++k) {
            const std::uint64_t hash = Hash(chunk[k]);
            const auto take = [&](std::uint32_t id, std::uint32_t item) {
                const Loaded& loaded = *usable.at(id);
                if (item < loaded.metas.count && loaded.String(loaded.metas.records[item].expression) == chunk[k]) {
                    found.push_back(Found{loaded.info.priority, id, k, item});
                }
            };
            const bool indexed = use_library_index && metas_index;
            if (indexed) {
                metas_index->PostingsOf(hash, [&](const Posting& posting) {
                    if (usable.contains(posting.dictionary)) {
                        take(posting.dictionary, posting.item);
                    }
                });
            }
            for (const auto& [id, loaded] : usable) {
                if (!indexed || !std::binary_search(covered.begin(), covered.end(), static_cast<std::int64_t>(id))) {
                    loaded->ItemsOf(loaded->meta_keys, hash, [&, id = id](std::uint32_t item) { take(id, item); });
                }
            }
        }
        // ORDER BY priority, id; then as the index gave them (by expression, then row).
        std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            if (a.dictionary != b.dictionary) return a.dictionary < b.dictionary;
            if (a.key != b.key) return a.key < b.key;
            return a.item < b.item;
        });
        for (const Found& f : found) {
            const Loaded& loaded = *usable.at(f.dictionary);
            const MetaRecord& record = loaded.metas.records[f.item];
            out.push_back(Meta{loaded.info.title, loaded.info.frequency_mode, std::string(loaded.String(record.expression)),
                               std::string(loaded.String(record.mode)), std::string(loaded.String(record.data))});
        }
    }
    return out;
}

std::vector<Kanji> Library::FindKanji(const std::string& character) {
    std::lock_guard lock(mutex);
    RefreshLocked();
    std::vector<Kanji> out;
    const std::uint64_t hash = Hash(character);
    for (const auto& loaded : dictionaries) {
        if (!loaded->info.enabled || loaded->kanji.count == 0) {
            continue;
        }
        loaded->ItemsOf(loaded->kanji_keys, hash, [&](std::uint32_t item) {
            if (item >= loaded->kanji.count) {
                return;
            }
            const KanjiRecord& record = loaded->kanji.records[item];
            if (loaded->String(record.character) != character) {
                return;
            }
            out.push_back(Kanji{loaded->info.id, loaded->info.title, loaded->info.priority, character,
                                std::string(loaded->String(record.onyomi)), std::string(loaded->String(record.kunyomi)),
                                std::string(loaded->String(record.tags)), std::string(loaded->String(record.meanings)),
                                std::string(loaded->String(record.stats))});
        });
    }
    return out;
}

std::vector<Meta> Library::FindKanjiMeta(const std::string& character) {
    std::lock_guard lock(mutex);
    RefreshLocked();
    std::vector<Meta> out;
    const std::uint64_t hash = Hash(character);
    for (const auto& loaded : dictionaries) {
        if (!loaded->info.enabled || loaded->kanji_metas.count == 0) {
            continue;
        }
        loaded->ItemsOf(loaded->kanji_meta_keys, hash, [&](std::uint32_t item) {
            if (item >= loaded->kanji_metas.count) {
                return;
            }
            const MetaRecord& record = loaded->kanji_metas.records[item];
            if (loaded->String(record.expression) == character) {
                out.push_back(Meta{loaded->info.title, loaded->info.frequency_mode, character,
                                   std::string(loaded->String(record.mode)), std::string(loaded->String(record.data))});
            }
        });
    }
    return out;
}

std::optional<std::string> Library::Media(std::int64_t dictionary_id, const std::string& path) {
    std::lock_guard lock(mutex);
    Loaded* loaded = FindLocked(dictionary_id);
    if (loaded == nullptr) {
        RefreshLocked();
        loaded = FindLocked(dictionary_id);
    }
    if (loaded == nullptr || loaded->media.count == 0) {
        return std::nullopt;
    }
    const MediaRecord* begin = loaded->media.records;
    const MediaRecord* end = begin + loaded->media.count;
    const MediaRecord* at = std::lower_bound(begin, end, path, [loaded](const MediaRecord& record, const std::string& p) {
        return loaded->String(record.path) < std::string_view(p);
    });
    const std::size_t media_size = loaded->media_bytes.Size();
    if (at == end || loaded->String(at->path) != path ||
        at->offset > media_size || at->size > media_size - at->offset) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(loaded->media_bytes.Data() + at->offset), at->size);
}

std::vector<std::pair<std::string, std::string>> Library::Styles() {
    std::lock_guard lock(mutex);
    RefreshLocked();
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& loaded : dictionaries) {
        if (loaded->info.enabled && !loaded->info.styles.empty()) {
            out.emplace_back(loaded->info.title, loaded->info.styles);
        }
    }
    return out;
}

std::uint32_t Library::TermCount(std::int64_t dictionary_id) {
    std::lock_guard lock(mutex);
    const Loaded* loaded = FindLocked(dictionary_id);
    return loaded != nullptr ? loaded->entries.count : 0;
}

Term Library::TermAt(std::int64_t dictionary_id, std::uint32_t entry) {
    std::lock_guard lock(mutex);
    const Loaded* loaded = FindLocked(dictionary_id);
    Term term;
    if (loaded == nullptr || entry >= loaded->entries.count) {
        return term;
    }
    const EntryRecord& record = loaded->entries.records[entry];
    term.dictionary_id = dictionary_id;
    term.entry = entry;
    term.dictionary = loaded->info.title;
    term.priority = loaded->info.priority;
    term.expression = loaded->String(record.expression);
    term.reading = loaded->String(record.reading);
    term.definition_tags = loaded->String(record.definition_tags);
    term.rules = loaded->String(record.rules);
    term.score = record.score;
    term.sequence = record.sequence;
    term.term_tags = loaded->String(record.term_tags);
    term.scoped = !loaded->info.scope.empty();
    return term;
}

std::uint32_t Library::MetaCount(std::int64_t dictionary_id) {
    std::lock_guard lock(mutex);
    const Loaded* loaded = FindLocked(dictionary_id);
    return loaded != nullptr ? loaded->metas.count : 0;
}

Meta Library::MetaAt(std::int64_t dictionary_id, std::uint32_t item) {
    std::lock_guard lock(mutex);
    const Loaded* loaded = FindLocked(dictionary_id);
    if (loaded == nullptr || item >= loaded->metas.count) {
        return {};
    }
    const MetaRecord& record = loaded->metas.records[item];
    return Meta{loaded->info.title, loaded->info.frequency_mode, std::string(loaded->String(record.expression)),
                std::string(loaded->String(record.mode)), std::string(loaded->String(record.data))};
}

std::vector<std::string> Library::MediaPaths(std::int64_t dictionary_id) {
    std::lock_guard lock(mutex);
    std::vector<std::string> out;
    if (const Loaded* loaded = FindLocked(dictionary_id)) {
        for (std::uint32_t i = 0; i < loaded->media.count; ++i) {
            out.emplace_back(loaded->String(loaded->media.records[i].path));
        }
    }
    return out;
}

} // namespace Dict
