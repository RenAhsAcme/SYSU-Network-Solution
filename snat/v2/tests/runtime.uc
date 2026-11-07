import { canonical, cidr, candidate6 } from '../ip.uc';
import { capture, quote } from '../io.uc';
import { rules, probe_rules } from '../policy.uc';
let n = 0;
function eq(a, b) { n++; if (sprintf('%J', a) != sprintf('%J', b)) die(sprintf('runtime %d: %J != %J\n', n, a, b)); }
function fails(f) { let bad = false; try { f(); } catch (e) { bad = true; } eq(bad, true); }
eq(canonical('2001:db8::1', 6), '2001:0db8:0000:0000:0000:0000:0000:0001');
eq(cidr('fd12:3456:789a:abff::1', 60, 6), 'fd12:3456:789a:abf0:0000:0000:0000:0000/60');
eq(cidr('10.2.3.99', 22, 4), '10.2.0.0/22');
for (let ip in ['1::2::3', ':::1', ':1', '1:', '2001:xx::1', '1:2:3:4:5:6:7:8:9']) fails(() => canonical(ip, 6));
let seq = 0;
eq(candidate6('2001:db8:1:2::1', n => ++seq), '2001:0db8:0001:0002:0001:0002:0003:0004');
let dangerous = "a'; printf injected; echo '";
eq(capture(['/usr/bin/printf', '%s', dangerous]).text, dangerous);
eq(capture(['/bin/sh', '-c', 'exit 7']).code, 7);
let c = { main: { proxy_enabled: 1, proxy_gid: '65534', tailscale_enabled: 1, tailscale_device: 'tailscale0',
    tailscale_ipv4: '100.64.0.0/10', tailscale_ipv6: 'fd7a:115c:a1e0::/48' }, rules: [] };
let mapping = []; for (let i = 0; i < 1024; i++) push(mapping, 0);
for (let family in [4, 6]) {
    let f = { fallback: 'primary' };
    for (let scope in ['lan', 'proxy', 'tailscale']) for (let proto in ['tcp', 'udp', 'other'])
        f[scope+'_'+proto] = proto == 'udp' ? 'endpoint' : proto == 'tcp' ? 'random' : 'host';
    c['ipv'+family] = f;
    let ctx = { family, device: 'wan-test', landev: 'lan-test',
        primary: family == 4 ? '172.25.192.65' : '2001:db8:1:2::1',
        wan_cidr: family == 4 ? '172.25.192.0/22' : '2001:db8:1:2::/64',
        lan_cidr: family == 4 ? '192.168.1.0/24' : 'fd12:3456::/60' };
    let members = [{ address: family == 4 ? '172.25.195.241' : '2001:db8:1:2::3', bound: true, state: 'active', expires: 180 }];
    let text = rules(c, ctx, members, 0, mapping);
    eq(index(text, 'timeout 90s') >= 0, true);
    eq(index(text, 'counter return') >= 0, true);
    eq(index(text, 'meta skgid 65534') >= 0, true);
    eq(index(text, 'flush ruleset') < 0, true);
    eq(index(text, 'goto slot0') >= 0, true);
    eq(index(text, 'jhash') >= 0, true);
    eq(index(probe_rules(family, ctx.device, [members[0].address]), 'priority 98') >= 0, true);
}
printf('runtime: %d checks passed\n', n);
