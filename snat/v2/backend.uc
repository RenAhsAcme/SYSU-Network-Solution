import * as fs from 'fs';
import { connect } from 'ubus';
import { canonical, cidr, v6 } from './ip.uc';
import { plan, ipv4 } from './core.uc';
import { capture, bounded, save, clock, json_command } from './io.uc';
import { rules, table_name, probe_rules } from './policy.uc';

export const RUN = '/var/run/sysu-snat/v2';
export const NET = '/usr/libexec/sysu-snat/net-helper';
export const ARP = '/usr/libexec/sysu-snat/arp';

export function execute(args) {
    let r = bounded(NET, args, 12);
    if (r.code) die('operation failed: ' + join(' ', args));
    return r.text;
};
export function nft(text, family, label) {
    let path = RUN + '/' + family + '-' + label + '.nft';
    if (!fs.writefile(path, text) || !fs.chmod(path, 0600)) die('nft staging failed');
    execute(['/usr/sbin/nft', '-c', '-f', path]);
    execute(['/usr/sbin/nft', '-f', path]);
};
export function context(config, family) {
    let f = config['ipv' + family], bus = connect('/var/run/ubus/ubus.sock');
    if (!bus) die('ubus unavailable');
    let wan = bus.call('network.interface.' + f.wan, 'status'), lan = bus.call('network.interface.' + f.lan, 'status');
    if (!wan?.up || !lan?.up) die('interface unavailable');
    for (let dev in [wan.l3_device, lan.l3_device])
        if (!match(dev || '', /^[A-Za-z0-9_.:-]{1,15}$/)) die('invalid device');
    if (wan.l3_device == lan.l3_device) die('shared WAN/LAN unsupported');
    let natives = filter(wan['ipv' + family + '-address'] || [], a => family == 4 ||
        ((a.preferred || 0) > 0 && match(a.address, /^[23]/)));
    let selected = filter(natives, a => !f.primary || canonical(f.primary, family) == canonical(a.address, family))[0];
    if (!selected) die('native primary unavailable');
    let c = { family, interface: f.wan, device: wan.l3_device, landev: lan.l3_device,
        primary: canonical(selected.address, family), prefix: selected.mask,
        native: map(natives, a => ({ address: a.address, prefix: a.mask, preferred: a.preferred,
            source: wan.proto || 'netifd', primary: a == selected })) };
    let defaults = json_command(['/sbin/ip', '-j', '-' + family, 'route', 'show', 'default', 'dev', c.device]);
    let gateway = filter(defaults, r => r.gateway)[0]?.gateway;
    if (!gateway) die('gateway unavailable');
    c.gateway = canonical(gateway, family);
    if (family == 4) {
        let a = lan['ipv4-address']?.[0]; if (!a) die('LAN IPv4 unavailable');
        c.plan = plan(c.primary, c.prefix, gateway, a.address, a.mask);
        c.wan_cidr = cidr(c.primary, c.prefix, 4); c.lan_cidr = cidr(a.address, a.mask, 4);
        c.valid_until = null;
    } else {
        let a = lan['ipv6-prefix-assignment']?.[0];
        if (c.prefix != 64 || !a || a.mask < 48 || a.mask > 64 || !match(v6(a.address), /^f[cd]/) ||
            !match(c.gateway, /^fe80:/)) die('unsupported IPv6 topology');
        c.wan_cidr = cidr(c.primary, 64, 6); c.lan_cidr = cidr(a.address, a.mask, 6);
        let lifetime = selected.valid || selected.preferred;
        c.valid_until = lifetime > 0 ? clock() + lifetime : null;
    }
    c.signature = join('|', [c.device, c.primary, c.prefix, c.gateway, c.landev, c.wan_cidr, c.lan_cidr]);
    return c;
};

