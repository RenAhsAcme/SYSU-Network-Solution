#!/bin/sh
set -eu
# 必须由调用者创建空网络空间；拒绝在含默认路由的环境运行。
[ "$(id -u)" = 0 ] || { echo 'root in an isolated network namespace required' >&2; exit 1; }
[ -z "$(ip route show default)" ] && [ -z "$(ip -6 route show default)" ] || exit 1
[ "$(ip -o link show | wc -l)" -eq 1 ] || exit 1
binary=$(realpath "${1:?Usage: unshare -n -- sh network.sh /path/to/arp}")
work=$(mktemp -d)
responder=
cleanup() {
    [ -z "$responder" ] || { kill "$responder" 2>/dev/null || :; wait "$responder" 2>/dev/null || :; }
    rm -f "$work/responder.py" "$work/conflict" "$work/free" "$work/ready" "$work/native-before" "$work/native-after"
    rmdir "$work"
}
trap cleanup EXIT
ip link set lo up
ip link add wan-test type veth peer name peer-test
ip link set wan-test up
ip link set peer-test up
ip addr add 172.25.192.65/22 dev wan-test
ip addr add 172.25.192.1/22 dev peer-test
ip route add default via 172.25.192.1 dev wan-test
ip route get 172.25.195.200 > "$work/native-before"
cat > "$work/responder.py" <<'PY'
import socket, struct, pathlib, sys
s=socket.socket(socket.AF_PACKET,socket.SOCK_RAW,socket.htons(0x0806))
s.bind(('peer-test',0))
pathlib.Path(sys.argv[1]).touch()
fake=bytes.fromhex('02aabbccddee')
while True:
    packet=s.recv(2048)
    if len(packet)<42 or packet[12:14]!=b'\x08\x06':continue
    arp=packet[14:42]
    if arp[6:8]!=b'\x00\x01' or arp[24:28]!=socket.inet_aton('172.25.193.7'):continue
    reply=struct.pack('!HHBBH',1,0x0800,6,4,2)+fake+arp[24:28]+arp[8:14]+arp[14:18]
    s.send(arp[8:14]+fake+b'\x08\x06'+reply)
PY
python3 "$work/responder.py" "$work/ready" & responder=$!
for i in 1 2 3 4 5; do [ ! -f "$work/ready" ] || break; sleep 1; done
test -f "$work/ready"
if "$binary" probe wan-test 172.25.193.7 > "$work/conflict"; then
    echo 'conflicting candidate accepted' >&2; exit 1
else
    test "$?" -eq 1
fi
grep -q '"event":"conflict"' "$work/conflict"
"$binary" probe wan-test 172.25.195.240 > "$work/free"
grep -q '"event":"clear"' "$work/free"
ip route get 172.25.195.200 > "$work/native-after"
cmp "$work/native-before" "$work/native-after"
test "$(ip -o -4 addr show dev wan-test | wc -l)" -eq 1
ping -I 172.25.192.65 -c 1 -W 1 172.25.192.1 >/dev/null
echo 'isolated ARP: conflict rejected, silent candidate clear, native WAN address/route/connectivity preserved'
