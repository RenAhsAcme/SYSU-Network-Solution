import { ipv4, address } from './core.uc';
import { random_source } from './random.uc';

export function v6(ip) {
    if (type(ip) != 'string' || !match(ip, /^[0-9a-fA-F:]+$/)) die('invalid IPv6');
    let halves = split(ip, '::');
    if (length(halves) > 2) die('invalid IPv6 compression');
    function parts(s) {
        if (!length(s)) return [];
        let a = split(s, ':');
        for (let i = 0; i < length(a); i++) {
            if (!match(a[i], /^[0-9a-fA-F]{1,4}$/)) die('invalid IPv6 group');
            a[i] = hex(a[i]);
        }
        return a;
    }
    let a = parts(halves[0]), b = length(halves) == 2 ? parts(halves[1]) : [];
    if (length(halves) == 2) {
        if (length(a) + length(b) >= 8) die('invalid IPv6 width');
        while (length(a) + length(b) < 8) push(a, 0);
    }
    a = [...a, ...b];
    if (length(a) != 8) die('invalid IPv6 width');
    return join(':', map(a, n => sprintf('%04x', n)));
};

export function canonical(ip, family) { return family == 4 ? address(ipv4(ip)) : v6(ip); };

export function cidr(ip, prefix, family) {
    if (prefix < 0 || prefix > (family == 4 ? 32 : 128)) die('invalid CIDR');
    if (family == 4) { let size = 1 << (32-prefix); return address(int(ipv4(ip) / size)*size) + '/' + prefix; }
    let a = split(v6(ip), ':');
    for (let i = 0; i < 8; i++) {
        let bits = prefix - i*16;
        if (bits < 0) bits = 0; if (bits > 16) bits = 16;
        let step = 1 << (16-bits);
        a[i] = sprintf('%04x', int(hex(a[i]) / step)*step);
    }
    return join(':', a) + '/' + prefix;
};

export function candidate6(primary, random) {
    let a = split(v6(primary), ':');
    for (let i = 4; i < 8; i++) a[i] = sprintf('%04x', random(65536));
    a[4] = sprintf('%04x', hex(a[4]) & ~512);
    let ip = join(':', a);
    return ip == v6(primary) || !match(substr(ip, 20), /[1-9a-f]/) ? null : ip;
};
