#!/bin/sh
set -eu
dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
build="$(mktemp -d "${TMPDIR:-/tmp}/sysu-auth-protocol.XXXXXX")"
trap 'rm -rf -- "$build"' EXIT HUP INT TERM
"${CC:-cc}" -D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie -no-pie \
    -I"$dir/../src/include" -I"$dir/../src/packet_plugin/rjv3/rjv3_hashes" \
    "$dir/test_protocol.c" "$dir/../src/packet_builder.c" "$dir/../src/sysu_protocol.c" \
    "$dir/../src/util/packet_util.c" "$dir/../src/md5.c" -o "$build/test_protocol"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}" "$build/test_protocol"