export function present(ctx) {
    let entries = json_command(['/sbin/ip', '-j', '-' + ctx.family, 'addr', 'show', 'dev', ctx.device]);
    let out = {};
    for (let dev in entries) for (let a in dev.addr_info || []) {
        if (!a.local) continue;
        let valid = !a.tentative && !a.dadfailed && index(a.flags || [], 'tentative') < 0 && index(a.flags || [], 'dadfailed') < 0;
        out[canonical(a.local, ctx.family)] = { prefix: a.prefixlen, ready: valid, protocol: a.protocol,
            preferred: a.preferred_life_time, valid: a.valid_life_time };
    }
    return out;
};
export function add_address(ctx, m) {
    let address = canonical(m.address, ctx.family), native = map(ctx.native, a => canonical(a.address, ctx.family));
    if (index(native, address) >= 0 || address == ctx.gateway || present(ctx)[address]) die('address already exists');
    if (cidr(address, ctx.prefix, ctx.family) != ctx.wan_cidr) die('candidate outside WAN');
    if (ctx.family == 4 && (ipv4(address) <= ctx.plan.low || ipv4(address) >= ctx.plan.high)) die('reserved candidate');
    m.state = 'validating'; m.created = false;
    save(RUN + '/intent-' + ctx.family + '.json', { ...m, device: ctx.device, signature: ctx.signature, resource_ctx: ctx });
    let args = ['/sbin/ip', '-' + ctx.family, 'addr', 'add', address + '/' + (ctx.family == 4 ? 32 : 128), 'dev', ctx.device];
    if (ctx.family == 6 && ctx.valid_until) {
        let life = int(ctx.valid_until - clock()); if (life < 30) die('prefix expiring');
        args = [...args, 'valid_lft', '' + life, 'preferred_lft', '0'];
    }
    if (ctx.family == 6 && !ctx.valid_until) args = [...args, 'preferred_lft', '0'];
    execute([...args, 'proto', '186']);
    m.created = true; m.bound = true;
    save(RUN + '/intent-' + ctx.family + '.json', { ...m, device: ctx.device, signature: ctx.signature, resource_ctx: ctx });
    if (ctx.family == 6) {
        let deadline = clock() + 10;
        while (clock() < deadline) {
            if (present(ctx)[address]?.ready) return;
            system(['/bin/sleep', '1']);
        }
        die('DAD failed or timed out');
    }
};
export function restrict_source(ctx, m, actual) {
    if (ctx.family != 6 || m.state == 'retiring') return;
    let life = ctx.valid_until ? int(ctx.valid_until - clock()) : 4294967295;
    if (actual?.preferred === 0 && type(actual.valid) == 'int' &&
        actual.valid >= life - 60 && actual.valid <= life + 60) return;
    if (!m.created || index(map(ctx.native, a => canonical(a.address, 6)), m.address) >= 0)
        die('IPv6 source preference ownership unavailable');
    let bus = connect('/var/run/ubus/ubus.sock'), fresh = bus?.call('network.interface.' + ctx.interface, 'status');
    if (!fresh) die('native ownership check failed');
    for (let a in fresh['ipv6-address'] || []) if (canonical(a.address, 6) == m.address) die('native mutation denied');
    if (life < 30) die('prefix expiring');
    // 扩展地址只供显式绑定及 SNAT，避免改变系统普通连接的原生源地址选择。
    execute(['/sbin/ip', '-6', 'addr', 'change', m.address + '/128', 'dev', ctx.device,
        'valid_lft', '' + life, 'preferred_lft', '0', 'proto', '186']);
};
export function remove_address(ctx, m) {
    if (!m.created) die('address ownership unproven');
    let ip = canonical(m.address, ctx.family);
    if (index(map(ctx.native, a => canonical(a.address, ctx.family)), ip) >= 0) die('native deletion denied');
    let bus = connect('/var/run/ubus/ubus.sock');
    if (!bus) die('native ownership check unavailable');
    let fresh = bus.call('network.interface.' + ctx.interface, 'status');
    if (!fresh) die('native ownership check failed');
    for (let a in fresh?.['ipv' + ctx.family + '-address'] || [])
        if (canonical(a.address, ctx.family) == ip) die('address now owned by netifd');
    execute([NET, 'retire', '' + ctx.family, ip]);
    if (present(ctx)[ip]) execute(['/sbin/ip', '-' + ctx.family, 'addr', 'del',
        ip + '/' + (ctx.family == 4 ? 32 : 128), 'dev', ctx.device]);
    m.bound = false; m.state = 'cooldown';
};
export function install(config, ctx, members, now, mapping) {
    nft(rules(config, ctx, members, now, mapping), ctx.family, 'pool');
};
export function rule_shape(family) {
    let r = capture(['/usr/sbin/nft', '-j', 'list', 'table', family == 4 ? 'ip' : 'ip6', table_name(family)]);
    if (r.code) return null;
    function normalize(v, scope) {
        if (type(v) == 'array') return map(v, a => normalize(a, scope));
        if (type(v) != 'object') return v;
        let out = {};
        if (v.name) scope = v.name;
        for (let key in sort(keys(v))) {
            if (index(['handle', 'packets', 'bytes', 'expires', 'genid'], key) >= 0) continue;
            if (key == 'timeout') {
                if (+v[key] > (scope == 'coordinator' ? 90000 : 180000)) die('lease timeout drift');
                continue;
            }
            out[key] = normalize(v[key], scope);
        }
        return out;
    }
    return sprintf('%J', normalize(json(r.text), ''));
};
export function renew(ctx, members, now) {
    let ip = ctx.family == 4 ? 'ip' : 'ip6', table = table_name(ctx.family);
    let active = filter(members, m => m.state == 'active' && m.bound && m.expires > now);
    let out = ['flush set ' + ip + ' ' + table + ' coordinator',
        'add element ' + ip + ' ' + table + ' coordinator { "' + ctx.device + '" timeout 90s }'];
    for (let i = 0; i < length(active); i++) {
        let timeout = int(active[i].expires - now); if (timeout < 1) timeout = 1;
        push(out, 'flush set ' + ip + ' ' + table + ' lease' + i,
            'add element ' + ip + ' ' + table + ' lease' + i + ' { "' + ctx.device + '" timeout ' + timeout + 's }');
    }
    nft(join('\n', out) + '\n', ctx.family, 'leases');
};
export function remove_probe(family) {
    capture(['/usr/sbin/nft', 'delete', 'table', family == 4 ? 'ip' : 'ip6', 'sysu_snat_probe_' + family]);
};

