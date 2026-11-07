#!/usr/bin/ucode
import * as fs from 'fs';
import * as loop from 'uloop';
import * as core from './core.uc';
import * as net from './backend.uc';
import { candidate6 } from './ip.uc';
import { random_source } from './random.uc';
import { clock, save, load, capture, quote, bounded, json_command } from './io.uc';
import { table_name } from './policy.uc';

let random = random_source(), state, config, stopping = false, reload = true;
let jobs = {}, listeners = {}, samplers = {}, port_sequence = 0, sequence = 0;
let dns_job, next_dns = 0;
let shared = core.bucket(clock(), core.LIMITS.wan_rate), tokens = {}, conflict_limits = {};
fs.mkdir(net.RUN, 0700); fs.chmod(net.RUN, 0700);
state = load(net.RUN + '/state.json', { schema: 2, generation: 1, families: {}, events: [] });
let boot_id = trim(fs.readfile('/proc/sys/kernel/random/boot_id'));
if (state.boot_id && state.boot_id != boot_id) die('state belongs to another boot');
state.boot_id = boot_id;
state.stopped = false;
if (state.schema != 2) die('incompatible state\n');
port_sequence = ((state.port_sequence == null ? random.below(100) : state.port_sequence) + 8) % 100;
if (state.limits) {
    shared = state.limits.shared; tokens = state.limits.tokens; conflict_limits = state.limits.conflicts;
}
state.limits = { shared, tokens, conflicts: conflict_limits };
for (let family in [4, 6]) {
    let f = state.families['ipv' + family];
    if (!f) f = state.families['ipv' + family] = { family, members: [], epoch: 1, native_ok: false,
        retry: core.episode(clock()), next_native: 0, next_context: 0, next_rules: 0, cooldown: {}, routes: [] };
    f.busy = false; f.next_context = 0; f.next_rules = 0; f.rule_shape = null; f.identity_rules = null;
    if (!tokens[family]) tokens[family] = core.bucket(clock(), core.LIMITS.family_rate);
    for (let m in f.members) { m.next_check = 0; m.recoveries = 0; }
    {
        let intent = load(net.RUN + '/intent-' + family + '.json', null);
        if (intent && !filter(f.members, m => m.id == intent.id)[0]) {
            let actual = net.present(intent.resource_ctx)[intent.address];
            if (actual) {
                if (intent.created || '' + actual.protocol == '186' || actual.protocol == '0xba') {
                    intent.created = true; intent.bound = true; intent.state = 'retiring'; push(f.members, intent);
                } else f.ownership_fault = 'unproven candidate ownership: ' + intent.address;
            }
        }
    }
}
function event(f, kind, detail) {
    let e = { time: time(), family: f?.family || 0, kind, detail: substr('' + detail, 0, 512) };
    push(state.events, e); while (length(state.events) > 512) shift(state.events);
    capture(['/usr/bin/logger', '-t', 'sysu-snat', sprintf('IPv%d %s: %s', e.family, kind, e.detail)], 1024);
}
function persist() { state.heartbeat = clock(); save(net.RUN + '/state.json', state); }
function is_busy(f) { return jobs[f.family] && !jobs[f.family].finished(); }
function cancel(task) {
    // uloop task 继承父进程的信号处理；阻塞读时 TERM 不会执行事件循环。
    if (task && !task.finished()) system(['/bin/kill', '-KILL', '' + task.pid()]);
}
function count(f) { return core.counts(f.members, clock()); }
function port(family) {
    port_sequence = (port_sequence + 1) % 100; state.port_sequence = port_sequence;
    return 43000 + family * 1000 + port_sequence * 24;
}

