import { rules } from '../policy.uc';
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
    print(rules(c, ctx, members, 0, mapping));
}
