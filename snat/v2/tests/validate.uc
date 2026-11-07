import { validate, is_native } from '../validate.uc';
import { network } from '../policy.uc';
let checks = 0;
function check(ok) { checks++; if (!ok) die('validation check ' + checks); }
function reject(fn) { let failed = false; try { fn(); } catch (e) { failed = true; } check(failed); }
function fixture() {
    let c = { main: { enabled: 1, proxy_enabled: 1, proxy_gid: '65534', tailscale_enabled: 1,
        tailscale_device: 'tailscale0', tailscale_ipv4: '100.64.0.0/10', tailscale_ipv6: 'fd7a:115c:a1e0::/48' }, rules: [] };
    for (let family in [4, 6]) {
        let f = { primary: '', fallback: 'primary', probe_url: 'https://one.example/', probe_url_secondary: 'https://two.example/' };
        for (let scope in ['lan', 'proxy', 'tailscale']) for (let proto in ['tcp', 'udp', 'other'])
            f[scope + '_' + proto] = proto == 'udp' ? 'endpoint' : 'host';
        c['ipv' + family] = f;
    }
    return c;
}
check(!!validate(fixture()));
for (let input in ['10.0.0.1/', '10.0.0.1/24/2', '10.0.0.1/33', '10.0.0.1/2.5', '10.0.0.999', '10.0.0.1;reboot'])
    reject(() => network(input, 4));
for (let input in ['2001:db8::/129', '2001::bad::1', '2001:db8::/64/2']) reject(() => network(input, 6));
check(network('2001:db8:1::1/64', 6) == '2001:0db8:0001:0000:0000:0000:0000:0000/64');
for (let family in [4, 6]) {
    for (let url in ['http://two.example/', 'https://two.example:0/', 'https://two.example:65536/', 'https://two.example/a\n']) {
        let c = fixture(); c['ipv' + family].probe_url_secondary = url; reject(() => validate(c));
    }
    let c = fixture(); c['ipv' + family].probe_url_secondary = 'https://ONE.example/another'; reject(() => validate(c));
    c = fixture(); c['ipv' + family].primary = family == 4 ? '10.0.0.999' : '2001::bad::1'; reject(() => validate(c));
    c = fixture(); c.rules = [{ family: '' + family, scope: 'proxy', proto: 'tcp', src: '', dest: '', dport: '65536', mode: 'host' }];
    reject(() => validate(c));
    c.rules[0].dport = '443'; c.rules[0].mode = 'endpoint'; reject(() => validate(c));
    c.rules[0].proto = 'udp'; check(!!validate(c));
}
check(is_native('172.25.192.65', [{ address: '172.25.192.65' }], 4));
check(!is_native('172.25.195.1', [{ address: '172.25.192.65' }], 4));
check(is_native('2001:db8::1', [{ address: '2001:0db8:0:0:0:0:0:1', preferred: 120 }], 6));
check(!is_native('2001:db8::1', [{ address: '2001:db8::1', preferred: 0 }], 6));
check(!is_native('2001:db8::2', [{ address: '2001:db8::1', preferred: 120 }], 6));
let inactive = fixture(); inactive.main.proxy_enabled = 0;
inactive.rules = [{ family: '4', scope: 'proxy', proto: 'tcp', src: '10.0.0.999', mode: 'host' }];
reject(() => validate(inactive));
printf('configuration validation: %d checks passed\n', checks);
