import * as core from '../core.uc';
import { random_source } from '../random.uc';

let checks = 0;
function eq(actual, expected) {
    checks++;
    if (sprintf('%J', actual) != sprintf('%J', expected))
        die(sprintf('check %d: %J != %J\n', checks, actual, expected));
}
function fails(fn) {
    let failed = false;
    try { fn(); } catch (e) { failed = true; }
    eq(failed, true);
}
let p = core.plan('172.25.192.65', 22, '172.25.192.1', '192.168.1.1', 24);
eq(core.address(p.low), '172.25.192.0');
eq(core.address(p.high), '172.25.195.255');
eq(map(p.regions, r => r.weight), [1, 2, 4, 8]);
fails(() => core.plan('203.0.113.2', 24, '203.0.113.1', '192.168.1.1', 24));
fails(() => core.plan('10.0.0.2', 24, '10.0.0.1', '10.0.0.99', 24));
fails(() => core.plan('10.0.0.2', 24, '10.0.1.1', '192.168.1.1', 24));
fails(() => core.ipv4('1.2.3.999'));
fails(() => core.ipv4('1.2.3.4;reboot'));

let region_counts = [0, 0, 0, 0];
for (let ticket = 0; ticket < 15; ticket++) {
    let draw = 0, s = core.sampler(p, []);
    let ip = core.sample(s, n => draw++ ? 0 : ticket);
    region_counts[+split(ip, '.')[2] - 192]++;
}
eq(region_counts, [1, 2, 4, 8]);
let tiny = core.plan('10.0.0.1', 30, '10.0.0.2', '192.168.1.1', 24);
eq(core.sample(core.sampler(tiny, []), n => 0), null);
let s = core.sampler(p, ['172.25.195.241']), seen = {};
for (let i = 0; i < 1019; i++) {
    let ip = core.sample(s, n => n - 1);
    eq(ip != null && !seen[ip], true);
    seen[ip] = true;
}
eq(core.sample(s, n => 0), null);
eq(seen['172.25.195.241'] == null, true);
eq(seen['172.25.192.255'], true);
eq(seen['172.25.193.0'], true);
let big = core.sampler(core.plan('10.0.0.2', 8, '10.0.0.1', '192.168.1.1', 24), []);
eq(length(big.blocked), 4);
eq(core.sample(big, n => n - 1), '10.255.255.254');

let local = core.bucket(0, core.LIMITS.family_rate), shared = core.bucket(0, core.LIMITS.wan_rate);
for (let i = 0; i < 8; i++) eq(core.permit(local, shared, 0).allowed, true);
eq(core.permit(local, shared, 0).allowed, false);
eq(core.permit(local, shared, 3.75).allowed, true);
eq(core.permit(local, shared, 3600).allowed, true);
eq(local.tokens <= 7, true);
fails(() => core.permit(local, shared, 3599));
eq(core.retry(0, false), 2); eq(core.retry(100, false), 10);
eq(core.retry(0, true), 5); eq(core.retry(100, true), 15);

function event(m, success, references) {
    return { id: m.id, generation: m.generation, epoch: m.epoch,
        kind: 'probe', success, references };
}
function active(id) {
    let m = core.member(id, '10.0.0.' + id, 1, 1);
    m.bound = true; m.state = 'ready';
    core.health(m, event(m, ['a', 'b'], ['a', 'b']), 0);
    eq(core.admission(m, [], 0), true);
    return m;
}
let m = active(3);
let failed = event(m, [], ['a', 'b']);
eq(core.health(m, failed, 1), 'recheck');
eq(core.selectable(m, 1), true);
eq(core.health(m, failed, 11), 'refill');
eq(core.selectable(m, 11), false);
eq(core.health(m, failed, 21), 'retire');
eq(m.state, 'retiring');
eq(core.health(m, event(m, ['a', 'b'], ['a', 'b']), 22), 'ignored');
let stale = active(4), old = event(stale, [], ['a', 'b']);
old.epoch = 0;
eq(core.health(stale, old, 1), 'stale');
eq(stale.failures, 0);
let degraded = event(stale, ['b'], []); degraded.unavailable = ['a'];
eq(core.health(stale, degraded, 170), 'improve');
eq(core.selectable(stale, 300), true);
eq(stale.qualified, false);
eq(core.health(stale, event(stale, [], []), 351), 'uncertain');
eq(stale.state, 'held');
eq(core.health(stale, event(stale, ['a', 'b'], []), 352), 'recheck');
eq(core.admission(stale, [], 352), false);
eq(core.health(stale, event(stale, ['a', 'b'], []), 362), 'restore');
let conflict = event(stale, [], []); conflict.kind = 'conflict';
eq(core.health(stale, conflict, 363), 'retire');
eq(core.admission(stale, [], 363), false);
let draining = active(8); draining.state = 'draining';
let clash = event(draining, [], []); clash.kind = 'conflict';
eq(core.health(draining, clash, 1), 'retire');
let alternating = active(9);
eq(core.health(alternating, event(alternating, ['a'], []), 60), 'healthy');
eq(core.health(alternating, event(alternating, ['b'], []), 120), 'healthy');
eq(core.counts([alternating], 181).qualified, 1);
eq(core.counts([alternating], 241).qualified, 0);
let partial = active(11);
eq(core.health(partial, event(partial, ['a'], ['a','b']), 1), 'healthy');
eq(core.health(partial, event(partial, ['a'], ['a','b']), 11), 'healthy');
eq(core.health(partial, event(partial, ['a'], ['a','b']), 21), 'improve');
eq(core.selectable(partial, 21), true);
eq(core.qualified(partial, 21), false);
let duplicate = core.member(10, '10.0.0.10', 1, 1); duplicate.bound = true; duplicate.state = 'ready';
eq(core.health(duplicate, event(duplicate, ['a', 'a'], []), 0), 'improve');
eq(core.admission(duplicate, [], 0), false);
eq(core.health(duplicate, event(duplicate, [], ['a', 'a']), 1), 'uncertain');
eq(duplicate.failures, 0);
fails(() => core.health(duplicate, event(duplicate, [null], []), 2));

let members = [];
for (let i = 1; i <= 5; i++) push(members, active(i));
let extra = core.member(6, '10.0.0.6', 1, 1);
extra.bound = true; extra.state = 'ready'; extra.qualified = true; extra.expires = 180;
extra.successes = { a: 0, b: 0 };
eq(core.admission(extra, members, 0), false);
let e = core.episode(0);
eq(core.discovery(e, members, 0, true), 'full');
members[0].qualified = false;
eq(core.discovery(e, members, 0, true), 'attempt');
push(members, extra);
let extra2 = core.member(7, '10.0.0.7', 1, 1); extra2.bound = true;
push(members, extra2);
eq(core.can_bind(members, 0), false);
for (let i = 0; i < 100; i++) core.next_episode(e, i * 100, 4, false);
eq(e.next - e.started, 0);
eq(e.next, 9915);
core.next_episode(e, 9916, 4, true);
eq(e.next, 9916); eq(e.failures, 0);

let random = random_source();
for (let i = 0; i < 100; i++) {
    let n = random.below(15); eq(n >= 0 && n < 15, true);
}
random.close();
printf('v2 core: %d checks passed\n', checks);
