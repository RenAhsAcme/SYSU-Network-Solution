#!/usr/bin/ucode
import { readfile, chmod } from 'fs';
import { cursor } from 'uci';

if (length(ARGV)) die('用法：sysu-auth-import（从 /etc/minieap.conf 导入，保持禁用）\n');
let text = readfile('/etc/minieap.conf');
if (text == null || length(text) > 8192) die('旧配置不存在或过大\n');
let old = {};
for (let line in split(text, '\n')) {
    if (!length(line) || substr(line, 0, 1) == '#') continue;
    let equal = index(line, '=');
    if (equal < 1) die('旧配置格式不支持，请手动填写\n');
    let key = trim(substr(line, 0, equal));
    if (key in old) die('旧配置含重复项，请手动填写\n');
    old[key] = substr(line, equal + 1);
}
for (let key, expected in {
    'module': 'rjv3', 'if-impl': 'sockraw', 'auth-round': '1',
    'dhcp-type': '2', 'eap-bcast-addr': '0', 'service': 'internet',
    'version-str': 'RG-SU For Linux V1.0'
}) {
    if (old[key] != null && old[key] != expected) die('旧配置与内置中大协议不一致，请手动核对\n');
}
for (let key in ['script', 'dhcp-script', 'rj-option', 'fake-dns1', 'fake-dns2', 'fake-serial', 'proxy-lan-iface'])
    if (length(old[key] || '')) die('旧配置包含未迁移的自定义行为，请手动核对\n');
for (let key in ['username', 'password'])
    if (!length(old[key] || '') || length(old[key]) > 64 || match(old[key], /[[:cntrl:]]/))
        die('旧凭据长度或格式不支持\n');
if (!match(old.nic || '', /^[A-Za-z0-9_.-]{1,15}$/)) die('旧设备名称无效\n');
let config = cursor();
if (!config.load('sysu-auth')) die('请先安装 SYSU 认证包\n');
if (length(config.get('sysu-auth', 'main', 'username') || '') || length(config.get('sysu-auth', 'main', 'password') || ''))
    die('新配置已有凭据，拒绝覆盖\n');
config.set('sysu-auth', 'main', 'enabled', '0');
config.set('sysu-auth', 'main', 'device', old.nic);
config.set('sysu-auth', 'main', 'username', old.username);
config.set('sysu-auth', 'main', 'password', old.password);
config.set('sysu-auth', 'main', 'heartbeat', '0');
for (let key, rule in {
    'stage-timeout': ['stage_timeout', 1, 60],
    'wait-after-fail': ['retry_delay', 5, 300], 'max-retries': ['max_retries', 1, 10]
}) {
    if (old[key] == null) continue;
    if (!match(old[key], /^[0-9]+$/) || +old[key] < rule[1] || +old[key] > rule[2])
        die('旧数值参数超出支持范围，未提交配置\n');
    config.set('sysu-auth', 'main', rule[0], old[key]);
}
if (!config.commit('sysu-auth') || !chmod('/etc/config/sysu-auth', 0600)) die('配置写入失败\n');
print('已导入凭据及支持的参数。新服务保持禁用，旧版认证未受影响。\n');
