#!/usr/bin/ucode
import * as fs from 'fs';
import { cursor } from 'uci';
import { load, save, capture, quote, clock } from './io.uc';
import { RUN } from './backend.uc';
let action = ARGV[0];
if (index(['start', 'apply', 'stop', 'check', 'refill', 'rotate', 'rebuild'], action) < 0) die('invalid action\n');
if (action == 'rebuild') action = 'rotate';
fs.mkdir(RUN, 0700);
let c = cursor(); c.load('sysu-snat');
if (action != 'stop' && action != 'apply' && c.get('sysu-snat', 'main', 'enabled') != '1') {
    printf('%J\n', { code: 2 }); exit(0);
}
let previous = load(RUN + '/operation.json', null);
let live = load(RUN + '/status.json', {});
let op = live.operation?.id == previous?.id ? live.operation : previous;
if (op && index(['pending', 'running'], op.state) >= 0 && clock() - (live.heartbeat || 0) < 15 && action != 'stop') {
    printf('%J\n', { code: op.action == action ? 0 : 3, operation_id: op.id }); exit(0);
}
let id = trim(fs.readfile('/proc/sys/kernel/random/uuid') || '');
if (!match(id, /^[a-f0-9-]{36}$/)) die('operation identity unavailable');
let request = { id, action, state: 'pending', created: time() };
save(RUN + '/operation.json', request);
if (index(['check', 'refill', 'rotate'], action) >= 0) save(RUN + '/request', request);
else {
    let command = '(trap "" HUP; exec /usr/bin/ucode /usr/libexec/sysu-snat/v2/action.uc ' + quote(action) + ' ' + quote(id) + ') >/dev/null 2>&1 </dev/null &';
    if (capture(['/bin/sh', '-c', command]).code) die('action dispatch failed');
}
printf('%J\n', { code: 0, operation_id: id, accepted: true });
