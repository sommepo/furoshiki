// SPDX-FileCopyrightText: Flycast Reader fork contributors (from the ARMSX2 Reader fork)
// SPDX-License-Identifier: GPL-2.0-or-later

// The reader's native calls from the Android app that are the same in every reader fork
// (one file for every fork, in shared/; each fork's package in the functions' names):
// the Save sync page (reader/SaveSync.kt), the routes (reader/Routes.kt) and going on to the
// next choice (reader/NextChoice.kt). The rest of the bridge is the fork's reader_jni.cpp.

#include <jni.h>
#include <cstdint>
#include <string>
#include "reader/core/emulator.h"
#include "reader/core/engine.h"
#ifndef READER_NO_SAVE_SYNC
#include "reader/save_sync/host.h"
#endif

// The JNI functions' names hold the app's Java package, which each fork's build gives as
// READER_JNI_PACKAGE (its package as JNI writes it, com_flycast_emulator: shared/reader.cmake's
// reader_jni_package).
#ifndef READER_JNI_PACKAGE
#error "READER_JNI_PACKAGE is not defined: the fork's CMake calls reader_jni_package()"
#endif
#define READER_JNI_JOIN_(package, name) Java_##package##_reader_##name
#define READER_JNI_JOIN(package, name) READER_JNI_JOIN_(package, name)
#define READER_JNI(name) READER_JNI_JOIN(READER_JNI_PACKAGE, name)

namespace {

std::string BytesOf(JNIEnv *env, jbyteArray bytes) {
	if (!bytes) {
		return {};
	}
	const jsize size = env->GetArrayLength(bytes);
	std::string out(static_cast<std::size_t>(size), '\0');
	env->GetByteArrayRegion(bytes, 0, size, reinterpret_cast<jbyte *>(out.data()));
	return out;
}

/// A JSON string of `text`: quotes and backslashes escaped, other control characters spaces.
std::string JsonQuoted(const std::string &text) {
	std::string out = "\"";
	for (const char c : text) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		default:
			out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
		}
	}
	return out + "\"";
}

} // namespace

/// Called by the fork's reader_jni.cpp where its native code is a static library: a linker takes
/// this file's functions into the app only when something in it is asked for.
void ReaderJniCommonLinked() {}

