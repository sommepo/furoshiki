#!/bin/sh
# Reader fork: LLVM's stand-in for Microsoft's assembler, told that the target is 64-bit
# Windows (it does not take that from the build's other settings). The build hands it the C
# compiler's options too; those it would misread (-msse4.1 as a machine named "sse4.1") are
# left out.
for arg do
  shift
  case "$arg" in
    -msse*|-mavx*|-march=*|/Zi|-Zi) ;;
    *) set -- "$@" "$arg" ;;
  esac
done
exec "$READER_HOST_PREFIX/bin/llvm-ml" -m64 "$@"
