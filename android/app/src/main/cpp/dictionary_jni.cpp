// SPDX-FileCopyrightText: Furoshiki contributors
// SPDX-License-Identifier: GPL-2.0-or-later

// The dictionary engine (shared/dict, docs/dictionary-engine.md) for the Kotlin layer: the
// shared Kotlin's dict/DictionaryNative.kt, written into this app's package at build time. One
// Dict::Library per handle; each process of the app opens its own on the same folder.
//
// Lists go to Kotlin as one string: records apart by U+001E, fields by U+001F (neither is in a
// dictionary's text). Strings cross as UTF-16, as JNI's "UTF-8" is not UTF-8 for characters past
// U+FFFF (𠮟, emoji).

#include <jni.h>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "dict/library.h"
#include "dict/sqlite_source.h"

namespace {

constexpr char Field = '\x1f';
constexpr char Record = '\x1e';

std::string FromJava(JNIEnv* env, jstring text) {
    std::string out;
    if (text == nullptr) {
        return out;
    }
    const jsize length = env->GetStringLength(text);
    const jchar* chars = env->GetStringChars(text, nullptr);
    if (chars == nullptr) {
        return out;
    }
    struct Release {
        JNIEnv* env;
        jstring text;
        const jchar* chars;
        ~Release() {
            env->ReleaseStringChars(text, chars);
        }
    } release{env, text, chars};
    out.reserve(static_cast<std::size_t>(length) * 3);
    for (jsize i = 0; i < length; ++i) {
        std::uint32_t c = chars[i];
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 < length && chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (chars[i + 1] - 0xDC00);
                ++i;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

jstring ToJava(JNIEnv* env, const std::string& text) {
    std::u16string out;
    out.reserve(text.size());
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t size = text.size();
    for (std::size_t i = 0; i < size;) {
        std::uint32_t c = bytes[i];
        int more = 0;
        if (c < 0x80) {
            more = 0;
        } else if ((c & 0xE0) == 0xC0) {
            c &= 0x1F;
            more = 1;
        } else if ((c & 0xF0) == 0xE0) {
            c &= 0x0F;
            more = 2;
        } else if ((c & 0xF8) == 0xF0) {
            c &= 0x07;
            more = 3;
        } else {
            c = 0xFFFD; // not UTF-8
        }
        ++i;
        for (int k = 0; k < more; ++k, ++i) {
            if (i >= size || (bytes[i] & 0xC0) != 0x80) {
                c = 0xFFFD;
                break;
            }
            c = (c << 6) | (bytes[i] & 0x3F);
        }
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            c = 0xFFFD;
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (c >> 10));
            out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
        } else {
            out += static_cast<char16_t>(c);
        }
    }
    return env->NewString(reinterpret_cast<const jchar*>(out.data()), static_cast<jsize>(out.size()));
}

jbyteArray ToByteArray(JNIEnv* env, const std::string& data) {
    if (data.size() > static_cast<std::size_t>(std::numeric_limits<jsize>::max())) {
        throw std::runtime_error("Dictionary data exceeds JNI byte array limit");
    }
    const auto len = static_cast<jsize>(data.size());
    jbyteArray bytes = env->NewByteArray(len);
    if (bytes == nullptr) {
        return nullptr;
    }
    if (len > 0) {
        env->SetByteArrayRegion(bytes, 0, len, reinterpret_cast<const jbyte*>(data.data()));
    }
    return bytes;
}

std::vector<std::string> Strings(JNIEnv* env, jobjectArray array) {
    std::vector<std::string> out;
    const jsize count = array == nullptr ? 0 : env->GetArrayLength(array);
    out.reserve(static_cast<std::size_t>(count));
    for (jsize i = 0; i < count; ++i) {
        auto item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        out.push_back(FromJava(env, item));
        env->DeleteLocalRef(item);
    }
    return out;
}

std::vector<std::int64_t> Ids(JNIEnv* env, jlongArray array) {
    std::vector<std::int64_t> out;
    const jsize count = array == nullptr ? 0 : env->GetArrayLength(array);
    out.resize(static_cast<std::size_t>(count));
    if (count > 0) {
        env->GetLongArrayRegion(array, 0, count, reinterpret_cast<jlong*>(out.data()));
    }
    return out;
}

Dict::Library* Of(jlong handle) {
    if (handle == 0) {
        throw std::runtime_error("Dictionary library isn't open");
    }
    return reinterpret_cast<Dict::Library*>(handle);
}

void Add(std::string& out, const std::string& field) {
    for (const char c : field) {
        out += (c == Field || c == Record) ? ' ' : c;
    }
    out += Field;
}

void Add(std::string& out, std::int64_t number) {
    out += std::to_string(number);
    out += Field;
}

void EndRecord(std::string& out) {
    if (!out.empty() && out.back() == Field) {
        out.back() = Record;
    } else {
        out += Record;
    }
}

void ThrowRuntime(JNIEnv* env, const char* message) {
    if (env->ExceptionCheck()) {
        return;
    }
    jclass cls = env->FindClass("java/lang/RuntimeException");
    if (cls != nullptr) {
        env->ThrowNew(cls, message);
        env->DeleteLocalRef(cls);
    }
}

/// Kotlin's exceptions are its own; the engine's become a RuntimeException.
template <typename Work>
auto Guarded(JNIEnv* env, Work&& work) -> decltype(work()) {
    try {
        return work();
    } catch (const std::exception& error) {
        ThrowRuntime(env, error.what());
    } catch (...) {
        ThrowRuntime(env, "Dictionary engine error");
    }
    return decltype(work()){};
}

/// Holds a global JNI ref for import callbacks across worker threads.
struct ProgressBridge {
    JNIEnv* env = nullptr;
    JavaVM* vm = nullptr;
    jobject target = nullptr;
    jmethodID step = nullptr;
    jmethodID go_on = nullptr;

