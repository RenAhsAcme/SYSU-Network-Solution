#!/bin/sh
set -eu
cd "$(dirname "$0")"
base=$(mktemp -d)
worker=
cleanup() {
    if [ -n "$worker" ]; then kill "$worker" 2>/dev/null || :; wait "$worker" 2>/dev/null || :; fi
    rm -rf "$base"
}
trap cleanup EXIT
mkdir -p "$base/usr/libexec/sysu-snat" "$base/var/run/sysu-snat/config" "$base/mock-bin"
printf 'CHECK_INTERVAL=60\n' > "$base/var/run/sysu-snat/config/ipv4.conf"
printf '#!/bin/sh\nexit 0\n' > "$base/mock-bin/logger"
chmod 700 "$base/mock-bin/logger"
printf '#!/bin/sh\n[ "$1" != sync ] || [ ! -f "$SNAT_BASE/dns-denied" ]\n' > "$base/usr/libexec/sysu-snat/dns"
chmod 700 "$base/usr/libexec/sysu-snat/dns"
cat > "$base/usr/libexec/sysu-snat/ipv4" <<'ENGINE'
#!/bin/sh
case "$1" in
check)
    if ! mkdir "$SNAT_BASE/busy"; then touch "$SNAT_BASE/overlap";exit 1;fi
    trap 'rmdir "$SNAT_BASE/busy";exit 130' TERM INT
    echo check >> "$SNAT_BASE/calls"
    sleep 2
    rmdir "$SNAT_BASE/busy";;
rules) echo rules >> "$SNAT_BASE/calls";;
clear) touch "$SNAT_BASE/cleared";;
esac
ENGINE
chmod 700 "$base/usr/libexec/sysu-snat/ipv4"
touch "$base/dns-denied"
SNAT_BASE="$base" sh ../src/worker 4 &
worker=$!
sleep 1
test ! -f "$base/calls"
rm "$base/dns-denied"
kill -USR1 "$worker"
for n in $(seq 1 10); do [ ! -f "$base/calls" ] || break;sleep 1;done
test -f "$base/calls"
SNAT_BASE="$base" sh ../src/worker 4 && exit 1
for n in 1 2 3; do kill -USR1 "$worker";sleep 1;done
sleep 3
test "$(grep -c check "$base/calls")" -ge 2
test ! -f "$base/overlap"
kill -TERM "$worker"
wait "$worker"
worker=
test -f "$base/cleared"
test ! -d "$base/busy"
test ! -f "$base/var/run/sysu-snat/ipv4/worker.pid"
echo 'worker: duplicate start/event coalescing/no overlap/stop cleanup passed'
