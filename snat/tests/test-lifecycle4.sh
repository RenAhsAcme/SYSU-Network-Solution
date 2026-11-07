#!/bin/sh
set -eu
cd "$(dirname "$0")"
base=$(mktemp -d)
trap 'rm -rf "$base"' EXIT
export AWP_BASE="$base"
mkdir -p "$base/mock-bin" "$base/var/run/sysu-snat/config" "$base/usr/lib/sysu-snat"
cp fixtures/ipv4.conf "$base/var/run/sysu-snat/config/ipv4.conf"
cp fixtures/policy.json "$base/var/run/sysu-snat/config/policy.json"
sed -i 's/^ENABLED=.*/ENABLED=1/' "$base/var/run/sysu-snat/config/ipv4.conf"
cp ../src/*.lua "$base/usr/lib/sysu-snat/"
cat > "$base/mock-bin/mock" <<'MOCK'
#!/bin/sh
case "$(basename "$0")" in
 logger) printf '%s\n' "$*" >> "$AWP_BASE/log";;
 ubus)
    [ "${AWP_DOWN:-0}" != 1 ] || exit 1
    case "$2" in
      *.wan) echo '{"up":true,"l3_device":"eth2","ipv4-address":[{"address":"10.1.2.2","mask":29}]}' ;;
      *) echo '{"up":true,"l3_device":"br-lan","ipv4-address":[{"address":"192.168.10.1","mask":24}]}' ;;
    esac;;
 ip)
    case "$*" in
      '-4 route show default dev eth2') echo 'default via 10.1.2.1 dev eth2';;
      '-o -4 addr show'*)
        echo '2: eth2 inet 10.1.2.2/29 scope global'
        if [ -f "$AWP_BASE/aliases" ]; then
          while read -r addr; do echo "2: eth2 inet $addr scope global"; done < "$AWP_BASE/aliases"
        fi;;
      'neigh show to '*) echo "$4 FAILED";;
      'addr add '*) echo "$3" >> "$AWP_BASE/aliases";;
      'addr del '*) grep -vxF "$3" "$AWP_BASE/aliases" > "$AWP_BASE/aliases.next" || :; mv "$AWP_BASE/aliases.next" "$AWP_BASE/aliases";;
    esac;;
 nft)
    case "$*" in
      'list table ip awp_auto') test -f "$AWP_BASE/nat";;
      'delete table ip awp_auto') rm -f "$AWP_BASE/nat";;
      '-f '*) case "$2" in */rules.nft|*/rules.next) touch "$AWP_BASE/nat";; esac;;
    esac;;
 curl)
    [ "${AWP_FAIL:-0}" != 1 ] || exit 1
    case "$*" in *'--interface 10.1.2.5 '*) echo 'mock: connection failed' >&2; exit 1;; esac
    case "$*" in *'%{remote_ip}'*) echo 183.240.99.224;; *) case "$*" in *'--connect-to ::183.240.99.224:'*) :;; *) exit 1;; esac;; esac;;
 ping|sleep) :;;
esac
MOCK
chmod 700 "$base/mock-bin/mock"
for name in logger ubus ip nft curl ping sleep; do ln -s mock "$base/mock-bin/$name"; done
PATH="$base/mock-bin:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH
for name in logger ubus ip nft curl ping sleep; do
  test "$(command -v "$name")" = "$base/mock-bin/$name"
done
grep -qF '[ -z "$BASE" ] || PATH="$BASE/mock-bin:$PATH"' ../src/ipv4
sh ../src/ipv4 refresh > "$base/stdout"
grep -q 'ARP 进度：4/4' "$base/stdout"
grep -q 'HTTPS 验证失败：10.1.2.5' "$base/stdout"
grep -q 'HTTPS 验证完成：4/4，通过=3' "$base/stdout"
! grep -q '进度\|HTTPS 验证失败' "$base/log"
grep -q '地址池已启用' "$base/log"
test "$(wc -l < "$base/var/run/sysu-snat/ipv4/pool")" -eq 3
before=$(sha256sum "$base/var/run/sysu-snat/ipv4/pool")
logs=$(wc -l < "$base/log")
sh ../src/ipv4 check >> "$base/stdout"
test "$before" = "$(sha256sum "$base/var/run/sysu-snat/ipv4/pool")"
test "$logs" -eq "$(wc -l < "$base/log")"
rm "$base/nat"
sh ../src/ipv4 check >> "$base/stdout"
grep -q '已恢复丢失' "$base/log"
mkdir -p "$base/etc/sysu-snat"
echo 10.1.2.3 > "$base/etc/sysu-snat/ipv4-excluded"
sh ../src/ipv4 refresh >> "$base/stdout"
! grep -qxF 10.1.2.3 "$base/var/run/sysu-snat/ipv4/pool"
grep -qxF '10.1.2.3 excluded' "$base/var/run/sysu-snat/ipv4/reserved"
! grep -qxF 10.1.2.3/32 "$base/aliases"
test "$(wc -l < "$base/var/run/sysu-snat/ipv4/pool")" -eq 2
AWP_FAIL=1 sh ../src/ipv4 refresh >> "$base/stdout" 2>&1 && exit 1
grep -q '原 WAN 的 HTTPS 检查失败' "$base/log"
grep -q '阶段=原WAN连通性检查' "$base/log"
test ! -f "$base/var/run/sysu-snat/ipv4/active"
test ! -s "$base/aliases"
logs=$(wc -l < "$base/log")
sh ../src/ipv4 check >> "$base/stdout"
test "$logs" -eq "$(wc -l < "$base/log")"
AWP_DOWN=1 sh ../src/ipv4 check >> "$base/stdout"
logs=$(wc -l < "$base/log")
AWP_DOWN=1 sh ../src/ipv4 check >> "$base/stdout"
test "$logs" -eq "$(wc -l < "$base/log")"
test "$(grep -c 'WAN 未就绪' "$base/log")" -eq 1
echo 'logging: rebuild/progress/probe failure/healthy check/rollback/retry checks passed'