extern "C" {

// ---- Going on to the next choice (reader/core/next_choice.h), where the emulator has it ----

JNIEXPORT void JNICALL READER_JNI(ReaderNative_toggleNextChoice)(JNIEnv *, jobject) {
	Reader::Emu::ToggleNextChoice();
}

JNIEXPORT jboolean JNICALL READER_JNI(ReaderNative_goingToNextChoice)(JNIEnv *, jobject) {
	return Reader::Emu::GoingToNextChoice() ? JNI_TRUE : JNI_FALSE;
}

// ---- Progress set by hand (reader.progress) ----

// What the game's script offers to mark by hand, as JSON: {"title","note",
// "items":[{"id","label","group","done"}]}, or null when it offers nothing.
JNIEXPORT jstring JNICALL READER_JNI(ReaderNative_progress)(JNIEnv *env, jobject) {
	const auto list = Reader::Engine::Instance().Progress();
	if (!list || list->items.empty()) {
		return nullptr;
	}
	std::string json = "{\"title\":" + JsonQuoted(list->title) + ",\"note\":" + JsonQuoted(list->note) + ",\"items\":[";
	for (std::size_t i = 0; i < list->items.size(); ++i) {
		const auto &item = list->items[i];
		json += (i ? "," : "") + std::string("{\"id\":") + JsonQuoted(item.id) + ",\"label\":" + JsonQuoted(item.label) +
				",\"group\":" + JsonQuoted(item.group) + ",\"done\":" + (item.done ? "true" : "false") + "}";
	}
	json += "]}";
	return env->NewStringUTF(json.c_str());
}

// Marks one; null when done, else what went wrong.
JNIEXPORT jstring JNICALL READER_JNI(ReaderNative_markProgress)(JNIEnv *env, jobject, jstring id, jboolean done) {
	const char *chars = env->GetStringUTFChars(id, nullptr);
	const std::string name = chars ? chars : "";
	if (chars) {
		env->ReleaseStringUTFChars(id, chars);
	}
	std::string error;
	if (Reader::Engine::Instance().MarkProgress(name, done != JNI_FALSE, error)) {
		return nullptr;
	}
	return env->NewStringUTF(error.c_str());
}

// ---- What the player may do in the game (reader.action) ----

// The labels, one per line (UTF-8); null when there are none.
JNIEXPORT jbyteArray JNICALL READER_JNI(ReaderNative_profileActions)(JNIEnv *env, jobject) {
	std::string labels;
	for (const std::string &label : Reader::Engine::Instance().Actions()) {
		labels += label;
		labels += '\n';
	}
	if (labels.empty()) {
		return nullptr;
	}
	jbyteArray out = env->NewByteArray(static_cast<jsize>(labels.size()));
	env->SetByteArrayRegion(out, 0, static_cast<jsize>(labels.size()), reinterpret_cast<const jbyte *>(labels.data()));
	return out;
}

// Runs one; what the profile says about it (UTF-8), or null.
JNIEXPORT jbyteArray JNICALL READER_JNI(ReaderNative_runProfileAction)(JNIEnv *env, jobject, jint index) {
	if (index < 0) {
		return nullptr;
	}
	const auto said = Reader::Engine::Instance().RunAction(static_cast<std::size_t>(index));
	if (!said) {
		return nullptr;
	}
	jbyteArray out = env->NewByteArray(static_cast<jsize>(said->size()));
	env->SetByteArrayRegion(out, 0, static_cast<jsize>(said->size()), reinterpret_cast<const jbyte *>(said->data()));
	return out;
}

// ---- Save sync (reader/save_sync, the page in reader/SaveSync.kt) ----
// Text goes both ways as UTF-8 bytes; what the page shows comes as one JSON object.

JNIEXPORT void JNICALL READER_JNI(ReaderNative_saveSyncConfigure)(JNIEnv *env, jobject,
		jboolean enabled, jbyteArray addresses, jbyteArray code, jbyteArray device) {
#ifndef READER_NO_SAVE_SYNC
	SaveSync::Host::Settings settings;
	settings.enabled = enabled != JNI_FALSE;
	settings.addresses = BytesOf(env, addresses);
	settings.code = BytesOf(env, code);
	settings.device = BytesOf(env, device);
	Reader::Emu::ConfigureSaveSync(settings);
#endif
}

JNIEXPORT jbyteArray JNICALL READER_JNI(ReaderNative_saveSyncState)(JNIEnv *env, jobject) {
	std::string json = "{";
#ifndef READER_NO_SAVE_SYNC
	if (SaveSync::Host::Started()) {
		const SaveSync::Host::Settings settings = SaveSync::Host::Get();
		json += "\"on\":" + std::string(settings.enabled ? "true" : "false");
		json += ",\"generation\":" + std::to_string(SaveSync::Host::Generation());
		json += ",\"status\":" + JsonQuoted(SaveSync::Host::Status());
		json += ",\"running\":" + JsonQuoted(SaveSync::Host::RunningDisc());
		json += ",\"conflicts\":[";
		bool first = true;
		for (const SaveSync::Host::Conflict &conflict : SaveSync::Host::Conflicts()) {
			json += first ? "" : ",";
			first = false;
			json += "{\"title\":" + JsonQuoted(conflict.title) + ",\"disc\":" + JsonQuoted(conflict.disc) +
				",\"name\":" + JsonQuoted(conflict.name) + ",\"message\":" + JsonQuoted(conflict.message) + "}";
		}
		json += "]";
	}
#endif
	json += "}";
	jbyteArray out = env->NewByteArray(static_cast<jsize>(json.size()));
	env->SetByteArrayRegion(out, 0, static_cast<jsize>(json.size()), reinterpret_cast<const jbyte *>(json.data()));
	return out;
}

JNIEXPORT void JNICALL READER_JNI(ReaderNative_saveSyncCheck)(JNIEnv *, jobject) {
#ifndef READER_NO_SAVE_SYNC
	if (SaveSync::Host::Started()) {
		SaveSync::Host::Check();
		SaveSync::Host::SyncAll();
	}
#endif
}

JNIEXPORT void JNICALL READER_JNI(ReaderNative_saveSyncResolve)(JNIEnv *env, jobject,
		jbyteArray title, jboolean keepMine) {
#ifndef READER_NO_SAVE_SYNC
	if (SaveSync::Host::Started()) {
		SaveSync::Host::Resolve(BytesOf(env, title), keepMine != JNI_FALSE);
	}
#endif
}

} // extern "C"