function publish() {
    let out = { enabled: config?.main.enabled == 1, stopped: state.stopped, now: time(), pending: false, version: '2.0.0',
        heartbeat: clock(), families: {}, events: state.events, error: state.error || '', dns_error: state.dns_error || '' };
    for (let key in state.families) {
        let f = state.families[key], c = count(f), setting = config?.[key] || {};
        let enabled = config?.main.enabled && setting.enabled;
        let label = state.stopped || !enabled ? 'stopped' : stopping ? 'stopping' : f.error ? 'fault' : !f.ctx ? 'waiting' :
            c.qualified == 5 ? 'active' : c.working ? 'degraded' : f.busy ? 'building' : 'fallback';
        out.families[key] = { enabled: !!enabled, running: true, state: label,
            interface: setting.wan, device: f.ctx?.device, native: f.ctx?.native || [],
            pool: map(filter(f.members, m => m.bound), m => ({ address: m.address, bound: m.bound,
                usable: core.selectable(m, clock()), qualified: core.qualified(m, clock()), state: m.state,
                phase: m.phase, reason: m.reason || '', verified: m.verified, expires_in: int(m.expires - clock()),
                evidence: m.last_result, next_check_in: int(m.next_check - clock()) })),
            count: c.bound, usable: c.working, qualified: c.qualified, target: 5,
            native_ok: f.native_ok, baseline_targets: f.baseline_targets || [], rules: f.rules_ok || false, fallback: setting.fallback,
            verified: f.verified, other: [], phase: f.phase || '',
            detail: f.error || f.detail || '', retry_at: f.retry?.next > clock() ? time() + int(f.retry.next-clock()) : null };
    }
    save(net.RUN + '/status.json', out);
}

function commit(f, force) {
    if (!f.ctx || (!force && clock() < f.next_rules && clock() < (f.next_audit || 0))) return;
    let active = filter(f.members, m => core.selectable(m, clock()));
    let identity = join(',', map(active, m => m.address));
    if (f.mapping_identity != identity) {
        if (!f.seed) f.seed = sprintf('%08x%08x', random.below(4294967296), random.below(4294967296));
        f.mapping = length(active) ?
            json(net.execute([net.NET, 'buckets', f.seed, ...map(active, m => m.address)])) : null;
        f.mapping_identity = identity;
    }
    let identity_rules = sprintf('%J', [config, f.ctx.signature, map(active, m => m.address), f.mapping]);
    let actual = net.rule_shape(f.family);
    if (actual && actual == f.rule_shape && identity_rules == f.identity_rules) {
        if (clock() >= f.next_rules) net.renew(f.ctx, f.members, clock());
        else { f.next_audit = clock() + 10; return; }
    } else net.install(config, f.ctx, f.members, clock(), f.mapping);
    f.rule_shape = net.rule_shape(f.family); f.identity_rules = identity_rules;
    f.next_audit = clock() + 10;
    f.next_rules = clock() + 30; f.rules_ok = true;
}

function start_listener(f, pending) {
    if (f.family != 4 || !f.ctx) return;
    let addresses = map(filter(f.members, m => m.bound), m => m.address);
    if (pending && index(addresses, pending) < 0) push(addresses, pending);
    if (!length(addresses)) {
        if (listeners[4]) cancel(listeners[4].task);
        delete listeners[4]; return;
    }
    let signature = f.ctx.device + ' ' + join(' ', addresses) + ' ' + join(',', map(f.members, m => m.id));
    if (listeners[4]?.signature == signature && !listeners[4].task.finished()) return;
    let previous = listeners[4], ctx = f.ctx, epoch = f.epoch;
    let task = loop.task(function(pipe) {
        let command = join(' ', map([net.ARP, 'listen', ctx.device, ...addresses], quote));
        let p = fs.popen('exec ' + command + ' 2>/dev/null');
        if (!p) return;
        for (let line = p.read('line'); line != null; line = p.read('line')) {
            if (length(line) > 512) continue;
            try { pipe.send(json(line)); } catch (e) {}
        }
        p.close();
    }, function(e) {
        if (f.epoch != epoch || e?.event != 'conflict') return;
        let m = filter(f.members, a => a.address == e.address && a.bound)[0];
        f.cooldown[e.address] = clock() + 86400;
        if (m) { m.state = 'retiring'; m.reason = 'address_conflict'; f.next_rules = 0; }
        f.pending_conflict = e.address;
        event(f, 'address_conflict', e.address);
    });
    listeners[4] = { signature, task };
    if (previous) cancel(previous.task);
}

