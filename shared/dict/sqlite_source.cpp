// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dict/sqlite_source.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <sqlite3.h>
#include <zlib.h>
#include "dict/json.h"

namespace Dict {

namespace {

/// A prepared statement, finalized when it goes.
class Statement {
public:
    Statement(sqlite3* db_, const char* sql) : db{db_} {
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("Couldn't read the old dictionaries file: ") + sqlite3_errmsg(db));
        }
    }
    ~Statement() {
        sqlite3_finalize(statement);
    }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement& Bind(int at, std::int64_t value) {
        sqlite3_bind_int64(statement, at, value);
        return *this;
    }
    Statement& Bind(int at, const std::string& value) {
        sqlite3_bind_text(statement, at, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        return *this;
    }
    bool Step() {
        const int rc = sqlite3_step(statement);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        throw std::runtime_error(std::string("Couldn't read the old dictionaries file: ") + sqlite3_errmsg(db));
    }
    std::int64_t Int(int column) const {
        return sqlite3_column_int64(statement, column);
    }
    std::string Text(int column) const {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
        return text ? std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(statement, column))) : std::string();
    }
    std::string Blob(int column) const {
        const void* data = sqlite3_column_blob(statement, column);
        return data ? std::string(static_cast<const char*>(data), static_cast<std::size_t>(sqlite3_column_bytes(statement, column)))
                    : std::string();
    }

private:
    sqlite3* db = nullptr;
    sqlite3_stmt* statement = nullptr;
};

std::string FileUri(const std::string& path) {
    std::string out = "file:";
    out.reserve(path.size() + 24);
    for (const char c : path) {
#ifdef _WIN32
        if (c == '\\') {
            out += '/';
            continue;
        }
#endif
        if (c == '%') {
            out += "%25";
        } else if (c == '?') {
            out += "%3F";
        } else if (c == '#') {
            out += "%23";
        } else {
            out += c;
        }
    }
    out += "?immutable=1";
    return out;
}

} // namespace

struct SqliteSource::Impl {
    sqlite3* db = nullptr;
    std::mutex mutex; // Read is called from several threads
    std::int64_t id = 0;
    std::string index_json;
    std::string styles;
    std::vector<std::string> terms;
    std::vector<std::string> metas;
    std::vector<std::string> media;

    ~Impl() {
        if (db != nullptr) {
            sqlite3_close(db);
        }
    }
};

SqliteSource::SqliteSource(const std::string& path, std::int64_t dictionary_id) : impl(std::make_unique<Impl>()) {
    impl->id = dictionary_id;
    // Immutable: no locks taken. The app's own SQLite has the file open in the same process, and
    // a second copy's locks and their release on close would get in each other's way.
    const std::string uri = FileUri(path);
    if (sqlite3_open_v2(uri.c_str(), &impl->db, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_URI, nullptr) !=
        SQLITE_OK) {
        const std::string error = impl->db ? sqlite3_errmsg(impl->db) : "unable to open file";
        throw std::runtime_error("Couldn't open the old dictionaries file: " + error);
    }
    Statement info(impl->db, "SELECT title, revision, author, description, frequency_mode, scope, styles "
                             "FROM dictionaries WHERE id = ?");
    info.Bind(1, dictionary_id);
    if (!info.Step()) {
        throw std::runtime_error("Dictionary " + std::to_string(dictionary_id) + " wasn't found in the old file");
    }
    std::string index = "{\"title\":" + Json::Quote(info.Text(0)) + ",\"revision\":" + Json::Quote(info.Text(1)) +
                        ",\"author\":" + Json::Quote(info.Text(2)) + ",\"description\":" + Json::Quote(info.Text(3)) +
                        ",\"frequencyMode\":" + Json::Quote(info.Text(4)) + ",\"format\":3";
    // "|ID|ID|": the games a names dictionary is for.
    const std::string scope = info.Text(5);
    std::string games;
    for (std::size_t at = 0; at < scope.size();) {
        const std::size_t end = scope.find('|', at);
        const std::string game = scope.substr(at, end == std::string::npos ? std::string::npos : end - at);
        if (!game.empty()) {
            games += (games.empty() ? "" : ",") + Json::Quote(game);
        }
        if (end == std::string::npos) {
            break;
        }
        at = end + 1;
    }
    if (!games.empty()) {
        index += ",\"furoshikiScope\":[" + games + "]";
    }
    impl->index_json = index + "}";
    impl->styles = info.Text(6);
    // The rows in runs, by id: a run is a bank.
    const auto runs = [this](const char* sql, std::size_t per, char tag, std::vector<std::string>& out) {
        std::vector<std::int64_t> ids;
        Statement all(impl->db, sql);
        all.Bind(1, impl->id);
        while (all.Step()) {
            ids.push_back(all.Int(0));
        }
        for (std::size_t at = 0; at < ids.size(); at += per) {
            const std::size_t last = std::min(ids.size(), at + per) - 1;
            out.push_back(std::string(1, tag) + std::to_string(ids[at]) + ":" + std::to_string(ids[last]));
        }
    };
    runs("SELECT id FROM terms WHERE dict = ? ORDER BY id", 10000, 't', impl->terms);
    runs("SELECT id FROM term_meta WHERE dict = ? ORDER BY id", 50000, 'm', impl->metas);
    Statement paths(impl->db, "SELECT path FROM media WHERE dict = ? ORDER BY path");
    paths.Bind(1, impl->id);
    while (paths.Step()) {
        impl->media.push_back("p" + paths.Text(0));
    }
}

