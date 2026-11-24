// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// Importing a Yomitan dictionary into a folder of the engine's files (format.h). The banks are
// read, parsed and compressed on every core but one, which writes them in their order.

#include "dict/importer.h"

#include <algorithm>
#include <cstdlib>
#include <charconv>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <zdict.h>
#include <zstd.h>
#include "dict/format.h"
#include "dict/json.h"
#include "dict/zip_reader.h"

namespace Dict::Detail {

namespace {

namespace fs = std::filesystem;
using namespace Format;

struct Failure {
    std::string message;
};

/// One definition, ready to be written: compressed, or as it was.
struct Definition {
    std::uint64_t hash_a = 0;
    std::uint64_t hash_b = 0;
    std::string bytes;
    std::uint32_t size = 0; ///< as JSON
    bool compressed = false;
};

/// A bank's own short texts, each once, as strings.bin has them: the writer appends the
/// block and moves its offsets along, so that it does no more than copy.
class LocalStrings {
public:
    LocalStrings() {
        data.push_back('\0'); // "" at 0
    }

    std::uint32_t Add(std::string_view text) {
        if (text.empty()) {
            return 0;
        }
        const auto found = offsets.find(text);
        if (found != offsets.end()) {
            return found->second;
        }
        const auto offset = static_cast<std::uint32_t>(data.size());
        std::uint32_t length = static_cast<std::uint32_t>(text.size());
        while (length >= 0x80) {
            data.push_back(static_cast<char>((length & 0x7F) | 0x80));
            length >>= 7;
        }
        data.push_back(static_cast<char>(length));
        data.append(text);
        // The key views the copy kept in `texts`, which never moves.
        texts.emplace_back(text);
        offsets.emplace(texts.back(), offset);
        return offset;
    }

    /// A text not looked for among the others (a frequency's value: they are nearly all new).
    std::uint32_t AddOnce(std::string_view text) {
        if (text.empty()) {
            return 0;
        }
        const auto offset = static_cast<std::uint32_t>(data.size());
        std::uint32_t length = static_cast<std::uint32_t>(text.size());
        while (length >= 0x80) {
            data.push_back(static_cast<char>((length & 0x7F) | 0x80));
            length >>= 7;
        }
        data.push_back(static_cast<char>(length));
        data.append(text);
        return offset;
    }

    std::string data;

private:
    std::deque<std::string> texts;
    std::unordered_map<std::string_view, std::uint32_t> offsets;
};

/// A bank (or a run of pictures) made ready off the writing thread: records whose texts are
/// offsets into its own `strings` and whose definitions are its own.
struct Prepared {
    LocalStrings strings;
    std::vector<EntryRecord> terms;
    std::vector<Definition> definitions;
    std::vector<KeyRecord> keys; // items: its own terms
    std::vector<MetaRecord> metas;
    std::vector<KeyRecord> meta_keys; // items: its own metas
    std::vector<KanjiRecord> kanji;
    std::vector<KeyRecord> kanji_keys;
    std::vector<MetaRecord> kanji_metas;
    std::vector<KeyRecord> kanji_meta_keys;
    std::vector<std::pair<std::string, std::string>> media; // name in the zip, bytes
};

/// Definitions shorter than this are kept as they are: a frame would not be smaller.
constexpr std::size_t SmallestCompressed = 48;
/// Dictionaries with less definition text than this get no trained zstd dictionary.
constexpr std::uint64_t TrainFrom = 4ull * 1024 * 1024;
constexpr std::size_t DictionarySize = 64 * 1024;
constexpr std::size_t SampleBytes = 2 * 1024 * 1024;

int Level() {
    static const int level = [] {
        if (const char* text = std::getenv("FUROSHIKI_DICT_LEVEL")) {
            return std::max(1, std::atoi(text));
        }
        return 3;
    }();
    return level;
}

std::string ReadEntry(Source& source, const std::string& name) {
    try {
        return source.Read(name);
    } catch (const std::runtime_error& error) {
        throw Failure{error.what()};
    }
}

/// The glossary of a format-1 row: its remaining values, kept as strings, as a JSON array.
std::string Format1Glossary(const std::vector<std::string_view>& f) {
    std::string glosses = "[";
    for (std::size_t i = 5; i < f.size(); ++i) {
        if (i > 5) {
            glosses.push_back(',');
        }
        if (!f[i].empty() && f[i].front() == '"') {
            glosses.append(f[i]);
        } else {
            glosses.append(Json::Quote(Json::Text(f[i])));
        }
    }
    glosses.push_back(']');
    return glosses;
}

class Compressor {
public:
    explicit Compressor(const ZSTD_CDict* dictionary_) : dictionary{dictionary_}, context{ZSTD_createCCtx()} {}
    ~Compressor() {
        ZSTD_freeCCtx(context);
    }
    Compressor(const Compressor&) = delete;
    Compressor& operator=(const Compressor&) = delete;