    ProgressBridge(JNIEnv* env_, jobject progress) : env{env_} {
        if (progress == nullptr) {
            return;
        }
        env->GetJavaVM(&vm);
        jclass type = env->GetObjectClass(progress);
        if (type != nullptr) {
            step = env->GetMethodID(type, "step", "(II)V");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                step = nullptr;
            }
            go_on = env->GetMethodID(type, "keepGoing", "()Z");
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                go_on = nullptr;
            }
            env->DeleteLocalRef(type);
        }
        if (vm != nullptr && (step != nullptr || go_on != nullptr)) {
            target = env->NewGlobalRef(progress);
        }
    }

    ~ProgressBridge() {
        if (target != nullptr) {
            env->DeleteGlobalRef(target);
        }
    }

    ProgressBridge(const ProgressBridge&) = delete;
    ProgressBridge& operator=(const ProgressBridge&) = delete;

    void Attach(Dict::ImportOptions& options) const {
        if (target == nullptr || vm == nullptr) {
            return;
        }
        const auto call = [vm = vm](const auto& with) {
            JNIEnv* now = nullptr;
            bool attached = false;
            if (vm->GetEnv(reinterpret_cast<void**>(&now), JNI_VERSION_1_6) != JNI_OK) {
                if (vm->AttachCurrentThread(&now, nullptr) != JNI_OK) {
                    return true;
                }
                attached = true;
            }
            const bool answer = with(now);
            if (now->ExceptionCheck()) {
                now->ExceptionClear();
            }
            if (attached) {
                vm->DetachCurrentThread();
            }
            return answer;
        };
        if (step != nullptr) {
            options.progress = [call, target = target, step = step](int done, int total) {
                call([&](JNIEnv* now) {
                    now->CallVoidMethod(target, step, done, total);
                    return true;
                });
            };
        }
        if (go_on != nullptr) {
            options.keep_going = [call, target = target, go_on = go_on]() {
                return call([&](JNIEnv* now) { return now->CallBooleanMethod(target, go_on) == JNI_TRUE; });
            };
        }
    }
};

} // namespace

#define DICT(name) Java_io_github_sommepo_furoshiki_reader_dict_DictionaryNative_##name

extern "C" {

JNIEXPORT jboolean JNICALL DICT(probe)(JNIEnv*, jclass) {
    return JNI_TRUE;
}

JNIEXPORT jlong JNICALL DICT(open)(JNIEnv* env, jclass, jstring folder) {
    return Guarded(env, [&]() -> jlong {
        return reinterpret_cast<jlong>(new Dict::Library(FromJava(env, folder)));
    });
}

JNIEXPORT void JNICALL DICT(close)(JNIEnv*, jclass, jlong handle) {
    if (handle != 0) {
        delete reinterpret_cast<Dict::Library*>(handle);
    }
}

/// id, title, revision, author, description, frequency mode, priority, enabled, terms, metas,
/// scope ("|ID|ID|"), kanji.
JNIEXPORT jstring JNICALL DICT(dictionaries)(JNIEnv* env, jclass, jlong handle) {
    return Guarded(env, [&]() -> jstring {
        std::string out;
        for (const Dict::DictionaryInfo& info : Of(handle)->Dictionaries()) {
            Add(out, info.id);
            Add(out, info.title);
            Add(out, info.revision);
            Add(out, info.author);
            Add(out, info.description);
            Add(out, info.frequency_mode);
            Add(out, info.priority);
            Add(out, info.enabled ? 1 : 0);
            Add(out, info.terms);
            Add(out, info.metas);
            Add(out, info.scope);
            Add(out, info.kanji);
            EndRecord(out);
        }
        return ToJava(env, out);
    });
}

JNIEXPORT jboolean JNICALL DICT(hasEnabled)(JNIEnv* env, jclass, jlong handle) {
    return Guarded(env, [&]() -> jboolean { return Of(handle)->HasEnabled() ? JNI_TRUE : JNI_FALSE; });
}

JNIEXPORT void JNICALL DICT(setEnabled)(JNIEnv* env, jclass, jlong handle, jlongArray ids, jboolean enabled) {
    Guarded(env, [&]() -> int {
        Of(handle)->SetEnabled(Ids(env, ids), enabled == JNI_TRUE);
        return 0;
    });
}

JNIEXPORT void JNICALL DICT(setOrder)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    Guarded(env, [&]() -> int {
        Of(handle)->SetOrder(Ids(env, ids));
        return 0;
    });
}

