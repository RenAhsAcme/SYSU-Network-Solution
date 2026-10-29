#!/bin/sh
set -eu
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/sysu-auth-session.XXXXXX")"
trap 'rm -rf -- "$build"' EXIT HUP INT TERM
"${CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -O1 -g \
    -fno-omit-frame-pointer -fsanitize=address,undefined \
    -I"$here/../src/include" "$here/test_session.c" "$here/../src/md5.c" -o "$build/test-session"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" "$build/test-session"
