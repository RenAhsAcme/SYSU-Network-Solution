#!/bin/sh
set -eu
cd "$(dirname "$0")"
base=$(mktemp -d)
trap 'rm -rf "$base"' EXIT
mkdir -p "$base/var/run/sysu-snat" "$base/mock-bin"
cat > "$base/mock-bin/ucode" <<'CONFIG'
#!/bin/sh
[ "${TEST_FAIL:-0}" = 0 ] || exit 1
printf '{"version":%s}\n' "$TEST_VERSION" > "$3/policy.json"
CONFIG
printf '#!/bin/sh\nexit 0\n' > "$base/mock-bin/logger"
chmod 700 "$base/mock-bin/"*
sed "s|/var/run/sysu-snat|$base/var/run/sysu-snat|g; s|/usr/bin/ucode|$base/mock-bin/ucode|g" ../files/sysu-snat.init > "$base/service"
PATH="$base/mock-bin:$PATH"
export PATH
extra_command() { :; }
. "$base/service"
TEST_VERSION=1
export TEST_VERSION
prepare_config
first="$(readlink -f "$base/var/run/sysu-snat/config")"
prepare_config
test "$(readlink -f "$base/var/run/sysu-snat/config")" = "$first"
TEST_VERSION=2
prepare_config
second="$(readlink -f "$base/var/run/sysu-snat/config")"
test "$first" != "$second"
test ! -d "$first"
test -f "$second/policy.json"
TEST_FAIL=1
export TEST_FAIL
if prepare_config; then echo 'invalid configuration accepted'; exit 1; fi
test "$(readlink -f "$base/var/run/sysu-snat/config")" = "$second"
config_load() { :; }
config_get_bool() { eval "$1=1"; }
if (start_service; touch "$base/empty-service-committed"); then exit 1; fi
test ! -f "$base/empty-service-committed"
echo 'service: atomic configuration/idempotence/invalid config/no empty service commit passed'