JNIEXPORT void JNICALL DICT(delete)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    Guarded(env, [&]() -> int {
        Of(handle)->Delete(Ids(env, ids));
        return 0;
    });
}

JNIEXPORT void JNICALL DICT(compact)(JNIEnv* env, jclass, jlong handle) {
    Guarded(env, [&]() -> int {
        Of(handle)->Compact();
        return 0;
    });
}

JNIEXPORT jint JNICALL DICT(uncovered)(JNIEnv* env, jclass, jlong handle) {
    return Guarded(env, [&]() -> jint { return Of(handle)->Uncovered(); });
}

/// dictionary id, dictionary, priority, entry, expression, reading, definition tags, rules,
/// score, sequence, term tags, scoped.
JNIEXPORT jstring JNICALL DICT(findTerms)(JNIEnv* env, jclass, jlong handle, jobjectArray keys, jstring game) {
    return Guarded(env, [&]() -> jstring {
        std::string out;
        for (const Dict::Term& term : Of(handle)->FindTerms(Strings(env, keys), FromJava(env, game))) {
            Add(out, term.dictionary_id);
            Add(out, term.dictionary);
            Add(out, term.priority);
            Add(out, static_cast<std::int64_t>(term.entry));
            Add(out, term.expression);
            Add(out, term.reading);
            Add(out, term.definition_tags);
            Add(out, term.rules);
            Add(out, term.score);
            Add(out, term.sequence);
            Add(out, term.term_tags);
            Add(out, term.scoped ? 1 : 0);
            EndRecord(out);
        }
        return ToJava(env, out);
    });
}

JNIEXPORT jstring JNICALL DICT(definition)(JNIEnv* env, jclass, jlong handle, jlong dictionary, jint entry) {
    return Guarded(env, [&]() -> jstring {
        return ToJava(env, Of(handle)->Definition(dictionary, static_cast<std::uint32_t>(entry)));
    });
}

/**
 * Many definitions at once, as UTF-8 bytes apart by U+001E: a lookup keeps about a hundred, and
 * one call each, every one's text made a Java string character by character, took 24 ms of a
 * hover with 52 dictionaries on the Y700. `ids` are (dictionary << 32 | entry).
 */
JNIEXPORT jbyteArray JNICALL DICT(definitions)(JNIEnv* env, jclass, jlong handle, jlongArray ids) {
    return Guarded(env, [&]() -> jbyteArray {
        std::string out;
        const std::vector<std::int64_t> list = Ids(env, ids);
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (i > 0) {
                out += Record;
            }
            const auto packed = static_cast<std::uint64_t>(list[i]);
            out += Of(handle)->Definition(static_cast<std::int64_t>(packed >> 32),
                                          static_cast<std::uint32_t>(packed & 0xffffffffu));
        }
        return ToByteArray(env, out);
    });
}

/// dictionary, frequency mode, expression, mode, data.
JNIEXPORT jstring JNICALL DICT(findMeta)(JNIEnv* env, jclass, jlong handle, jobjectArray expressions) {
    return Guarded(env, [&]() -> jstring {
        std::string out;
        for (const Dict::Meta& meta : Of(handle)->FindMeta(Strings(env, expressions))) {
            Add(out, meta.dictionary);
            Add(out, meta.frequency_mode);
            Add(out, meta.expression);
            Add(out, meta.mode);
            Add(out, meta.data);
            EndRecord(out);
        }
        return ToJava(env, out);
    });
}