    Definition Make(std::string_view json) {
        if (json.size() >= Compressed) {
            throw Failure{"A dictionary definition is too large to import"};
        }
        Definition out;
        out.hash_a = Hash(json, 0x6a09e667f3bcc908ull);
        out.hash_b = Hash(json, 0xbb67ae8584caa73bull);
        out.size = static_cast<std::uint32_t>(json.size());
        if (json.size() >= SmallestCompressed) {
            out.bytes.resize(ZSTD_compressBound(json.size()));
            const std::size_t made =
                dictionary != nullptr
                    ? ZSTD_compress_usingCDict(context, out.bytes.data(), out.bytes.size(), json.data(), json.size(),
                                               dictionary)
                    : ZSTD_compressCCtx(context, out.bytes.data(), out.bytes.size(), json.data(), json.size(), Level());
            if (!ZSTD_isError(made) && made < json.size()) {
                out.bytes.resize(made);
                out.compressed = true;
                return out;
            }
        }
        out.bytes.assign(json);
        return out;
    }

private:
    const ZSTD_CDict* dictionary;
    ZSTD_CCtx* context;
};

Prepared PrepareTerms(const char* begin, const char* end, const std::string& name, int format,
                      const ZSTD_CDict* dictionary) {
    Prepared out;
    Compressor compressor(dictionary);
    // Within a bank, a definition met again is not compressed again.
    std::unordered_map<std::uint64_t, std::uint32_t> known;
    try {
        Json::ForEachRowIn(begin, end, [&](const std::vector<std::string_view>& f) {
            if (f.size() < 5 || f[0].empty() || f[0].front() != '"') {
                return;
            }
            const std::string expression = Json::StringText(f[0]);
            std::string reading = Json::Text(f[1]);
            // An empty reading means the expression is its own reading.
            if (reading == expression) {
                reading.clear();
            }
            EntryRecord record{};
            record.expression = out.strings.Add(expression);
            record.reading = out.strings.Add(reading);
            record.definition_tags = out.strings.Add(Json::Text(f[2]));
            record.rules = out.strings.Add(Json::Text(f[3]));
            record.score = static_cast<std::int32_t>(Json::Number(f[4]));
            std::string format1;
            std::string_view json;
            if (format >= 3) {
                json = f.size() > 5 ? f[5] : std::string_view("[]");
                if (f.size() > 6) {
                    record.sequence = Json::Number(f[6]);
                }
                if (f.size() > 7) {
                    record.term_tags = out.strings.Add(Json::Text(f[7]));
                }
            } else {
                format1 = Format1Glossary(f);
                json = format1;
            }
            const auto at = static_cast<std::uint32_t>(out.terms.size());
            out.keys.push_back(KeyRecord{Hash(expression), at, 0});
            if (!reading.empty()) {
                out.keys.push_back(KeyRecord{Hash(reading), at, 0});
            }
            const std::uint64_t quick = Hash(json, 0x3c6ef372fe94f82bull);
            const auto found = known.find(quick);
            if (found != known.end() && out.definitions[found->second].size == json.size() &&
                out.definitions[found->second].hash_a == Hash(json, 0x6a09e667f3bcc908ull)) {
                record.definition = found->second;
            } else {
                record.definition = static_cast<std::uint32_t>(out.definitions.size());
                known.emplace(quick, record.definition);
                out.definitions.push_back(compressor.Make(json));
            }
            out.terms.push_back(record);
        });
    } catch (const Json::Malformed&) {
        throw Failure{"Couldn't read " + name + " in the dictionary. Download it again"};
    }
    return out;
}

Prepared PrepareMetas(const char* begin, const char* end, const std::string& name) {
    Prepared out;
    try {
        Json::ForEachRowIn(begin, end, [&](const std::vector<std::string_view>& f) {
            if (f.size() < 3 || f[0].empty() || f[0].front() != '"') {
                return;
            }
            const std::string expression = Json::StringText(f[0]);
            const auto at = static_cast<std::uint32_t>(out.metas.size());
            out.metas.push_back(
                MetaRecord{out.strings.AddOnce(expression), out.strings.Add(Json::Text(f[1])), out.strings.AddOnce(f[2])});
            out.meta_keys.push_back(KeyRecord{Hash(expression), at, 0});
        });
    } catch (const Json::Malformed&) {
        throw Failure{"Couldn't read " + name + " in the dictionary. Download it again"};
    }
    return out;
}

/// A kanji bank's rows: [character, onyomi, kunyomi, tags, meanings[], stats{}] (format 3), or
/// with the meanings as the remaining values (format 1).
Prepared PrepareKanji(const char* begin, const char* end, const std::string& name, int format) {
    Prepared out;
    try {
        Json::ForEachRowIn(begin, end, [&](const std::vector<std::string_view>& f) {
            if (f.size() < 4 || f[0].empty() || f[0].front() != '"') {
                return;
            }
            const std::string character = Json::StringText(f[0]);
            std::string meanings;
            std::string stats = "{}";
            if (format >= 3) {
                meanings = f.size() > 4 && !f[4].empty() && f[4].front() == '[' ? std::string(f[4]) : std::string("[]");
                if (f.size() > 5 && !f[5].empty() && f[5].front() == '{') {
                    stats = std::string(f[5]);
                }
            } else {
                meanings = "[";
                for (std::size_t i = 4; i < f.size(); ++i) {
                    meanings += i > 4 ? "," : "";
                    meanings += !f[i].empty() && f[i].front() == '"' ? std::string(f[i]) : Json::Quote(Json::Text(f[i]));
                }
                meanings += "]";
            }
            const auto at = static_cast<std::uint32_t>(out.kanji.size());
            out.kanji.push_back(KanjiRecord{out.strings.Add(character), out.strings.Add(Json::Text(f[1])),
                                            out.strings.Add(Json::Text(f[2])), out.strings.Add(Json::Text(f[3])),
                                            out.strings.AddOnce(meanings), out.strings.AddOnce(stats)});
            out.kanji_keys.push_back(KeyRecord{Hash(character), at, 0});
        });
    } catch (const Json::Malformed&) {
        throw Failure{"Couldn't read " + name + " in the dictionary. Download it again"};
    }
    return out;
}

Prepared PrepareKanjiMetas(const char* begin, const char* end, const std::string& name) {
    Prepared out = PrepareMetas(begin, end, name);
    out.kanji_metas = std::move(out.metas);
    out.kanji_meta_keys = std::move(out.meta_keys);
    out.metas.clear();
    out.meta_keys.clear();
    return out;
}

/// Rows read in pieces of this size at the same time: a bank can be a million rows.
constexpr std::size_t Piece = 4 * 1024 * 1024;

/// A bank read whole, then its pieces at the same time, in their order.
template <typename Prepare>
std::vector<Prepared> ReadBankInPieces(Source& zip, const std::string& name, Prepare&& prepare) {
    const std::string bytes = ReadEntry(zip, name);
    std::vector<std::pair<const char*, const char*>> pieces;
    try {
        pieces = Json::SplitRows(bytes, Piece);
    } catch (const Json::Malformed&) {
        throw Failure{"Couldn't read " + name + " in the dictionary. Download it again"};
    }
    std::vector<std::future<Prepared>> later;
    for (std::size_t i = 1; i < pieces.size(); ++i) {
        later.push_back(std::async(std::launch::async, [&prepare, piece = pieces[i]] {
            return prepare(piece.first, piece.second);
        }));
    }
    std::vector<Prepared> out;
    out.reserve(pieces.size());
    std::exception_ptr failed;
    try {
        out.push_back(prepare(pieces.front().first, pieces.front().second));
    } catch (...) {
        failed = std::current_exception();
    }
    for (auto& future : later) {
        try {
            out.push_back(future.get());
        } catch (...) {
            if (!failed) {
                failed = std::current_exception();
            }
        }
    }
    if (failed) {
        std::rethrow_exception(failed);
    }
    return out;
}

std::vector<Prepared> ReadTermBank(Source& zip, const std::string& name, int format,
                                   const ZSTD_CDict* dictionary) {
    return ReadBankInPieces(zip, name, [&](const char* begin, const char* end) {
        return PrepareTerms(begin, end, name, format, dictionary);
    });
}

std::vector<Prepared> ReadKanjiBank(Source& zip, const std::string& name, int format) {
    return ReadBankInPieces(zip, name, [&](const char* begin, const char* end) { return PrepareKanji(begin, end, name, format); });
}

std::vector<Prepared> ReadKanjiMetaBank(Source& zip, const std::string& name) {
    return ReadBankInPieces(zip, name, [&](const char* begin, const char* end) { return PrepareKanjiMetas(begin, end, name); });
}

std::vector<Prepared> ReadMetaBank(Source& zip, const std::string& name) {
    return ReadBankInPieces(zip, name, [&](const char* begin, const char* end) { return PrepareMetas(begin, end, name); });
}

std::vector<Prepared> ReadMedia(Source& zip, const std::vector<std::string>& names) {
    std::vector<Prepared> out(1);
    out.front().media.reserve(names.size());
    for (const std::string& name : names) {
        out.front().media.emplace_back(name, ReadEntry(zip, name));
    }
    return out;
}

/// A zstd dictionary trained on a sample of the definitions, or nothing for a small dictionary.
std::string Train(Source& zip, const std::vector<std::string>& term_banks, int format, std::uint64_t bank_bytes,
                  const std::function<void()>& keep_going) {
    if (term_banks.empty() || bank_bytes < TrainFrom) {
        return {};
    }
    std::string samples;
    std::vector<std::size_t> sizes;
    // From the first banks, every so many definitions, so that the sample is spread out.
    for (const std::string& name : term_banks) {
        keep_going();
        const std::string bytes = ReadEntry(zip, name);
        std::vector<std::string_view> all;
        std::vector<std::string> format1;
        try {
            Json::ForEachRow(bytes, [&](const std::vector<std::string_view>& f) {
                if (f.size() < 5) {
                    return;
                }
                if (format >= 3) {
                    if (f.size() > 5 && f[5].size() >= SmallestCompressed) {
                        all.push_back(f[5]);
                    }
                } else {
                    format1.push_back(Format1Glossary(f));
                }
            });
        } catch (const Json::Malformed&) {
            return {};
        }
        for (const std::string& text : format1) {
            all.push_back(text);
        }
        std::size_t total = 0;
        for (const std::string_view text : all) {
            total += text.size();
        }
        const std::size_t step = std::max<std::size_t>(1, total / SampleBytes);
        for (std::size_t i = 0; i < all.size() && samples.size() < SampleBytes; i += step) {
            samples.append(all[i]);
            sizes.push_back(all[i].size());
        }
        if (samples.size() >= SampleBytes / 2) {
            break;
        }
    }
    if (sizes.size() < 100) {
        return {};
    }
    keep_going();
    std::string dictionary(DictionarySize, '\0');
    const std::size_t made = ZDICT_trainFromBuffer(dictionary.data(), dictionary.size(), samples.data(), sizes.data(),
                                                   static_cast<unsigned>(sizes.size()));
    if (ZDICT_isError(made)) {
        return {};
    }
    dictionary.resize(made);
    return dictionary;
}

/// strings.bin, made of the banks' own blocks and a few texts of the writer's.
class StringFile {
public:
    StringFile() {
        data.push_back('\0'); // "" at 0
    }

