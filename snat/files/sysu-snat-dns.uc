#!/usr/bin/ucode
import { readfile, writefile } from 'fs';
import { cursor } from 'uci';
import { connect } from 'ubus';

let base = getenv('SNAT_DNS_BASE') || '';
let action = ARGV[0];
if (action != 'sync' && action != 'release') die('无效的 DNS 管理动作\n');
let config = json(readfile(base + '/var/run/sysu-snat/config/policy.json') || '{}');
let desired = [], owner = 'sysu_snat_notinterface';
let bus = base ? null : connect();
let network = base ? json(readfile(base + '/network.json') || '{}') : {};
function add(v) {
    if (type(v) != 'string' || !match(v, /^[A-Za-z0-9_.:-]{1,32}$/)) die('DNS 排除接口无效\n');
    if (index(desired, v) < 0) push(desired, v);
}
let active = action == 'sync' && config.main?.enabled && config.main?.dnsmasq_protect != 0;
if (active) {
    function status(name) { return base ? network[name] || {} : bus.call('network.interface.' + name, 'status') || {}; }
    for (let family in ['ipv4', 'ipv6']) {
        let f = config[family];
        if (!f?.enabled) continue;
        let wan = status(f.wan), lan = status(f.lan);
        if (wan.l3_device && wan.l3_device == lan.l3_device) die('WAN 与 LAN 共用设备，无法单独限制 WAN DNS 监听\n');
        add(f.wan);
        if (wan.l3_device) add(wan.l3_device);
    }
    // 接口切换时，旧地址释放前仍保留其设备的监听限制。
    for (let family in ['ipv4', 'ipv6'])
        for (let line in split(readfile(base + '/var/run/sysu-snat/' + family + '/owned') || '', '\n')) {
            let pair = match(line, /^(\S+)\s+\S+$/);
            if (pair) add(pair[1]);
        }
}
let c = base ? cursor(base + '/etc/config', base + '/uci', '') : cursor();
if (!c.load('dhcp')) {
    if (active) die('无法读取 DHCP/DNS 配置，未继续构建地址池\n');
    exit(0);
}
if (length(c.changes('dhcp'))) die('DHCP/DNS 有未应用的修改，暂缓调整监听范围\n');
function array(v) { return type(v) == 'array' ? v : v == null ? [] : [v]; }
function equal(a, b) { return sprintf('%J', a) == sprintf('%J', b); }
let changed = false, listen_changed = false;
c.foreach('dhcp', 'dnsmasq', function(s) {
    let old = array(s.notinterface), managed = array(s[owner]);
    let next = [], owned = [];
    for (let entry in old)
        if (index(managed, entry) < 0 || index(desired, entry) >= 0) push(next, entry);
    for (let entry in desired) {
        if (index(next, entry) < 0) { push(next, entry); push(owned, entry); }
        else if (index(managed, entry) >= 0) push(owned, entry);
    }
    if (!equal(old, next)) {
        if (length(next)) c.set('dhcp', s['.name'], 'notinterface', next);
        else c.delete('dhcp', s['.name'], 'notinterface');
        changed = true;
        listen_changed = true;
    }
    if (!equal(managed, owned)) {
        if (length(owned)) c.set('dhcp', s['.name'], owner, owned);
        else c.delete('dhcp', s['.name'], owner);
        changed = true;
    }
});
if (!changed) exit(0);
let path = base + '/etc/config/dhcp', before = readfile(path);
if (!c.commit('dhcp')) die('DNS 监听配置保存失败\n');
let committed = readfile(path);
let service = base ? {} : bus.call('service', 'list', { name: 'dnsmasq' }) || {};
let running = base ? network.dnsmasq_running != false : false;
for (let key, instance in service.dnsmasq?.instances || {})
    if (instance.running) running = true;
if (listen_changed && running) {
    let rc = system([base ? base + '/mock-bin/dnsmasq' : '/etc/init.d/dnsmasq', 'reload']);
    if (rc != 0) {
        if (readfile(path) == committed && before != null) {
            writefile(path, before);
            system([base ? base + '/mock-bin/dnsmasq' : '/etc/init.d/dnsmasq', 'reload']);
        }
        die('DNS 监听配置重载失败，未继续构建地址池\n');
    }
    print(action == 'release' || !active ? '已撤回插件添加的 DNS 监听限制\n' : '已限制 dnsmasq 监听地址池 WAN\n');
}