/// dictionary id, dictionary, onyomi, kunyomi, tags, meanings (JSON), stats (JSON).
JNIEXPORT jstring JNICALL DICT(findKanji)(JNIEnv* env, jclass, jlong handle, jstring character) {
    return Guarded(env, [&]() -> jstring {
        std::string out;
        for (const Dict::Kanji& kanji : Of(handle)->FindKanji(FromJava(env, character))) {
            Add(out, kanji.dictionary_id);
            Add(out, kanji.dictionary);
            Add(out, kanji.onyomi);
            Add(out, kanji.kunyomi);
            Add(out, kanji.tags);
            Add(out, kanji.meanings);
            Add(out, kanji.stats);
            EndRecord(out);
        }
        return ToJava(env, out);
    });
}

/// A character's frequencies (kanji_meta_bank): dictionary, frequency mode, character, mode, data.
JNIEXPORT jstring JNICALL DICT(findKanjiMeta)(JNIEnv* env, jclass, jlong handle, jstring character) {
    return Guarded(env, [&]() -> jstring {
        std::string out;
        for (const Dict::Meta& meta : Of(handle)->FindKanjiMeta(FromJava(env, character))) {
            Add(out, meta.dictionary);
            Add(out, meta.frequency_mode);
            Add(out, meta.expression);
            Add(out, meta.mode);
            Add(out, meta.data);
            EndRecord(out);
        }
        return ToJava(env, out);
    });
}

JNIEXPORT jbyteArray JNICALL DICT(media)(JNIEnv* env, jclass, jlong handle, jlong dictionary, jstring path) {
    return Guarded(env, [&]() -> jbyteArray {
        const std::optional<std::string> bytes = Of(handle)->Media(dictionary, FromJava(env, path));
        if (!bytes) {
            return nullptr;
        }
        return ToByteArray(env, *bytes);
    });
}

/// title, styles.css.
JNIEXPORT jstring JNICALL DICT(styles)(JNIEnv* env, jclass, jlong handle) {
    return Guarded(env, [&]() -> jstring {
        std::string out;
        for (const auto& [title, css] : Of(handle)->Styles()) {
            Add(out, title);
            Add(out, css);
            EndRecord(out);
        }
        return ToJava(env, out);
    });
}

/**
 * Moves the dictionary `id` of an earlier version's SQLite file at `path` into the library, read
 * straight from SQLite (Dict::SqliteSource). The result as importZip's.
 */
JNIEXPORT jstring JNICALL DICT(importSqlite)(JNIEnv* env, jclass, jlong handle, jstring path, jlong id, jobject progress,
                                             jint threads) {
    return Guarded(env, [&]() -> jstring {
        ProgressBridge bridge(env, progress);
        Dict::ImportOptions options;
        options.threads = threads;
        options.ahead_bytes = 64ll * 1024 * 1024;
        bridge.Attach(options);
        std::string out;
        try {
            Dict::SqliteSource source(FromJava(env, path), id);
            const Dict::ImportResult result = Of(handle)->ImportFrom(source, options);
            Add(out, result.ok ? 1 : 0);
            Add(out, result.already_there ? 1 : 0);
            Add(out, result.not_dictionary ? 1 : 0);
            Add(out, result.error);
            Add(out, result.title);
            Add(out, result.id);
            Add(out, result.terms);
            Add(out, result.metas);
            Add(out, result.media);
            Add(out, result.kanji);
        } catch (const std::exception& error) {
            out.clear();
            Add(out, 0);
            Add(out, 0);
            Add(out, 0);
            Add(out, error.what());
            for (int i = 0; i < 6; ++i) {
                Add(out, 0);
            }
        }
        EndRecord(out);
        return ToJava(env, out);
    });
}

/**
 * Imports the zip at `path`. `progress` (DictionaryNative.Progress) is told the banks done and
 * their total and asked whether to go on, from any thread. The result: ok, already there,
 * not a dictionary (each 0 or 1), error, title, id, terms, metas, pictures, kanji.
 */
JNIEXPORT jstring JNICALL DICT(importZip)(JNIEnv* env, jclass, jlong handle, jstring path, jobject progress, jint threads,
                                          jint ahead_mb) {
    return Guarded(env, [&]() -> jstring {
        ProgressBridge bridge(env, progress);
        Dict::ImportOptions options;
        options.threads = threads;
        if (ahead_mb > 0) {
            options.ahead_bytes = static_cast<std::int64_t>(ahead_mb) * 1024 * 1024;
        }
        bridge.Attach(options);
        const Dict::ImportResult result = Of(handle)->Import(FromJava(env, path), options);
        std::string out;
        Add(out, result.ok ? 1 : 0);
        Add(out, result.already_there ? 1 : 0);
        Add(out, result.not_dictionary ? 1 : 0);
        Add(out, result.error);
        Add(out, result.title);
        Add(out, result.id);
        Add(out, result.terms);
        Add(out, result.metas);
        Add(out, result.media);
        Add(out, result.kanji);
        EndRecord(out);
        return ToJava(env, out);
    });
}

} // extern "C"