    /// Appends a bank's block; its offsets are then this much further on.
    std::uint32_t Append(const std::string& block) {
        const auto base = static_cast<std::uint32_t>(data.size());
        data.append(block);
        return base;
    }

    std::uint32_t Add(std::string_view text) {
        if (text.empty()) {
            return 0;
        }
        const auto offset = static_cast<std::uint32_t>(data.size());
        std::uint32_t length = static_cast<std::uint32_t>(text.size());
        while (length >= 0x80) {
            data.push_back(static_cast<char>((length & 0x7F) | 0x80));
            length >>= 7;
        }
        data.push_back(static_cast<char>(length));
        data.append(text);
        return offset;
    }

    [[nodiscard]] const std::string& Data() const {
        return data;
    }

private:
    std::string data;
};

struct Key128 {
    std::uint64_t a;
    std::uint64_t b;
    bool operator==(const Key128&) const = default;
};

struct Key128Hash {
    std::size_t operator()(const Key128& key) const {
        return static_cast<std::size_t>(key.a);
    }
};

class Output {
public:
    explicit Output(const fs::path& path) : stream(path, std::ios::binary | std::ios::trunc) {
        if (!stream) {
            const std::u8string u8 = path.filename().u8string();
            throw Failure{"Couldn't write dictionary file (" + std::string(u8.begin(), u8.end()) + ")"};
        }
    }