function job(f, kind, m) {
    if (is_busy(f)) return false;
    let ctx = f.ctx, epoch = f.epoch, generation = state.generation, job_config = config;
    let source = m?.address || ctx.primary, local_port = port(f.family);
    let references = map(filter(f.members, a => a.id != m?.id && core.selectable(a, clock())), a => a.address);
    f.busy = true; f.phase = kind;
    if (config.main.log_mode == 'debug') event(f, 'task_started', kind + ' ' + source);
    if (kind == 'candidate') start_listener(f, source);
    jobs[f.family] = loop.task(function() {
        let result = { kind, epoch, generation, id: m?.id, ctx, member: m };
        try {
            let fresh = net.context(job_config, ctx.family);
            if (fresh.signature != ctx.signature) die('topology changed before task');
            if (kind == 'candidate') {
                if (ctx.family == 4) {
                    let arp = bounded(net.NET, [net.ARP, 'probe', ctx.device, source], 10);
                    if (arp.code == 1) { result.conflict = true; return result; }
                    if (arp.code) die('ARP probe uncertain');
                }
                net.add_address(ctx, m);
            }
            if (kind == 'retire') {
                net.remove_address(ctx, m); result.removed = true;
            } else if (kind == 'drain') {
                let r = bounded(net.NET, [net.NET, 'count', '' + ctx.family, source], 5);
                if (!r.code && json(r.text).count == 0 || clock() >= m.drain_until) {
                    net.remove_address(ctx, m); result.removed = true;
                }
            } else {
                result.evidence = net.verify(job_config, ctx, source, references, local_port);
                if (kind == 'candidate') {
                    if (length(result.evidence.success) < 2) {
                        net.remove_address(ctx, m); result.rejected = true;
                    } else {
                        m.state = 'ready';
                        if (ctx.family == 4) {
                            let announced = bounded(net.NET, [net.ARP, 'announce', ctx.device, source], 5);
                            if (announced.code) die('address announcement failed');
                        }
                    }
                }
            }
        } catch (e) {
            result.error = '' + e;
            if (kind == 'candidate' && m.created) {
                try { net.remove_address(ctx, m); result.rejected = true; } catch (cleanup) {
                    result.cleanup_error = '' + cleanup;
                }
            }
        }
        return result;
    }, function(r) {
        if (!r || r.epoch == null) return;
        f.busy = false;
        if (r.member?.created && r.member.bound && !filter(f.members, a => a.id == r.member.id)[0]) push(f.members, r.member);
        let current = r.id ? filter(f.members, a => a.id == r.id)[0] : null;
        if (r.epoch != f.epoch || r.generation != state.generation) {
            if (r.kind == 'candidate' && current?.created && current.bound) { current.state = 'retiring'; current.resource_ctx = r.ctx; }
            event(f, 'stale_task', r.id || r.kind); persist(); return;
        }
        if (r.conflict) {
            f.cooldown[source] = clock() + 86400;
            let limit = conflict_limits[ctx.device] || { count: 0, next: 0 };
            limit.count++; if (limit.count >= 10) limit.next = clock() + 60;
            conflict_limits[ctx.device] = limit;
        }
        if (r.error) {
            f.detail = r.error;
            if (current) current.next_check = clock() + 10;
            if (r.cleanup_error) { f.error = r.cleanup_error; if (current) current.state = 'retiring'; }
            event(f, 'task_error', r.error);
        }
        if (r.removed && current) { current.bound = false; current.state = 'cooldown'; }
        if (r.kind == 'native') {
            f.native_ok = length(r.evidence?.success || []) > 0;
            f.next_native = clock() + (f.native_ok ? config.main.check_interval : 10);
            f.baseline_targets = r.evidence?.success || [];
            if (f.native_ok) f.retry.next = clock();
        }
        if (r.kind == 'health' && current && r.evidence) {
            current.last_result = r.evidence;
            current.last_checked = time();
            let ev = { ...r.evidence, kind: 'probe', id: current.id, epoch: current.epoch, generation: current.generation };
            let decision = core.health(current, ev, clock());
            current.next_check = clock() + (decision == 'healthy' || decision == 'improve' ? config.main.check_interval : 10);
            if (length(ev.success)) { current.verified = time(); f.verified = time(); }
            if (decision == 'restore') {
                if (core.admission(current, f.members, clock())) f.retry.next = clock();
                else if (!count(f).draining) { current.state = 'draining'; current.drain_until = clock() + 300; }
                else { current.state = 'held'; current.phase = 'waiting_to_drain'; }
                f.next_rules = 0;
            }
            if (decision == 'retire' || decision == 'refill') { f.retry.next = clock(); f.next_rules = 0; }
            if (decision != 'healthy') event(f, decision, current.address);
        }
        if (r.kind == 'candidate') {
            if (r.rejected) {
                f.cooldown[source] = clock() + (length(r.evidence?.references || []) >= 2 ? 600 : 30);
                if (current) { current.bound = false; current.state = 'cooldown'; }
            } else if (current && r.evidence && length(r.evidence.success) >= 2 && !r.error) {
                if (f.pending_conflict == current.address || f.cooldown[current.address] > clock()) current.state = 'retiring';
                else {
                    core.health(current, { ...r.evidence, kind: 'probe', id: current.id,
                        epoch: current.epoch, generation: current.generation }, clock());
                    current.state = 'ready';
                    let c = count(f);
                    if (c.working == 5) {
                        let replaceable = filter(f.members, a => a.state == 'active' &&
                            (a.id == f.rotate_ids?.[0] || !core.qualified(a, clock())))[0];
                        if (replaceable && !c.draining) { replaceable.state = 'draining'; replaceable.drain_until = clock() + 300; }
                        if (replaceable?.id == f.rotate_ids?.[0] && replaceable?.state == 'draining') shift(f.rotate_ids);
                    }
                    if (core.admission(current, f.members, clock())) {
                        current.verified = time(); current.next_check = clock() + config.main.check_interval;
                        f.retry.failures = 0; f.retry.next = clock(); f.verified = time(); f.next_rules = 0;
                        event(f, 'admitted', current.address);
                    } else { current.state = 'retiring'; }
                }
            }
        }
        f.members = filter(f.members, a => a.bound || a.state == 'retiring');
        if (config.main.log_mode == 'debug') event(f, 'task_finished', kind + ' ' + source + ' ' + (r.error || 'done'));
        persist();
        if (r.kind == 'candidate' && !r.cleanup_error) fs.unlink(net.RUN + '/intent-' + f.family + '.json');
        publish();
    });
    return true;
}