SqliteSource::~SqliteSource() = default;

std::string SqliteSource::Index() {
    return impl->index_json;
}
std::vector<std::string> SqliteSource::TermBanks() {
    return impl->terms;
}
std::vector<std::string> SqliteSource::MetaBanks() {
    return impl->metas;
}
std::vector<std::string> SqliteSource::Media() {
    return impl->media;
}
std::string SqliteSource::MediaPath(const std::string& name) {
    return name.substr(1);
}
std::uint64_t SqliteSource::Size(const std::string& name) {
    return name.front() == 't' ? 10000ull * 1500 : name.front() == 'm' ? 50000ull * 60 : 64 * 1024;
}
std::string SqliteSource::Styles() {
    return impl->styles;
}

std::string SqliteSource::Unpack(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    if (size == 0) {
        return "[]";
    }
    if (bytes[0] == 0) {
        return std::string(reinterpret_cast<const char*>(bytes + 1), size - 1);
    }
    if (bytes[0] != 1) {
        throw std::runtime_error("A definition in the old dictionaries file uses an unsupported format");
    }
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        throw std::runtime_error("Couldn't initialize decompression");
    }
    struct EndInflate {
        z_stream& s;
        ~EndInflate() {
            inflateEnd(&s);
        }
    } end{stream};
    stream.next_in = const_cast<Bytef*>(bytes + 1);
    stream.avail_in = static_cast<uInt>(size - 1);
    std::string out;
    char buffer[16384];
    constexpr std::size_t MaxGlossaryBytes = 64u * 1024 * 1024;
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof buffer;
        status = inflate(&stream, Z_NO_FLUSH);
        const std::size_t made = sizeof buffer - stream.avail_out;
        if (out.size() + made > MaxGlossaryBytes) {
            throw std::runtime_error("A definition in the old dictionaries file is damaged");
        }
        out.append(buffer, made);
    }
    if (status != Z_STREAM_END) {
        throw std::runtime_error("Couldn't unpack a definition from the old dictionaries file");
    }
    return out;
}

std::string SqliteSource::Read(const std::string& name) {
    std::lock_guard lock(impl->mutex);
    if (name.front() == 'p') {
        Statement picture(impl->db, "SELECT data FROM media WHERE dict = ? AND path = ?");
        picture.Bind(1, impl->id).Bind(2, name.substr(1));
        if (!picture.Step()) {
            throw std::runtime_error("Missing picture in the old dictionaries file: " + name.substr(1));
        }
        return picture.Blob(0);
    }
    const std::size_t colon = name.find(':');
    const std::int64_t from = std::stoll(name.substr(1, colon - 1));
    const std::int64_t to = std::stoll(name.substr(colon + 1));
    std::string out = "[";
    bool first = true;
    if (name.front() == 't') {
        Statement rows(impl->db, "SELECT expression, reading, def_tags, rules, score, glossary, sequence, term_tags "
                                 "FROM terms WHERE dict = ? AND id BETWEEN ? AND ? ORDER BY id");
        rows.Bind(1, impl->id).Bind(2, from).Bind(3, to);
        while (rows.Step()) {
            out += first ? "[" : ",[";
            first = false;
            const std::string glossary = rows.Blob(5);
            out += Json::Quote(rows.Text(0)) + "," + Json::Quote(rows.Text(1)) + "," + Json::Quote(rows.Text(2)) + "," +
                   Json::Quote(rows.Text(3)) + "," + std::to_string(rows.Int(4)) + "," +
                   Unpack(glossary.data(), glossary.size()) + "," + std::to_string(rows.Int(6)) + "," +
                   Json::Quote(rows.Text(7)) + "]";
        }
    } else {
        Statement rows(impl->db, "SELECT expression, mode, data FROM term_meta WHERE dict = ? AND id BETWEEN ? AND ? "
                                 "ORDER BY id");
        rows.Bind(1, impl->id).Bind(2, from).Bind(3, to);
        while (rows.Step()) {
            out += first ? "[" : ",[";
            first = false;
            const std::string data = rows.Text(2);
            out += Json::Quote(rows.Text(0)) + "," + Json::Quote(rows.Text(1)) + "," + (data.empty() ? "null" : data) + "]";
        }
    }
    return out + "]";
}

} // namespace Dict
