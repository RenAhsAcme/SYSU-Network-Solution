#!/usr/bin/ucode
import { writefile, chmod } from 'fs';
import { cursor } from 'uci';

let c = cursor();
if (!c.load('sysu-snat')) die('SYSU SNAT 配置不存在\n');
function value(section, key, fallback) {
    let v = c.get('sysu-snat', section, key);
    return v == null ? fallback : v;
}
function text(v, pattern, name) {
    if (type(v) != 'string' || length(v) > 512 || !match(v, pattern)) die(name + '无效\n');
    return v;
}
function integer(v, low, high, name) {
    text(v, /^[0-9]+$/, name);
    if (+v < low || +v > high) die(name + '超出范围\n');
    return +v;
}
function flag(section, key, fallback) {
    return integer(value(section, key, fallback), 0, 1, key);
}
function mode(v, udp) {
    return text(v, udp ? /^(original|primary|random|host|endpoint)$/ : /^(original|primary|random|host)$/, '地址分配方式');
}
let config = { main: {
    enabled: flag('main', 'enabled', '0'),
    log_mode: text(value('main', 'log_mode', 'normal'), /^(normal|debug)$/, '日志模式'),
    check_interval: integer(value('main', 'check_interval', '60'), 15, 3600, '检查间隔'),
    proxy_enabled: flag('main', 'proxy_enabled', '0'),
    proxy_gid: value('main', 'proxy_gid', ''),
    tailscale_enabled: flag('main', 'tailscale_enabled', '0'),
    tailscale_device: text(value('main', 'tailscale_device', 'tailscale0'), /^[A-Za-z0-9_.:-]{1,15}$/, 'Tailscale 设备'),
    tailscale_ipv4: value('main', 'tailscale_ipv4', '100.64.0.0/10'),
    tailscale_ipv6: value('main', 'tailscale_ipv6', 'fd7a:115c:a1e0::/48')
}, rules: [] };
if (length(config.main.proxy_gid)) config.main.proxy_gid = '' + integer(config.main.proxy_gid, 1, 4294967294, '代理 GID');
if (config.main.proxy_enabled && !length(config.main.proxy_gid)) die('启用代理流量需要填写 GID\n');
for (let family in ['ipv4', 'ipv6']) {
    let f = {
        enabled: flag(family, 'enabled', '1'),
        wan: text(value(family, 'wan', family == 'ipv4' ? 'wan' : 'wan6'), /^[A-Za-z0-9_]{1,32}$/, 'WAN 接口'),
        lan: text(value(family, 'lan', 'lan'), /^[A-Za-z0-9_]{1,32}$/, 'LAN 接口'),
        primary: text(value(family, 'primary', ''), /^[0-9A-Fa-f:.]*$/, '主地址'),
        probe_url: text(value(family, 'probe_url', family == 'ipv4' ? 'https://www.baidu.com/' : 'https://mirrors.ustc.edu.cn/'), /^https:\/\/[A-Za-z0-9.-]+(:[0-9]{1,5})?(\/[^[:cntrl:] ]*)?$/, 'HTTPS 验证地址'),
        parallel: integer(value(family, 'parallel', '8'), 1, 8, '并发数'),
        retry_seconds: integer(value(family, 'retry_seconds', '300'), 30, 86400, '重试间隔'),
        pool_size: integer(value(family, 'pool_size', '254'), 1, 512, 'IPv6 候选数'),
        fallback: text(value(family, 'fallback', 'primary'), /^(primary|original)$/, '回退方式')
    };
    if (index([1, 2, 4, 8], f.parallel) < 0) die('并发数须为 1、2、4 或 8\n');
    for (let scope in ['lan', 'proxy', 'tailscale'])
        for (let proto in ['tcp', 'udp', 'other']) {
            let key = scope + '_' + proto;
            f[key] = mode(value(family, key, scope == 'tailscale' ? (proto == 'udp' ? 'endpoint' : proto == 'other' ? 'host' : 'random') : 'random'), proto == 'udp');
        }
    config[family] = f;
}
c.foreach('sysu-snat', 'rule', function(s) {
    if (length(config.rules) >= 64) die('最多支持 64 条策略规则\n');
    if (s.enabled == '0') return;
    let r = { name: substr(s.name || s['.name'], 0, 64),
        family: text(s.family || '4', /^(4|6)$/, '规则地址族'),
        scope: text(s.scope || 'lan', /^(lan|proxy|tailscale)$/, '规则来源'),
        proto: text(s.proto || 'any', /^(any|tcp|udp)$/, '规则协议'),
        src: s.src || '', dest: s.dest || '', sport: s.sport || '', dport: s.dport || '',
        mode: mode(s.mode || 'primary', s.proto == 'udp') };
    for (let key in ['src', 'dest']) text(r[key], /^[0-9A-Fa-f:.\/]*$/, '规则地址');
    for (let key in ['sport', 'dport']) text(r[key], /^([0-9]+(-[0-9]+)?)?$/, '规则端口');
    if (r.proto == 'any' && (length(r.sport) || length(r.dport))) die('端口条件需要选择 TCP 或 UDP\n');
    push(config.rules, r);
});
if (ARGV[0] == 'show') { printf('%J\n', config); exit(0); }
if (ARGV[0] != 'stage' || !match(ARGV[1] || '', /^\/var\/run\/sysu-snat\/config\.[A-Za-z0-9]+$/)) die('无效的内部调用\n');
let dir = ARGV[1];
function quote(v) { return "'" + replace('' + v, /'/g, "'\\''") + "'"; }
function save(name, data) {
    if (!writefile(dir + '/' + name, data) || !chmod(dir + '/' + name, 0600)) die('运行配置写入失败\n');
}
save('policy.json', sprintf('%J\n', config));
for (let family in ['ipv4', 'ipv6']) {
    let f = config[family], fields = { ENABLED: config.main.enabled && f.enabled ? 1 : 0,
        WAN_IF: f.wan, LAN_IF: f.lan, PRIMARY: f.primary, PROBE_URL: f.probe_url,
        PARALLEL: f.parallel, RETRY_SECONDS: f.retry_seconds, POOL_SIZE: f.pool_size,
        LOG_MODE: config.main.log_mode, CHECK_INTERVAL: config.main.check_interval,
        TAILSCALE_ENABLED: config.main.tailscale_enabled, TAILSCALE_PREFIX: config.main.tailscale_ipv6 };
    let lines = [];
    for (let key, v in fields) push(lines, key + '=' + quote(v));
    save(family + '.conf', join('\n', lines) + '\n');
}
