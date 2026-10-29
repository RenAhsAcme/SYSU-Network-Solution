#!/bin/sh
set -eu
dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/sysu-auth-state.XXXXXX")"
trap 'rm -rf -- "$build"' EXIT HUP INT TERM
"${CC:-cc}" -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
    -I"$dir/../src/include" "$dir/test_state.c" \
    "$dir/../src/eap_state_machine.c" "$dir/../src/util/sched_alarm.c" \
    "$dir/../src/util/packet_util.c" -o "$build/test_state"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" "$build/test_state"
