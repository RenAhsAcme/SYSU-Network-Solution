#!/bin/sh
set -eu
cd "$(dirname "$0")"
base=$(mktemp -d)
trap 'rm -rf "$base"' EXIT
mkdir -p "$base/var" "$base/tmp/run/sysu-snat/ipv4" "$base/mock-bin"
ln -s "$base/tmp/run" "$base/var/run"
cat > "$base/mock-bin/config" <<'CONFIG'
#!/bin/sh
printf '{"version":%s}\n' "$TEST_VERSION" > "$2/policy.json"
for family in 4 6; do printf "ENABLED=1\nPRIMARY=''\n" > "$2/ipv$family.conf";done
CONFIG
printf '#!/bin/sh\nexit "${TEST_FAIL:-0}"\n' > "$base/mock-bin/lua"
printf '#!/bin/sh\nexit 0\n' > "$base/mock-bin/logger"
chmod 700 "$base/mock-bin/"*
sed "s|/var/run/sysu-snat|$base/var/run/sysu-snat|g; s|/usr/libexec/sysu-snat/config|$base/mock-bin/config|g; s|/etc/hotplug.d/iface/|$base/hotplug/|g; s|/etc/crontabs/root|$base/crontab|g" ../files/sysu-snat.init > "$base/service"
PATH="$base/mock-bin:$PATH"
export PATH
extra_command() { :; }
. "$base/service"
TEST_VERSION=1
export TEST_VERSION
prepare_config
first="$(readlink -f "$base/var/run/sysu-snat/config")"
printf '%s\n' "$first" > "$base/var/run/sysu-snat/ipv4/config-inuse"
TEST_VERSION=2
prepare_config
test -f "$first/policy.json"
second="$(readlink -f "$base/var/run/sysu-snat/config")"
printf '%s\n' "$second" > "$base/var/run/sysu-snat/ipv4/config-inuse"
TEST_VERSION=3
prepare_config
test ! -d "$first"
test -f "$second/policy.json"
before="$(readlink -f "$base/var/run/sysu-snat/config")"
TEST_FAIL=1
export TEST_FAIL
if prepare_config; then echo 'invalid configuration accepted';exit 1;fi
test "$before" = "$(readlink -f "$base/var/run/sysu-snat/config")"
config_load() { :; }
config_get_bool() { eval "$1=1"; }
if (start_service; touch "$base/empty-service-committed"); then exit 1;fi
test ! -f "$base/empty-service-committed"
echo 'service: canonical generation references/retention/invalid config/no empty service commit passed'