    void Write(const void* data, std::size_t size) {
        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        written += size;
    }

    void Close() {
        stream.close();
        if (!stream) {
            throw Failure{"Couldn't save dictionary files (is the disk full?)"};
        }
    }

    [[nodiscard]] std::uint64_t Written() const {
        return written;
    }

private:
    std::ofstream stream;
    std::uint64_t written = 0;
};

template <typename Record>
void WriteTable(const fs::path& path, const char (&magic)[8], const std::vector<Record>& records) {
    Output out(path);
    Header header{};
    std::memcpy(header.magic, magic, 8);
    header.version = Version;
    header.count = static_cast<std::uint32_t>(records.size());
    out.Write(&header, sizeof header);
    if (!records.empty()) {
        out.Write(records.data(), records.size() * sizeof(Record));
    }
    out.Close();
}

void WriteText(const fs::path& path, const std::string& text) {
    Output out(path);
    out.Write(text.data(), text.size());
    out.Close();
}

} // namespace

namespace {

/// A Yomitan zip as a source: its banks by their numbers, its pictures, its stylesheet.
class ZipSource : public Source {
public:
    /// Throws Failure; `not_dictionary` says the zip has no index.json at all.
    ZipSource(const std::string& zip_path, bool& not_dictionary) {
        std::string error;
        if (!zip.Open(zip_path, error)) {
            throw Failure{error == "Couldn't open the file" ? "Couldn't open this zip file. Choose a Yomitan dictionary (.zip)"
                                                             : error};
        }
        std::string index_name;
        for (const ZipArchive::Entry& entry : zip.Entries()) {
            const std::string& name = entry.name;
            if (name == "index.json" || (name.size() > 11 && name.ends_with("/index.json"))) {
                if (index_name.empty() || name.size() < index_name.size()) {
                    index_name = name;
                }
            }
        }
        if (index_name.empty()) {
            not_dictionary = true;
            throw Failure{"This zip file isn't a Yomitan dictionary"};
        }
        index = index_name;
        prefix = index_name.substr(0, index_name.size() - 10);
    }