function target(url, family) {
    let m = match(url, /^https:\/\/([A-Za-z0-9.-]+)(:([0-9]{1,5}))?(\/[^[:cntrl:] ]*)?$/);
    if (!m || +m[3] > 65535 || (m[3] != null && +m[3] < 1)) die('invalid HTTPS target');
    let r = bounded(NET, [NET, 'resolve', '' + family, m[1]], 8);
    if (r.code) return null;
    return { id: m[1], host: m[1], port: m[3] || '443', address: trim(r.text), url };
}
export function targets(config, family) {
    let f = config['ipv' + family], urls = [f.probe_url, f.probe_url_secondary];
    if (f.probe_url_tertiary) push(urls, f.probe_url_tertiary);
    let out = [], seen = {};
    for (let url in urls) {
        let t = target(url, family);
        if (t && !seen[t.id]) { push(out, t); seen[t.id] = true; }
    }
    return out;
};
export function request(ctx, source, t, port) {
    let address = ctx.family == 6 ? '[' + t.address + ']' : t.address;
    let r = bounded(NET, ['/usr/bin/curl', '-' + ctx.family, '--noproxy', '*', '--proxy', '',
        '--interface', 'ifhost!' + ctx.device + '!' + source, '--resolve', t.host + ':' + t.port + ':' + address,
        '--local-port', '' + port, '--connect-timeout', '3', '--max-time', '8', '--max-filesize', '65536',
        '--silent', '--show-error', '--output', '/dev/null', '--write-out', '%{http_code} %{remote_ip}', t.url], 10);
    let result = split(trim(r.text), ' ');
    ctx.last_probe = { source, target: t.id, address: t.address, curl_code: r.code, response: substr(r.text, 0, 128) };
    if (r.code || +result[0] < 200 || +result[0] >= 300 || canonical(result[1], ctx.family) != canonical(t.address, ctx.family)) return false;
    let proof = bounded(NET, [NET, 'prove', '' + ctx.family, source, t.address, '' + port], 5);
    ctx.last_probe.proof_code = proof.code; ctx.last_probe.proof = proof.text;
    return !proof.code;
};

