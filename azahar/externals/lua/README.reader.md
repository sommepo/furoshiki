# Lua 5.4.8 (vendored)

Source: https://www.lua.org/ftp/lua-5.4.8.tar.gz
SHA-256: 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
Licence: MIT (see the notice at the end of `lua.h`).

Unmodified files from the tarball's `src/` directory, but for one change in
`lstrlib.c` (marked "reader fork"). The standalone interpreter and compiler
(`lua.c`, `luac.c`) and the libraries the reader never opens (`io`, `os`,
`package`, `debug`, plus `linit.c`) are left out.

The change: a pattern match (`find`, `match`, `gmatch`, `gsub`) counts its
steps and stops with the error "pattern too complex" after ten million of
them (`MAXMATCHSTEPS`; a tenth of a second or so). Lua's instruction count
hook, which stops a profile that runs too long, never runs inside a match, so
a pattern that backtracks without end, such as
`string.rep('a', 400):find('.-.-.-x')`, held the emulator's thread (and the
engine's lock) for about ten seconds with no error. The reader's desktop tests
have the case (`progress_test.cpp`). The same change is in every reader fork's
Lua.

The reader fork builds these sources as C++ (see `externals/CMakeLists.txt`)
so Lua errors unwind as C++ exceptions through the bindings in
`shared/reader/core/lua_api.cpp` (at the repository's top).