function read_config() {
    let next = load('/var/run/sysu-snat/config/policy.json', null);
    if (!next?.main) die('runtime configuration unavailable');
    if (state.config && sprintf('%J', state.config) != sprintf('%J', next)) {
        state.generation++;
        for (let key, f in state.families) {
            let changed = false;
            for (let name in ['probe_url', 'probe_url_secondary', 'probe_url_tertiary'])
                if (state.config[key][name] != next[key][name]) changed = true;
            for (let m in f.members) {
                m.generation = state.generation; m.next_check = 0;
                if (changed) { m.qualified = false; m.successes = {}; }
            }
        }
        event(null, 'config_applied', state.generation);
    }
    config = next;
    state.config = next;
    for (let key, f in state.families) f.next_context = 0;
}

function tick_family(f) {
    let now = clock(), setting = config['ipv' + f.family];
    f.busy = is_busy(f);
    if (now >= f.next_context) {
        try {
            let ctx = net.context(config, f.family);
            if (f.ctx && f.ctx.signature != ctx.signature) {
                f.epoch++;
                for (let m in f.members) { m.state = 'retiring'; m.resource_ctx = f.ctx; }
                f.native_ok = false; f.next_native = 0; f.next_rules = 0; delete samplers[f.family];
                event(f, 'topology_changed', ctx.signature);
            }
            f.ctx = ctx; f.error = null; f.next_context = now + 10;
            if (f.family == 6) f.routes = net.routes(ctx, config, f.routes);
        } catch (e) { f.error = '' + e; f.next_context = now + 5; return; }
    }
    if (!f.ctx) return;
    if ((f.next_presence || 0) <= now) {
        let missing = net.present(f.ctx);
        for (let m in f.members) if (m.bound && !missing[m.address]?.ready && m.state != 'validating') {
            m.state = 'retiring'; m.reason = 'address_missing'; f.next_rules = 0;
        }
        if (f.family == 6) for (let m in f.members)
            if (m.bound && missing[m.address]?.ready) net.restrict_source(f.ctx, m, missing[m.address]);
        f.next_presence = now + 10;
    }
    if (!config.main.enabled || !setting.enabled || stopping) {
        for (let m in f.members) m.state = 'retiring';
        f.next_rules = 0;
    }
    for (let m in f.members) if (m.state == 'active' && m.expires <= now) { m.state = 'held'; f.next_rules = 0; }
    commit(f, false);
    if (f.busy) return;
    let retiring = filter(f.members, m => m.state == 'retiring')[0];
    if (retiring) {
        let saved = f.ctx; f.ctx = retiring.resource_ctx || saved;
        job(f, 'retire', retiring); f.ctx = saved; return;
    }
    if (!config.main.enabled || !setting.enabled || stopping) return;
    start_listener(f, null);
    let due = filter(f.members, m => m.bound && m.state != 'draining' && m.next_check <= now);
    sort(due, (a, b) => a.next_check - b.next_check);
    let urgent = filter(due, m => m.state == 'active' && m.expires > now && m.expires <= now + 30);
    if (length(due) && (count(f).qualified >= 5 || length(urgent) || (f.health_turns || 0) < 2)) {
        f.health_turns = (f.health_turns || 0) + 1;
        job(f, 'health', length(urgent) ? urgent[0] : due[0]); return;
    }
    f.health_turns = 0;
    let draining = filter(f.members, m => m.state == 'draining' && (m.next_drain || 0) <= now)[0];
    if (draining) { draining.next_drain = now + 15; job(f, 'drain', draining); return; }
    if (f.next_native <= now) { job(f, 'native', null); return; }
    if (f.ownership_fault || state.dns_error) { if (f.ownership_fault) f.error = f.ownership_fault; return; }
    let c = count(f);
    for (let address in f.cooldown) if (f.cooldown[address] <= now) delete f.cooldown[address];
    if (length(f.cooldown) >= 4096) { f.detail = 'cooldown capacity reached'; return; }
    let rotating = length(f.rotate_ids || []) > 0;
    if ((!rotating && c.qualified >= 5) || !core.can_bind(f.members, now) || (rotating && c.draining)) return;
    if (!f.native_ok && (f.next_explore || 0) > now) return;
    let decision = core.discovery(f.retry, f.members, now, !f.error);
    if (rotating && decision == 'full') decision = now < f.retry.next ? 'backoff' :
        f.retry.attempted >= 32 || now - f.retry.started >= 120 ? 'end' : 'attempt';
    if (decision == 'end') { core.next_episode(f.retry, now, c.working, false); return; }
    if (decision != 'attempt') return;
    let limiter = conflict_limits[f.ctx.device];
    if (limiter?.next > now) { f.detail = 'RFC5227 conflict rate limit'; return; }
    let permission = core.permit(tokens[f.family], shared, now);
    if (!permission.allowed) { f.detail = 'candidate rate limit: ' + int(permission.wait + 1) + 's'; return; }
    let ip;
    if (f.family == 4) {
        if (!samplers[4]) {
            let excluded = map(f.members, a => a.address);
            for (let dev in json_command(['/sbin/ip', '-j', '-4', 'addr', 'show']))
                for (let a in dev.addr_info || []) if (a.local) push(excluded, a.local);
            for (let a in split(fs.readfile('/etc/sysu-snat/ipv4-excluded') || '', '\n'))
                if (match(trim(a), /^[0-9.]+$/)) push(excluded, trim(a));
            for (let a in f.cooldown) if (f.cooldown[a] > now) push(excluded, a);
            samplers[4] = core.sampler(f.ctx.plan, excluded);
        }
        ip = core.sample(samplers[4], random.below);
        if (!ip) { delete samplers[4]; f.retry.next = now + 15; return; }
    } else {
        ip = candidate6(f.ctx.primary, random.below);
        if (!ip || filter(f.members, m => m.address == ip)[0] || f.cooldown[ip] > now) return;
    }
    if (limiter?.count >= 10) limiter.next = now + 60;
    f.retry.attempted++;
    if (!f.native_ok) f.next_explore = now + 30;
    let id = sprintf('%d-%d-%d', f.family, time(), ++sequence);
    let m = core.member(id, ip, state.generation, f.epoch); m.next_check = 0;
    job(f, 'candidate', m);
}

