# The reader's change to Lua

The reader builds its own Lua (`reader_lua` in `reader/CMakeLists.txt`) from the
emulator's (`ext/lua`, a submodule: Lua 5.4.7), as C++, but takes `lstrlib.c`
from here: the submodule's file with one change (marked "reader fork"). The
emulator's own Lua, built as C, is left as it is.

The change: a pattern match (`find`, `match`, `gmatch`, `gsub`) counts its
steps and stops with the error "pattern too complex" after ten million of
them (`MAXMATCHSTEPS`; a tenth of a second or so). Lua's instruction count
hook, which stops a profile that runs too long, never runs inside a match, so
a pattern that backtracks without end, such as
`string.rep('a', 400):find('.-.-.-x')`, held the emulator's thread (and the
engine's lock) for about ten seconds with no error. The reader's desktop tests
have the case (`progress_test.cpp`). The same change is in every reader fork's
Lua (Flycast Reader's `reader/lua` is the copy the others are taken from).

If the submodule is moved to another Lua release, this file is taken from it
again and the change made anew (its `lstrlib.c` was 5.4.7's, the same file as
5.4.8's).
