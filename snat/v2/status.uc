#!/usr/bin/ucode
import { readfile } from 'fs';
import { clock, load } from './io.uc';
let text = readfile('/var/run/sysu-snat/v2/status.json', 1048576);
let status;
try { status = json(text || '{}'); } catch (e) { status = {}; }
if (!status.families) status = { enabled: false, families: {}, error: '维护进程尚未发布状态' };
let operation = load('/var/run/sysu-snat/v2/operation.json', null);
if (operation) status.operation = operation;
if (!status.stopped && (status.heartbeat == null || clock() - status.heartbeat > 15)) {
    status.error = '维护进程离线，最近状态已过期';
    for (let key, f in status.families) { f.state = 'offline'; f.running = false; }
}
let action = ARGV[0] || 'status';
if (action == 'members') {
    let members = {};
    for (let key, f in status.families) members[key] = f.pool;
    printf('%J\n', members);
} else if (action == 'events') printf('%J\n', status.events || []);
else if (action == 'operation') printf('%J\n', status.operation || {});
else printf('%J\n', status);