function tick() {
    try {
        if (reload) { read_config(); reload = false; }
        {
            let op = load(net.RUN + '/operation.json', null);
            if (op && index(['pending', 'running'], op.state) >= 0 && time() - op.created > 30 &&
                state.operation?.id != op.id && !fs.access(net.RUN + '/request')) {
                op.state = 'interrupted'; op.finished = time(); save(net.RUN + '/operation.json', op);
            }
        }
        if (!stopping && clock() >= next_dns && (!dns_job || dns_job.finished())) {
            next_dns = clock() + 60;
            dns_job = loop.task(function() {
                return bounded(net.NET, ['/usr/libexec/sysu-snat/dns', 'sync'], 30);
            }, function(r) {
                state.dns_error = !r || r.code ? trim(fs.readfile('/var/run/sysu-snat/dns-error') || 'DNS protection check failed') : '';
            });
        }
        let action = fs.readfile(net.RUN + '/request');
        let requested_members;
        if (action) {
            fs.unlink(net.RUN + '/request');
            if (substr(trim(action), 0, 1) == '{') {
                let request = json(action);
                requested_members = request.members;
                state.operation = { ...request, state: 'running', started: time(), targets: [] };
                for (let key, f in state.families) for (let m in f.members) push(state.operation.targets, m.id);
                save(net.RUN + '/operation.json', state.operation);
                action = request.action;
            }
            if (trim(action) == 'stop') {
                stopping = true;
                let op = load(net.RUN + '/operation.json', null);
                if (op?.action == 'stop') state.operation = { ...op, state: 'running', started: time() };
            }
            for (let key, f in state.families) {
                f.retry.next = clock(); f.next_native = 0;
                if (index(['check', 'rebuild', 'rotate'], trim(action)) >= 0)
                    for (let m in f.members) m.next_check = 0;
                if (trim(action) == 'rebuild' || trim(action) == 'rotate') {
                    if (!length(f.rotate_ids || [])) f.rotate_ids = map(filter(f.members, a => a.state == 'active' &&
                        (requested_members == null || index(requested_members, a.id) >= 0)), a => a.id);
                }
            }
        }
        for (let key, f in state.families) tick_family(f);
        fs.writefile(net.RUN + '/daemon.pid', split(fs.readfile('/proc/self/stat'), ' ')[0]);
        let finished = true;
        for (let key, f in state.families) if (is_busy(f) || length(filter(f.members, m => m.bound))) finished = false;
        if (state.operation?.state == 'running') {
            let op = state.operation, done = state.operation.action != 'stop' || finished;
            for (let key, f in state.families) {
                if (op.action == 'check') for (let m in f.members)
                    if (index(op.targets, m.id) >= 0 && (m.last_checked || 0) < op.started) done = false;
                if (op.action == 'refill' && config[key].enabled && count(f).qualified < 5) done = false;
                if (op.action == 'rotate' && (length(f.rotate_ids || []) || count(f).draining)) done = false;
            }
            if (done) { op.state = 'completed'; op.finished = time(); save(net.RUN + '/operation.json', op); }
        }
        if (stopping && finished) state.stopped = true;
        state.error = ''; persist(); publish();
        if (stopping && finished) {
            for (let key, f in state.families) {
                capture(['/usr/sbin/nft', 'delete', 'table', f.family == 4 ? 'ip' : 'ip6', table_name(f.family)]);
                net.remove_probe(f.family);
                for (let route in f.routes || []) net.remove_route(route);
            }
            loop.end();
        }
    } catch (e) { state.error = '' + e; persist(); publish(); }
}

loop.init();
let signal_reload = loop.signal(10, function() { reload = true; });
// 进程退出保留数据平面，显式 stop 才清理；避免升级或崩溃触发全池删除。
let signal_term = loop.signal(15, function() { loop.end(); });
let signal_int = loop.signal(2, function() { loop.end(); });
let timer;
timer = loop.timer(1, function() { tick(); timer.set(1000); });
loop.run();
for (let key, t in jobs) cancel(t);
cancel(dns_job);
for (let key, listener in listeners) cancel(listener.task);
fs.unlink(net.RUN + '/daemon.pid');
random.close();