export function verify(config, ctx, source, references, port) {
    let list = targets(config, ctx.family), success = [], refs = [], unavailable = [], diagnostics = [];
    let unique = {};
    for (let address in [source, ctx.primary, ...references]) unique[canonical(address, ctx.family)] = true;
    let addresses = keys(unique);
    if (!length(list)) return { success: [], references: [], unavailable: [], resolved: 0, diagnostics: [] };
    nft(probe_rules(ctx.family, ctx.device, addresses, port, list), ctx.family, 'probe');
    try {
        for (let i = 0; i < length(list); i++) {
            let t = list[i], ok = request(ctx, source, t, port + i);
            push(diagnostics, ctx.last_probe);
            if (ok) { push(success, t.id); push(refs, t.id); continue; }
            let baseline = request(ctx, ctx.primary, t, port + 8 + i);
            if (!baseline && length(references)) baseline = request(ctx, references[0], t, port + 16 + i);
            if (baseline) push(refs, t.id); else push(unavailable, t.id);
        }
    } catch (e) { remove_probe(ctx.family); die(e); }
    remove_probe(ctx.family);
    return { success, references: refs, unavailable, resolved: length(list), diagnostics };
};

function argument(args, name) { let i = index(args, name); return i < 0 ? null : args[i+1]; }
export function route_exists(args) {
    if (args[0] == 'rule') {
        let rs = json_command(['/sbin/ip', '-j', '-6', 'rule', 'show', 'priority', '32000']);
        return length(filter(rs, r => +r.priority == 32000 && '' + r.table == '40960' &&
            r.iif == argument(args, 'iif') &&
            cidr(split(r.dst || '::', '/')[0], r.dstlen || +split(r.dst || '::/0', '/')[1], 6) == argument(args, 'to'))) > 0;
    }
    let table = argument(args, 'table') || 'main';
    let rs = json_command(['/sbin/ip', '-j', '-6', 'route', 'show', 'table', table]);
    let from = argument(args, 'from'), gateway = argument(args, 'via');
    return length(filter(rs, r => r.dev == argument(args, 'dev') && +r.metric == 4096 &&
        (args[2] == 'default' ? r.dst == 'default' : r.dst && cidr(split(r.dst, '/')[0], +split(r.dst, '/')[1], 6) == args[2]) &&
        (!from || r.from && cidr(split(r.from, '/')[0], +split(r.from, '/')[1], 6) ==
            cidr(split(from, '/')[0], +split(from, '/')[1], 6)) &&
        (!gateway || r.gateway && canonical(r.gateway, 6) == gateway))) > 0;
};
export function remove_route(args) {
    if (args[0] == 'route' && args[2] == 'default' && !argument(args, 'from')) die('main default deletion denied');
    if (args[0] == 'rule' && argument(args, 'priority') != '32000') die('foreign rule deletion denied');
    if (route_exists(args)) {
        let remove = [...args]; remove[1] = 'del'; execute(['/sbin/ip', '-6', ...remove]);
    }
};

export function routes(ctx, config, owned) {
    if (ctx.family != 6) return owned || [];
    let desired = [
        ['route', 'add', 'default', 'from', ctx.lan_cidr, 'via', ctx.gateway, 'dev', ctx.device, 'metric', '4096'],
        ['route', 'add', ctx.wan_cidr, 'dev', ctx.landev, 'table', '40960', 'metric', '4096'],
        ['rule', 'add', 'priority', '32000', 'iif', ctx.device, 'to', ctx.wan_cidr, 'lookup', '40960']
    ];
    if (config.main.tailscale_enabled) push(desired,
        ['route', 'add', 'default', 'from', config.main.tailscale_ipv6, 'via', ctx.gateway, 'dev', ctx.device, 'metric', '4096']);
    let before = capture(['/sbin/ip', '-6', 'route', 'show', 'table', '40960']).text;
    let rule = capture(['/sbin/ip', '-6', 'rule', 'show', 'priority', '32000']).text;
    if ((!owned || !length(owned)) && (length(trim(before)) || length(trim(rule)))) die('route ownership unavailable');
    for (let old in owned || []) if (index(map(desired, a => join(' ', a)), join(' ', old)) < 0)
        remove_route(old);
    for (let args in desired) {
        if (route_exists(args)) continue;
        execute(['/sbin/ip', '-6', ...args]);
        if (!route_exists(args)) die('route verification failed');
    }
    return desired;
};
