# Building from source

[Back to README](../README.md)

## Repository layout

| Folder | Description |
| --- | --- |
| `home/` | Desktop app (Windows and Linux) |
| `android/` | Android app |
| `shared/` | Shared lookup and Anki card code |
| `home/ocr-helper/` | Python OCR helper (`package.sh` builds its zip) |
| `cores/` | Core interface between Furoshiki and emulators |
| `station/`, `pcsx2/`, `armsx2/`, `ppsspp/`, `flycast/`, `azahar/`, `vita3k/`, `melonds/` | Emulator forks, cores, and game scripts |

## Build steps

All build scripts run on Linux. Each emulator's `tools/reader/env.sh` lists the expected compilers and toolchains.

1. Fetch an emulator's submodules:
   ```bash
   git submodule update --init --recursive -- <folder>
   ```
2. Build its core: `<emulator>/tools/reader/build-core-windows.sh`, `build-core-android.sh`, or `build-core.sh` (Linux).
3. Build Furoshiki:
   - **Windows:** `HOME_PUBLIC=1 home/tools/build-windows.sh`, then `HOME_PUBLIC=1 home/tools/package-windows.sh`
   - **Android:** `android/tools/build.sh`
