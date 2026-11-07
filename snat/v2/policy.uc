import { canonical, cidr } from './ip.uc';

function device(s) {
    if (!match(s || '', /^[A-Za-z0-9_.:-]{1,15}$/)) die('invalid device');
    return '"' + s + '"';
}
export function network(s, family) {
    if (type(s) != 'string') die('invalid network');
    let p = split(s, '/');
    if (length(p) > 2 || length(p) == 2 && !match(p[1], /^[0-9]{1,3}$/)) die('invalid network prefix');
    return cidr(p[0], length(p) == 2 ? +p[1] : family == 4 ? 32 : 128, family);
};
function port(s) {
    let p = split(s, '-');
    if (length(p) > 2 || !match(s, /^[0-9]+(-[0-9]+)?$/) || +p[0] < 1 || +p[0] > 65535 ||
        (length(p) == 2 && (+p[1] < +p[0] || +p[1] > 65535))) die('invalid port');
    return s;
}
export function table_name(family) { return 'sysu_snat_v2_' + family; };

export function rules(config, ctx, members, now, mapping) {
    let family = ctx.family, ip = family == 4 ? 'ip' : 'ip6', f = config['ipv' + family];
    let name = table_name(family), primary = canonical(ctx.primary, family), entries = [], out = [];
    let active = filter(members, m => m.state == 'active' && m.bound && m.expires > now);
    if (length(active) > 5) die('pool exceeds capacity');
    function fallback() { return f.fallback == 'primary' ? 'counter snat to ' + primary : 'counter return'; }
    push(out, 'add table ' + ip + ' ' + name, 'delete table ' + ip + ' ' + name,
        'table ' + ip + ' ' + name + ' {',
        'set coordinator { type ifname; flags timeout; elements = { ' + device(ctx.device) + ' timeout 90s }; }');
    for (let i = 0; i < length(active); i++) {
        let m = active[i], timeout = int(m.expires - now);
        if (timeout < 1) timeout = 1;
        push(entries, i + ' : goto slot' + i);
        push(out, 'set lease' + i + ' { type ifname; flags timeout; elements = { ' + device(ctx.device) + ' timeout ' + timeout + 's }; }',
            'chain slot' + i + ' { oifname @coordinator oifname @lease' + i + ' counter snat to ' + canonical(m.address, family) + '; ' + fallback() + '; }');
    }
    let n = length(active);
    if (n) {
        push(out, 'map randoms { typeof numgen random mod ' + n + ' : verdict; elements = { ' + join(', ', entries) + ' }; }');
        {
            if (length(mapping || []) != 1024) die('invalid stable mapping');
            let buckets = [];
            for (let i = 0; i < 1024; i++) {
                if (mapping[i] < 0 || mapping[i] >= n) die('invalid slot');
                push(buckets, i + ' : goto slot' + mapping[i]);
            }
            push(out, 'map stable { typeof jhash ' + ip + ' saddr mod 1024 : verdict; elements = { ' + join(', ', buckets) + ' }; }');
        }
    }
    push(out, 'chain postrouting { type nat hook postrouting priority 99; policy accept;',
        ' ' + ip + ' saddr ' + primary + ' meta skgid 0 counter return');
    function action(mode) {
        if (index(['original', 'primary', 'random', 'host', 'endpoint'], mode) < 0) die('invalid mode');
        if (mode == 'original') return 'counter return';
        if (mode == 'primary') return 'counter snat to ' + primary;
        if (!n) return fallback();
        if (mode == 'random') return 'numgen random mod ' + n + ' vmap @randoms';
        let expression = ip + ' saddr' + (mode == 'endpoint' ? ' . udp sport' : '');
        return 'jhash ' + expression + ' mod 1024 seed ' +
            (mode == 'endpoint' ? '0x7461696c' : '0x73797375') + ' vmap @stable';
    }
    function base(scope) {
        let common = 'oifname ' + device(ctx.device) + ' ' + ip + ' daddr != ' + network(ctx.wan_cidr, family);
        if (scope == 'proxy') {
            if (!config.main.proxy_enabled) return null;
            let gid = '' + config.main.proxy_gid;
            if (!match(gid, /^[0-9]+$/) || +gid < 1 || +gid >= 4294967295) die('invalid GID');
            return 'meta skgid ' + gid + ' ' + common;
        }
        if (scope == 'tailscale') {
            if (!config.main.tailscale_enabled) return null;
            return 'iifname ' + device(config.main.tailscale_device) + ' ' + ip + ' saddr ' +
                network(config.main['tailscale_ipv' + family], family) + ' ' + common;
        }
        if (scope != 'lan') die('invalid scope');
        return 'iifname ' + device(ctx.landev) + ' ' + ip + ' saddr ' + (family == 4 ? network(ctx.lan_cidr, 4) :
            '{ ' + network(ctx.lan_cidr, 6) + ', ' + network(ctx.wan_cidr, 6) + ' }') + ' ' + common;
    }
    for (let r in config.rules || []) {
        if (+r.family != family) continue;
        let b = base(r.scope); if (!b) continue;
        for (let key in ['src', 'dest']) if (length(r[key] || '')) b += ' ' + ip + (key == 'src' ? ' saddr ' : ' daddr ') + network(r[key], family);
        if (index(['any', 'tcp', 'udp'], r.proto) < 0) die('invalid protocol');
        if (r.proto != 'any') b += ' meta l4proto ' + r.proto;
        for (let key in ['sport', 'dport']) if (length(r[key] || '')) {
            if (r.proto == 'any') die('port needs transport');
            b += ' ' + r.proto + ' ' + key + ' ' + port(r[key]);
        }
        if (r.mode == 'endpoint' && r.proto != 'udp') die('endpoint needs UDP');
        push(out, ' ' + b + ' ' + action(r.mode));
    }
    for (let scope in ['lan', 'tailscale', 'proxy']) {
        let b = base(scope); if (!b) continue;
        for (let proto in ['tcp', 'udp', 'other'])
            push(out, ' ' + b + (proto == 'other' ? '' : ' meta l4proto ' + proto) + ' ' + action(f[scope + '_' + proto]));
    }
    push(out, '}', '}');
    return join('\n', out) + '\n';
};

export function probe_rules(family, dev, addresses, start_port, targets) {
    let ip = family == 4 ? 'ip' : 'ip6', name = 'sysu_snat_probe_' + family;
    let entries = map(addresses, a => canonical(a, family) + ' : ' + canonical(a, family));
    let scope = 'meta skgid 0';
    if (start_port && length(targets || [])) {
        let destinations = {}, ports = {};
        for (let t in targets) { destinations[canonical(t.address, family)] = true; ports[t.port] = true; }
        scope += ' meta l4proto tcp tcp sport ' + start_port + '-' + (start_port + 20) +
            ' tcp dport { ' + join(', ', keys(ports)) + ' } ' + ip + ' daddr { ' + join(', ', keys(destinations)) + ' }';
    }
    return 'add table ' + ip + ' ' + name + '\ndelete table ' + ip + ' ' + name + '\n' +
        'table ' + ip + ' ' + name + ' { map sources { type ' + (family == 4 ? 'ipv4_addr' : 'ipv6_addr') + ' : ' +
        (family == 4 ? 'ipv4_addr' : 'ipv6_addr') + '; elements = { ' + join(', ', entries) + ' }; }\n' +
        'chain postrouting { type nat hook postrouting priority 98; policy accept; ' + scope + ' oifname ' + device(dev) +
        ' ct mark set 0x53534e32 counter snat to ' + ip + ' saddr map @sources; }\n}\n';
};