    std::string Index() override {
        return Read(index);
    }
    std::vector<std::string> TermBanks() override {
        return Banks("term_bank");
    }
    std::vector<std::string> MetaBanks() override {
        return Banks("term_meta_bank");
    }
    std::vector<std::string> KanjiBanks() override {
        return Banks("kanji_bank");
    }
    std::vector<std::string> KanjiMetaBanks() override {
        return Banks("kanji_meta_bank");
    }
    std::vector<std::string> Media() override {
        std::vector<std::string> media;
        for (const ZipArchive::Entry& entry : zip.Entries()) {
            if (entry.name.starts_with(prefix)) {
                std::string lower = entry.name;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                for (const char* extension :
                     {".png", ".jpg", ".jpeg", ".gif", ".webp", ".avif", ".svg", ".bmp", ".ico"}) {
                    if (lower.ends_with(extension)) {
                        media.push_back(entry.name);
                        break;
                    }
                }
            }
        }
        return media;
    }
    std::string MediaPath(const std::string& name) override {
        return name.substr(prefix.size());
    }
    std::uint64_t Size(const std::string& name) override {
        const ZipArchive::Entry* entry = zip.Find(name);
        return entry != nullptr ? entry->size : 0;
    }
    std::string Read(const std::string& name) override {
        const ZipArchive::Entry* entry = zip.Find(name);
        std::string out;
        if (entry == nullptr || !zip.Read(*entry, out)) {
            throw std::runtime_error("Couldn't read " + name + " in the zip file. Download it again");
        }
        return out;
    }
    std::string Styles() override {
        return zip.Find(prefix + "styles.css") != nullptr ? Read(prefix + "styles.css") : std::string();
    }

private:
    std::vector<std::string> Banks(const std::string& kind) {
        std::vector<std::pair<long, std::string>> found;
        const std::string start = prefix + kind + "_";
        for (const ZipArchive::Entry& entry : zip.Entries()) {
            const std::string& name = entry.name;
            if (name.size() > start.size() + 5 && name.starts_with(start) && name.ends_with(".json")) {
                const std::string_view digits(name.data() + start.size(), name.size() - start.size() - 5);
                long number = 0;
                const auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), number);
                if (ec == std::errc{} && ptr == digits.data() + digits.size() && number >= 0) {
                    found.emplace_back(number, name);
                }
            }
        }
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        std::vector<std::string> names;
        for (auto& [number, name] : found) {
            names.push_back(std::move(name));
        }
        return names;
    }

    ZipArchive zip;
    std::string index;
    std::string prefix;
};

} // namespace

Written WriteDictionary(const std::string& zip_path, const std::string& out_folder, const ImportOptions& options,
                        const std::function<bool(const std::string&)>& title_taken) {
    Written result;
    std::unique_ptr<ZipSource> source;
    try {
        source = std::make_unique<ZipSource>(zip_path, result.not_dictionary);
    } catch (const Failure& failure) {
        result.error = failure.message;
        return result;
    }
    return WriteDictionaryFrom(*source, out_folder, options, title_taken);
}

