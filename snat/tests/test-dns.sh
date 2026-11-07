#!/bin/sh
set -eu
cd "$(dirname "$0")"
base=$(mktemp -d)
trap 'rm -rf "$base"' EXIT
export SNAT_DNS_BASE="$base"
mkdir -p "$base/etc/config" "$base/uci" "$base/var/run/sysu-snat/config" "$base/mock-bin"
cp ../files/sysu-snat-dns.uc "$base/helper"
mkdir -p "$base/v2" "$base/usr/libexec/sysu-snat/v2"
cp ../v2/io.uc "$base/v2/io.uc"
cp ../v2/io.uc "$base/usr/libexec/sysu-snat/v2/io.uc"
cat > "$base/etc/config/dhcp" <<'CONFIG'
config dnsmasq 'first'
    list notinterface 'wan'
    list notinterface 'guest'
config dnsmasq 'second'
    option port '5353'
CONFIG
cat > "$base/network.json" <<'NETWORK'
{"wan":{"l3_device":"eth0"},"wan6":{"l3_device":"eth0"},"lan":{"l3_device":"br-lan"},"uplink":{"l3_device":"eth1"}}
NETWORK
cat > "$base/var/run/sysu-snat/config/policy.json" <<'POLICY'
{"main":{"enabled":1,"dnsmasq_protect":1},"ipv4":{"enabled":1,"wan":"wan","lan":"lan"},"ipv6":{"enabled":1,"wan":"wan6","lan":"lan"}}
POLICY
cat > "$base/mock-bin/dnsmasq" <<'RELOAD'
#!/bin/sh
echo reload >> "$SNAT_DNS_BASE/reloads"
[ ! -f "$SNAT_DNS_BASE/fail" ]
RELOAD
chmod 700 "$base/mock-bin/dnsmasq"
ucode "$base/helper" sync
test "$(wc -l < "$base/reloads")" -eq 1
cp "$base/etc/config/dhcp" "$base/active"
ucode "$base/helper" sync
cmp "$base/etc/config/dhcp" "$base/active"
test "$(wc -l < "$base/reloads")" -eq 1
grep -q sysu_snat_notinterface "$base/etc/config/dhcp"
test "$(uci -c "$base/etc/config" -t "$base/uci" get dhcp.second.notinterface)" = 'wan eth0 wan6'
uci -c "$base/etc/config" -t "$base/uci" set dhcp.first.test_pending=1
if ucode "$base/helper" sync; then exit 1;fi
cmp "$base/etc/config/dhcp" "$base/active"
uci -c "$base/etc/config" -t "$base/uci" revert dhcp
ucode "$base/helper" release
grep -q "notinterface 'wan'" "$base/etc/config/dhcp"
grep -q "notinterface 'guest'" "$base/etc/config/dhcp"
! grep -q sysu_snat_notinterface "$base/etc/config/dhcp"
! grep -q "notinterface 'eth0'" "$base/etc/config/dhcp"
cp "$base/etc/config/dhcp" "$base/before"
touch "$base/fail"
if ucode "$base/helper" sync; then exit 1;fi
cmp "$base/before" "$base/etc/config/dhcp"
rm "$base/fail"
sed -i 's/br-lan/eth0/' "$base/network.json"
if ucode "$base/helper" sync; then exit 1;fi
cmp "$base/before" "$base/etc/config/dhcp"
sed -i 's/"lan":{"l3_device":"eth0"}/"lan":{"l3_device":"br-lan"}/' "$base/network.json"
sed -i 's/"wan":"wan"/"wan":"uplink"/' "$base/var/run/sysu-snat/config/policy.json"
ucode "$base/helper" sync
grep -q "notinterface 'eth1'" "$base/etc/config/dhcp"
mkdir -p "$base/var/run/sysu-snat/v2"
printf '%s\n' '{"families":{"ipv4":{"ctx":{"device":"eth2"},"members":[{"bound":true}]}}}' > "$base/var/run/sysu-snat/v2/state.json"
ucode "$base/helper" sync
grep -q "notinterface 'eth2'" "$base/etc/config/dhcp"
rm "$base/var/run/sysu-snat/v2/state.json"
ucode "$base/helper" sync
! grep -q "notinterface 'eth2'" "$base/etc/config/dhcp"
sed -i 's/"dnsmasq_protect":1/"dnsmasq_protect":0/' "$base/var/run/sysu-snat/config/policy.json"
ucode "$base/helper" sync
cmp "$base/before" "$base/etc/config/dhcp"
sed -i 's/"dnsmasq_protect":0/"dnsmasq_protect":1/' "$base/var/run/sysu-snat/config/policy.json"
sed -i 's/^{/{"dnsmasq_running":false,/' "$base/network.json"
count="$(wc -l < "$base/reloads")"
ucode "$base/helper" sync
test "$count" = "$(wc -l < "$base/reloads")"
ucode "$base/helper" release
test "$count" = "$(wc -l < "$base/reloads")"
cmp "$base/before" "$base/etc/config/dhcp"
mkdir -p "$base/usr/libexec/sysu-snat" "$base/var/run/sysu-snat/v2"
cp "$base/helper" "$base/usr/libexec/sysu-snat/dns-config"
chmod 700 "$base/usr/libexec/sysu-snat/dns-config"
printf '#!/bin/sh\nexit 0\n' > "$base/mock-bin/logger"
chmod 700 "$base/mock-bin/logger"
export PATH="$base/mock-bin:$PATH"
sh ../files/sysu-snat-dns sync
echo $$ > "$base/var/run/sysu-snat/v2/daemon.pid"
sh ../files/sysu-snat-dns release
grep -q sysu_snat_notinterface "$base/etc/config/dhcp"
rm "$base/var/run/sysu-snat/v2/daemon.pid"
sh ../files/sysu-snat-dns release
cmp "$base/before" "$base/etc/config/dhcp"
printf 'not a valid UCI configuration\n' > "$base/etc/config/dhcp"
if ucode "$base/helper" sync; then exit 1;fi
echo 'dns: multiple instances/shared WAN/idempotence/ownership/rollback/interface change/switch/pending changes/stopped DNS/invalid configuration passed'
