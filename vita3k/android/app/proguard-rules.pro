# this is necessary so that native functions can call Java ones using JNI
-keep class org.libsdl.app** { *; }
-keep class org.vita3k.emulator.NativeLib { *; }
-keep class org.vita3k.emulator.Emulator { *; }
-keep class org.vita3k.emulator.EmuSurface { *; }
-keep class org.vita3k.emulator.overlay.** { *; }
# Reader fork: the bridge calls the reader by name, and its web popup calls back into it
-keep class org.vita3k.emulator.reader.** { *; }
-keep class org.vita3k.emulator.data.EmulatorConfig { *; }
-keep class org.vita3k.emulator.data.NativeAppInfo { *; }
-keep class org.vita3k.emulator.data.NativeImeState { *; }
-keep class org.vita3k.emulator.data.NativeTrophy { *; }
-keep class org.vita3k.emulator.data.NativeTrophyCollection { *; }
-keep class org.vita3k.emulator.data.NativeUser { *; }
-keep interface org.vita3k.emulator.data.InstallCallback { *; }
