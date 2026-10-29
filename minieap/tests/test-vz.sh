#!/bin/sh
set -eu
here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
hashes="$here/../src/packet_plugin/rjv3/rjv3_hashes"
build="$(mktemp -d /tmp/sysu-vz.XXXXXX)"
trap 'rm -rf "$build"' EXIT HUP INT TERM
reference=""
if [ "${1:-}" = --generate ]; then
    reference="$2"
    generate=1
else
    reference="${1:-}"
    generate=0
fi
if [ -n "$reference" ]; then
    g++ -std=c++17 -O2 -I"$reference/src" "$here/vz_reference.cpp" \
        "$reference/src/rg/vz/md5vz.cpp" "$reference/src/rg/vz/sha1vz.cpp" \
        "$reference/src/rg/vz/ripemd128.cpp" "$reference/src/rg/vz/tiger.cpp" \
        "$reference/src/rg/vz/tiger_tables.cpp" "$reference/src/rg/vz/whirlpool_variant.cpp" \
        "$reference/src/rg/vz/vz_tables.cpp" "$reference/src/rg/vz/vz_kdf.cpp" -o "$build/reference"
fi
if [ "$generate" = 1 ]; then
    "$build/reference" --vectors > "$here/vz_reference.h"
    exit 0
fi
for chars in signed unsigned; do
    gcc -std=gnu11 -O1 -g -Wall -Wextra -Werror -f"$chars"-char \
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
        -I"$here/../src/include" -I"$hashes" "$here/test_vz.c" \
        "$hashes/checkV4.c" "$hashes/rjmd5.c" "$hashes/rjsha1.c" \
        "$hashes/rjripemd128.c" "$hashes/rjtiger.c" "$hashes/rjtiger_sbox.c" \
        "$hashes/rjwhirlpool.c" "$hashes/rjwhirlpool_sbox.c" "$hashes/byte_order.c" \
        "$here/../src/md5.c" -o "$build/test-$chars"
    "$build/test-$chars"
    if [ -n "$reference" ]; then
        "$build/test-$chars" --dump > "$build/challenges"
        "$build/reference" < "$build/challenges"
    fi
done