Written WriteDictionaryFrom(Source& source, const std::string& out_folder, const ImportOptions& options,
                            const std::function<bool(const std::string&)>& title_taken) {
    Written result;
    try {
        Json::Value index;
        std::string index_text;
        try {
            index_text = source.Index();
        } catch (const std::runtime_error& error) {
            throw Failure{error.what()};
        }
        if (!Json::Parse(index_text, index) || index.type != Json::Value::Type::Object) {
            throw Failure{"Couldn't read the dictionary's index.json. Download the dictionary again"};
        }
        std::string title = index.String("title");
        const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
        while (!title.empty() && space(title.back())) {
            title.pop_back();
        }
        while (!title.empty() && space(title.front())) {
            title.erase(title.begin());
        }
        if (title.empty()) {
            throw Failure{"This dictionary has no title and can't be imported"};
        }
        result.title = title;
        const int format = static_cast<int>(index.Number("format", index.Number("version", 3)));
        if (format != 1 && format != 3) {
            throw Failure{"This dictionary's format (" + std::to_string(format) +
                          ") isn't supported. Choose a Yomitan dictionary"};
        }
        if (title_taken && title_taken(title)) {
            result.already_there = true;
            throw Failure{"\"" + title + "\" is already installed"};
        }
        const std::vector<std::string> term_banks = source.TermBanks();
        const std::vector<std::string> meta_banks = source.MetaBanks();
        const std::vector<std::string> kanji_banks = source.KanjiBanks();
        const std::vector<std::string> kanji_meta_banks = source.KanjiMetaBanks();
        if (term_banks.empty() && meta_banks.empty() && kanji_banks.empty() && kanji_meta_banks.empty()) {
            throw Failure{"\"" + title + "\" has no entries in it"};
        }
        std::string styles;
        try {
            styles = source.Styles();
        } catch (const std::runtime_error& error) {
            throw Failure{error.what()};
        }
        std::unordered_map<std::string, std::uint64_t> sizes;
        const std::vector<std::string> media = source.Media();
        for (const auto* list : {&term_banks, &meta_banks, &kanji_banks, &kanji_meta_banks, &media}) {
            for (const std::string& name : *list) {
                sizes.emplace(name, source.Size(name));
            }
        }
        std::uint64_t term_bytes = 0;
        for (const std::string& name : term_banks) {
            term_bytes += sizes[name];
        }

        const fs::path out = fs::path(std::u8string(out_folder.begin(), out_folder.end()));
        fs::create_directories(out);
        const auto keep_going = [&options] {
            if (options.keep_going && !options.keep_going()) {
                throw Failure{"Import cancelled"};
            }
        };

        const std::string trained = Train(source, term_banks, format, term_bytes, keep_going);
        ZSTD_CDict* cdict = trained.empty() ? nullptr : ZSTD_createCDict(trained.data(), trained.size(), Level());
        struct FreeDict {
            ZSTD_CDict* dict;
            ~FreeDict() {
                ZSTD_freeCDict(dict);
            }
        } free_dict{cdict};
        if (cdict != nullptr) {
            WriteText(out / "zdict.bin", trained);
        }

        // The work, in the order it is written.
        struct Job {
            enum class Kind { Terms, Metas, Kanji, KanjiMetas, Media } kind;
            std::string name;
            std::vector<std::string> media;
            std::uint64_t bytes;
        };
        std::vector<Job> jobs;
        for (const std::string& name : term_banks) {
            jobs.push_back({Job::Kind::Terms, name, {}, sizes[name]});
        }
        for (const std::string& name : meta_banks) {
            jobs.push_back({Job::Kind::Metas, name, {}, sizes[name]});
        }
        for (const std::string& name : kanji_banks) {
            jobs.push_back({Job::Kind::Kanji, name, {}, sizes[name]});
        }
        for (const std::string& name : kanji_meta_banks) {
            jobs.push_back({Job::Kind::KanjiMetas, name, {}, sizes[name]});
        }
        {
            std::vector<std::string> run;
            std::uint64_t run_bytes = 0;
            for (const std::string& name : media) {
                run.push_back(name);
                run_bytes += sizes[name];
                if (run_bytes >= 8ull * 1024 * 1024 || run.size() >= 2000) {
                    jobs.push_back({Job::Kind::Media, {}, std::move(run), run_bytes});
                    run.clear();
                    run_bytes = 0;
                }
            }
            if (!run.empty()) {
                jobs.push_back({Job::Kind::Media, {}, std::move(run), run_bytes});
            }
        }
        const int total =
            static_cast<int>(term_banks.size() + meta_banks.size() + kanji_banks.size() + kanji_meta_banks.size());
        int done = 0;
        if (options.progress) {
            options.progress(done, total);
        }

        const int machine = static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
        const int threads = std::max(1, options.threads > 0 ? options.threads : std::min(machine - 1, 15));
        std::deque<std::pair<std::future<std::vector<Prepared>>, std::uint64_t>> ahead;
        std::uint64_t ahead_bytes = 0;
        std::size_t next = 0;
        const auto fill = [&] {
            while (next < jobs.size() && static_cast<int>(ahead.size()) < threads + 1 &&
                   (ahead.empty() ||
                    ahead_bytes + jobs[next].bytes <= static_cast<std::uint64_t>(options.ahead_bytes))) {
                const Job& job = jobs[next++];
                ahead_bytes += job.bytes;
                switch (job.kind) {
                case Job::Kind::Terms:
                    ahead.emplace_back(std::async(std::launch::async, ReadTermBank, std::ref(source), job.name, format,
                                                  cdict),
                                       job.bytes);
                    break;
                case Job::Kind::Metas:
                    ahead.emplace_back(std::async(std::launch::async, ReadMetaBank, std::ref(source), job.name),
                                       job.bytes);
                    break;
                case Job::Kind::Kanji:
                    ahead.emplace_back(std::async(std::launch::async, ReadKanjiBank, std::ref(source), job.name, format),
                                       job.bytes);
                    break;
                case Job::Kind::KanjiMetas:
                    ahead.emplace_back(std::async(std::launch::async, ReadKanjiMetaBank, std::ref(source), job.name),
                                       job.bytes);
                    break;
                case Job::Kind::Media:
                    ahead.emplace_back(std::async(std::launch::async, ReadMedia, std::ref(source), job.media),
                                       job.bytes);
                    break;
                }
            }
        };
        // Whatever happens, no thread is left reading the archive after it is gone.
        struct Drain {
            std::deque<std::pair<std::future<std::vector<Prepared>>, std::uint64_t>>& ahead;
            ~Drain() {
                for (auto& [future, bytes] : ahead) {
                    if (future.valid()) {
                        future.wait();
                    }
                }
            }
        } drain{ahead};

        StringFile strings;
        std::vector<EntryRecord> entries;
        std::vector<DefinitionRecord> definitions;
        std::unordered_map<Key128, std::uint32_t, Key128Hash> known;
        std::vector<KeyRecord> keys;
        std::vector<MetaRecord> metas;
        std::vector<KeyRecord> meta_keys;
        std::vector<KanjiRecord> kanji;
        std::vector<KeyRecord> kanji_keys;
        std::vector<MetaRecord> kanji_metas;
        std::vector<KeyRecord> kanji_meta_keys;
        std::vector<std::pair<std::string, MediaRecord>> pictures;
        Output defs(out / "defs.bin");
        std::unique_ptr<Output> pack;

        fill();
        while (!ahead.empty()) {
            keep_going();
            std::vector<Prepared> parts = ahead.front().first.get();
            ahead_bytes -= ahead.front().second;
            ahead.pop_front();
            fill();
            for (Prepared& bank : parts) {
                std::vector<std::uint32_t> placed(bank.definitions.size());
                for (std::size_t i = 0; i < bank.definitions.size(); ++i) {
                    Definition& definition = bank.definitions[i];
                    const Key128 key{definition.hash_a, definition.hash_b};
                    const auto found = known.find(key);
                    if (found != known.end()) {
                        placed[i] = found->second;
                        continue;
                    }
                    const auto at = static_cast<std::uint32_t>(definitions.size());
                    definitions.push_back(DefinitionRecord{defs.Written(), static_cast<std::uint32_t>(definition.bytes.size()),
                                                           definition.size | (definition.compressed ? Compressed : 0u)});
                    defs.Write(definition.bytes.data(), definition.bytes.size());
                    known.emplace(key, at);
                    placed[i] = at;
                }
                const std::uint32_t base = strings.Append(bank.strings.data);
                const auto moved = [base](std::uint32_t offset) { return offset == 0 ? 0 : offset + base; };
                const auto first_entry = static_cast<std::uint32_t>(entries.size());
                for (EntryRecord record : bank.terms) {
                    record.expression = moved(record.expression);
                    record.reading = moved(record.reading);
                    record.definition_tags = moved(record.definition_tags);
                    record.rules = moved(record.rules);
                    record.term_tags = moved(record.term_tags);
                    record.definition = placed[record.definition];
                    entries.push_back(record);
                }
                for (KeyRecord key : bank.keys) {
                    key.item += first_entry;
                    keys.push_back(key);
                }
                const auto first_meta = static_cast<std::uint32_t>(metas.size());
                for (MetaRecord record : bank.metas) {
                    record.expression = moved(record.expression);
                    record.mode = moved(record.mode);
                    record.data = moved(record.data);
                    metas.push_back(record);
                }
                for (KeyRecord key : bank.meta_keys) {
                    key.item += first_meta;
                    meta_keys.push_back(key);
                }
                const auto first_kanji = static_cast<std::uint32_t>(kanji.size());
                for (KanjiRecord record : bank.kanji) {
                    record.character = moved(record.character);
                    record.onyomi = moved(record.onyomi);
                    record.kunyomi = moved(record.kunyomi);
                    record.tags = moved(record.tags);
                    record.meanings = moved(record.meanings);
                    record.stats = moved(record.stats);
                    kanji.push_back(record);
                }
                for (KeyRecord key : bank.kanji_keys) {
                    key.item += first_kanji;
                    kanji_keys.push_back(key);
                }
                const auto first_kanji_meta = static_cast<std::uint32_t>(kanji_metas.size());
                for (MetaRecord record : bank.kanji_metas) {
                    record.expression = moved(record.expression);
                    record.mode = moved(record.mode);
                    record.data = moved(record.data);
                    kanji_metas.push_back(record);
                }
                for (KeyRecord key : bank.kanji_meta_keys) {
                    key.item += first_kanji_meta;
                    kanji_meta_keys.push_back(key);
                }
                for (auto& [name, bytes] : bank.media) {
                    if (!pack) {
                        pack = std::make_unique<Output>(out / "media.pack");
                    }
                    MediaRecord record{};
                    record.offset = pack->Written();
                    record.size = static_cast<std::uint32_t>(bytes.size());
                    pack->Write(bytes.data(), bytes.size());
                    pictures.emplace_back(source.MediaPath(name), record);
                }
            }
            if (!parts.empty() && parts.front().media.empty() && options.progress) {
                options.progress(++done, total);
            }
        }
        defs.Close();
        if (pack) {
            pack->Close();
        }

        const auto by_key = [](const KeyRecord& a, const KeyRecord& b) {
            return a.hash != b.hash ? a.hash < b.hash : a.item < b.item;
        };
        std::sort(keys.begin(), keys.end(), by_key);
        std::sort(meta_keys.begin(), meta_keys.end(), by_key);
        std::sort(kanji_keys.begin(), kanji_keys.end(), by_key);
        std::sort(kanji_meta_keys.begin(), kanji_meta_keys.end(), by_key);
        // A path given twice in the zip: the last one, as the database kept it.
        std::stable_sort(pictures.begin(), pictures.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<MediaRecord> media_records;
        for (std::size_t i = 0; i < pictures.size(); ++i) {
            if (i + 1 < pictures.size() && pictures[i + 1].first == pictures[i].first) {
                continue;
            }
            MediaRecord record = pictures[i].second;
            record.path = strings.Add(pictures[i].first);
            media_records.push_back(record);
        }

        WriteTable(out / "entries.bin", EntriesMagic, entries);
        WriteTable(out / "defs.idx", DefinitionsMagic, definitions);
        WriteTable(out / "keys.bin", KeysMagic, keys);
        WriteTable(out / "metas.bin", MetasMagic, metas);
        WriteTable(out / "meta_keys.bin", KeysMagic, meta_keys);
        if (!kanji.empty() || !kanji_metas.empty()) {
            WriteTable(out / "kanji.bin", KanjiMagic, kanji);
            WriteTable(out / "kanji_keys.bin", KeysMagic, kanji_keys);
            WriteTable(out / "kanji_metas.bin", MetasMagic, kanji_metas);
            WriteTable(out / "kanji_meta_keys.bin", KeysMagic, kanji_meta_keys);
        }
        WriteTable(out / "media.idx", MediaMagic, media_records);
        WriteText(out / "strings.bin", strings.Data());

        // Furoshiki's names dictionaries say which games they are for (docs/titles.md).
        std::vector<std::string> games;
        if (const Json::Value* scope = index.Get("furoshikiScope"); scope != nullptr) {
            for (const Json::Value& game : scope->array) {
                if (game.type == Json::Value::Type::String) {
                    games.push_back(game.string);
                }
            }
        }
        result.revision = index.String("revision");
        result.author = index.String("author");
        result.description = index.String("description");
        result.frequency_mode = index.String("frequencyMode");
        result.scope = ScopeText(games);
        result.styles = styles;
        result.terms = static_cast<std::int64_t>(entries.size());
        result.metas = static_cast<std::int64_t>(metas.size() + kanji_metas.size());
        result.kanji = static_cast<std::int64_t>(kanji.size());
        result.media = static_cast<std::int64_t>(media_records.size());
        result.ok = true;
    } catch (const Failure& failure) {
        result.error = failure.message;
    } catch (const std::filesystem::filesystem_error& error) {
        result.error = std::string("Couldn't save dictionary files: ") + error.what();
    } catch (const std::bad_alloc&) {
        result.error = "Not enough memory to import this dictionary";
    } catch (const std::exception& error) {
        result.error = std::string("Couldn't import dictionary: ") + error.what();
    }
    return result;
}

std::string InfoJson(const Written& written, std::int64_t id) {
    std::string out = "{\n";
    const auto put = [&out](const char* key, const std::string& value, bool last = false) {
        out += "  ";
        out += Json::Quote(key);
        out += ": ";
        out += value;
        out += last ? "\n" : ",\n";
    };
    put("format", std::to_string(Version));
    put("id", std::to_string(id));
    put("title", Json::Quote(written.title));
    put("revision", Json::Quote(written.revision));
    put("author", Json::Quote(written.author));
    put("description", Json::Quote(written.description));
    put("frequencyMode", Json::Quote(written.frequency_mode));
    put("scope", Json::Quote(written.scope));
    put("terms", std::to_string(written.terms));
    put("metas", std::to_string(written.metas));
    put("media", std::to_string(written.media));
    put("kanji", std::to_string(written.kanji));
    put("styles", Json::Quote(written.styles), true);
    out += "}\n";
    return out;
}

} // namespace Dict::Detail
