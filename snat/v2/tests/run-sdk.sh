#!/bin/sh
set -eu
sdk=${1:?Usage: run-sdk.sh /path/to/openwrt-sdk}
sdk=$(realpath "$sdk")
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
runtime=$(find "$sdk/build_dir/target-x86_64_musl" -maxdepth 1 -type d -name 'ucode-20*' | sort | tail -n 1)
toolchain=$(find "$sdk/staging_dir" -maxdepth 1 -type d -name 'toolchain-x86_64_gcc-*_musl' | sort | tail -n 1)
target="$sdk/staging_dir/target-x86_64_musl"
[ -x "$runtime/ucode" ] && [ -e "$toolchain/lib/ld-musl-x86_64.so.1" ] || exit 1
libs="$runtime:$target/lib:$target/usr/lib:$toolchain/lib:$toolchain/usr/lib"
export LD_LIBRARY_PATH="$libs"
for test in core runtime io validate; do
    "$toolchain/lib/ld-musl-x86_64.so.1" --library-path "$libs" "$runtime/ucode" \
        -L "$runtime/*.so" "$here/$test.uc"
done
for module in daemon status control action config; do
    "$toolchain/lib/ld-musl-x86_64.so.1" --library-path "$libs" "$runtime/ucode" \
        -L "$runtime/*.so" -c -o /dev/null "$here/../$module.uc"
done
