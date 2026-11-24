# The dictionary engine

Furoshiki's own dictionary storage (the plan and its reasons: `docs/dictionary-engine.md`).
Yomitan dictionaries are imported into files of its own, a folder each, and looked up through
one index of the whole library. Plain C++20 and zstd: no Qt, so the desktop, Android (through
JNI) and the PS Vita can build the same code.

| File | What |
| --- | --- |
| `library.h/.cpp` | The library: installed dictionaries, display order, enable switches, imports, and lookups |
| `importer.h/.cpp` | Parallel importer: reads zip or SQLite banks across worker threads and writes dictionary files in order |
| `format.h` | On-disk binary formats and key hashing |
| `zip_reader.h/.cpp` | Memory-mapped zip reader (stored, deflated, and ZIP64) |
| `sqlite_source.h/.cpp` | Direct reader for migrating dictionaries from legacy `reader_dict.db` SQLite databases |
| `json.h/.cpp` | Streaming bank row scanner and minimal JSON parser for the engine's metadata files |
| `mapped_file.h/.cpp` | Cross-platform memory-mapped files (reads whole files on PS Vita, where mmap is unavailable) |
| `dict.cmake` | Fetches zstd 1.5.7 (verified by SHA-256) and builds `reader_zstd` and `reader_dict` |

## Testing and verification

- `DictionaryDatabase` (`shared/reader/desktop/dictionary.*`) opens a library when given
  `engine:FOLDER`. Whenever `READER_DICT_ENGINE` is enabled, the full dictionary and popup test
  suite runs against both the engine and SQLite:
  `READER_DICT_BACKEND=engine tools/reader/test-desktop.sh "[dictionary],[popup]"`.
- `[.engine-compare]` (`shared/reader/desktop/tests/dict_engine.cpp`) checks a library against
  a SQLite database of the same dictionaries: every term, definition, frequency, pitch, and
  picture, plus identical lookups, field by field.

## Key differences from SQLite

- Each distinct definition is stored once and compressed with zstd using a dictionary trained on
  a sample of that dictionary's own definitions (skipped when total definition text is under 4 MB).
- Large banks are read in chunks across worker threads with bounded read-ahead (128 MB by default),
  while a single writer appends sequentially.
- Imports are staged in `.import-*` folders and atomically renamed into place when complete;
  abandoned `.import-*` folders older than 24 hours are cleaned up when the library opens.
- One shared index (`terms.idx`, `metas.idx`) covers the library. Individual imports and deletions
  do not rebuild it; unindexed dictionaries are queried directly until `Compact()`, which the app
  runs once a batch of imports finishes.

## Benchmark environment variables

- `FUROSHIKI_DICT_EACH=1`: query each dictionary directly instead of using the combined index.
- `FUROSHIKI_DICT_AHEAD_MB`: override the bank read-ahead limit in MB.
- `FUROSHIKI_DICT_LEVEL`: override the zstd compression level (default: 3).
