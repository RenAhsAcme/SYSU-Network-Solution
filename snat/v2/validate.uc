import { connect } from 'ubus';
import { canonical } from './ip.uc';
import { rules, network } from './policy.uc';

export function is_native(primary, addresses, family) {
    let ip = canonical(primary, family);
    return length(filter(addresses || [], a => canonical(a.address, family) == ip &&
        (family == 4 || a.preferred > 0))) > 0;
};

export function validate(config) {
    for (let rule in config.rules) if (index(['4', '6'], '' + rule.family) < 0) die('规则地址族无效');
    for (let family in [4, 6]) {
        let f = config['ipv' + family];
        if (f.primary) canonical(f.primary, family);
        network(config.main['tailscale_ipv' + family], family);
        let hosts = {};
        for (let url in [f.probe_url, f.probe_url_secondary, f.probe_url_tertiary]) {
            if (!url) continue;
            let target = match(url, /^https:\/\/([A-Za-z0-9.-]+)(:([0-9]{1,5}))?(\/[^[:cntrl:] ]*)?$/);
            if (!target || length(url) > 512 || target[3] != null && (+target[3] < 1 || +target[3] > 65535))
                die('HTTPS 验证地址无效');
            hosts[lc(target[1])] = true;
        }
        if (length(hosts) < 2) die('至少需要两个不同主机的 HTTPS 验证地址');
        let ctx = { family, device: 'wan-test', landev: 'lan-test',
            primary: family == 4 ? '10.0.0.2' : '2001:db8::1',
            wan_cidr: family == 4 ? '10.0.0.0/24' : '2001:db8::/64',
            lan_cidr: family == 4 ? '192.168.1.0/24' : 'fd12:3456::/60' };
        rules({ ...config, main: { ...config.main, proxy_enabled: 1,
            proxy_gid: config.main.proxy_gid || '1', tailscale_enabled: 1 } }, ctx, [], 0, null);
    }
    return config;
};

export function native_primary(config) {
    if (!config.main.enabled) return;
    let bus;
    for (let family in [4, 6]) {
        let f = config['ipv' + family];
        if (!f.enabled || !f.primary) continue;
        if (!bus) bus = connect('/var/run/ubus/ubus.sock');
        if (!bus) die('无法核对原生 WAN 地址');
        let wan = bus.call('network.interface.' + f.wan, 'status');
        if (!wan?.up) continue;
        if (!is_native(f.primary, wan['ipv' + family + '-address'], family)) die('主地址不属于当前原生 WAN');
    }
};
