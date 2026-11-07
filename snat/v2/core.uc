export const LIMITS = {
    target: 5, bound: 8, candidates: 2, episode: 32, episode_seconds: 120,
    evidence_seconds: 180, conflicts_seconds: 86400,
    family_rate: 16.0 / 60, wan_rate: 24.0 / 60, burst: 8
};

export function ipv4(s) {
    if (type(s) != 'string' || !match(s, /^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/))
        die('invalid IPv4 address');
    let parts = split(s, '.'), n = 0;
    for (let i = 0; i < 4; i++) {
        if (+parts[i] > 255 || length(parts[i]) > 3) die('invalid IPv4 octet');
        n = n * 256 + +parts[i];
    }
    return n;
};

export function address(n) {
    return sprintf('%d.%d.%d.%d', int(n / 16777216) % 256,
        int(n / 65536) % 256, int(n / 256) % 256, n % 256);
};

export function plan(wan, prefix, gateway, lan, lan_prefix) {
    if (type(prefix) != 'int' || prefix < 8 || prefix > 30 ||
        type(lan_prefix) != 'int' || lan_prefix < 0 || lan_prefix > 32)
        die('unsupported prefix');
    let own = ipv4(wan), gw = ipv4(gateway), size = 1 << (32 - prefix);
    let low = int(own / size) * size, high = low + size - 1;
    let lsize = 1 << (32 - lan_prefix), lown = ipv4(lan);
    let llow = int(lown / lsize) * lsize, lhigh = llow + lsize - 1;
    function private(n) {
        return (n >= 167772160 && n <= 184549375) ||
            (n >= 2886729728 && n <= 2887778303) ||
            (n >= 3232235520 && n <= 3232301055);
    }
    if (!private(low) || !private(high) || own <= low || own >= high ||
        gw <= low || gw >= high || !(high < llow || low > lhigh))
        die('unsupported WAN topology');
    let regions = [];
    for (let i = 0; i < 4; i++)
        push(regions, { low: low + i * size / 4, high: low + (i + 1) * size / 4 - 1, weight: 1 << i });
    return { low, high, own, gw, regions };
};

export function sampler(p, excluded) {
    let blocked = {};
    blocked['' + p.low] = true; blocked['' + p.high] = true;
    blocked['' + p.own] = true; blocked['' + p.gw] = true;
    for (let i = 0; i < length(excluded); i++) blocked['' + ipv4(excluded[i])] = true;
    return { plan: p, blocked };
};

// 在剩余地址中按排名取样，避免稠密排除时不断重抽，也不展开整个 /8。
export function sample(s, random) {
    let groups = [], total = 0;
    for (let i = 0; i < 4; i++) {
        let r = s.plan.regions[i], removed = [];
        for (let key in s.blocked)
            if (+key >= r.low && +key <= r.high) push(removed, +key);
        sort(removed, (a, b) => a - b);
        let count = r.high - r.low + 1 - length(removed);
        if (count > 0) { push(groups, { region: r, removed, count }); total += r.weight; }
    }
    if (!total) return null;
    let ticket = random(total), chosen;
    for (let i = 0; i < length(groups); i++) {
        chosen = groups[i];
        if (ticket < chosen.region.weight) break;
        ticket -= chosen.region.weight;
    }
    let n = chosen.region.low + random(chosen.count);
    for (let i = 0; i < length(chosen.removed); i++) {
        if (chosen.removed[i] > n) break;
        n++;
    }
    s.blocked['' + n] = true;
    return address(n);
};

export function bucket(now, rate) {
    return { at: now, tokens: LIMITS.burst, rate };
};

function replenish(b, now) {
    if (now < b.at) die('non-monotonic clock');
    b.tokens += (now - b.at) * b.rate;
    if (b.tokens > LIMITS.burst) b.tokens = LIMITS.burst;
    b.at = now;
};

export function permit(local, shared, now) {
    replenish(local, now); replenish(shared, now);
    let wait = 0;
    for (let b in [local, shared]) {
        let delay = (1 - b.tokens) / b.rate;
        if (delay > wait) wait = delay;
    }
    if (wait > 0.000001) return { allowed: false, wait };
    local.tokens -= 1; shared.tokens -= 1;
    return { allowed: true, wait: 0 };
};

export function retry(failures, working) {
    let delays = working ? [5, 10, 15] : [2, 5, 10];
    return delays[failures < 2 ? failures : 2];
};

export function member(id, ip, generation, epoch) {
    return { id, address: ip, generation, epoch, state: 'validating',
        phase: 'healthy', qualified: false, successes: {}, failures: 0,
        recoveries: 0, expires: 0, bound: false };
};

export function current(m, result) {
    return m.id == result.id && m.generation == result.generation && m.epoch == result.epoch;
};

export function selectable(m, now) {
    return m.state == 'active' && m.bound && m.expires > now;
};

export function qualified(m, now) {
    let fresh = 0;
    for (let target in m.successes)
        if (now - m.successes[target] < LIMITS.evidence_seconds) fresh++;
    return fresh >= 2 && m.qualified;
};

export function counts(members, now) {
    let c = { bound: 0, candidates: 0, working: 0, qualified: 0, draining: 0 };
    for (let i = 0; i < length(members); i++) {
        let m = members[i];
        if (m.bound) c.bound++;
        if (m.bound && (m.state == 'validating' || m.state == 'ready')) c.candidates++;
        if (m.state == 'draining') c.draining++;
        if (selectable(m, now)) { c.working++; if (qualified(m, now)) c.qualified++; }
    }
    return c;
};

export function can_bind(members, now) {
    let c = counts(members, now);
    return c.bound < LIMITS.bound && c.candidates < LIMITS.candidates;
};

function targets(values) {
    if (type(values) != 'array' || length(values) > 3) die('invalid target evidence');
    let distinct = {};
    for (let i = 0; i < length(values); i++) {
        if (type(values[i]) != 'string' || !length(values[i]) || length(values[i]) > 128)
            die('invalid target identity');
        distinct[values[i]] = true;
    }
    return keys(distinct);
}

// 只返回决策；地址、conntrack 和 NAT 变更必须经协调器确认后提交。
export function health(m, event, now) {
    if (!current(m, event)) return 'stale';
    if (index(['retiring', 'cooldown'], m.state) >= 0) return 'ignored';
    if (index(['conflict', 'missing', 'prefix_expired'], event.kind) >= 0) {
        m.state = 'retiring'; m.qualified = false;
        return 'retire';
    }
    if (m.state == 'draining') return 'ignored';
    if (event.kind != 'probe') die('invalid health event');
    let ok = targets(event.success || []), references = targets(event.references || []);
    if (!m.target_failures) m.target_failures = {};
    for (let target in references) {
        if (index(ok, target) >= 0) m.target_failures[target] = 0;
        else {
            m.target_failures[target] = (m.target_failures[target] || 0) + 1;
            if (m.target_failures[target] >= 3) delete m.successes[target];
        }
    }
    if (length(ok)) {
        m.expires = now + LIMITS.evidence_seconds;
        for (let target in targets(event.unavailable || [])) delete m.successes[target];
        for (let i = 0; i < length(ok); i++) m.successes[ok[i]] = now;
        m.failures = 0;
        m.qualified = true;
        m.qualified = qualified(m, now);
        m.phase = m.qualified ? 'healthy' : 'limited';
        if (m.state == 'suspect' || m.state == 'held') {
            m.recoveries = m.qualified ? m.recoveries + 1 : 0;
            return m.recoveries >= 2 ? 'restore' : 'recheck';
        }
        return m.qualified ? 'healthy' : 'improve';
    }
    m.recoveries = 0;
    if (length(references) < 2) {
        if (now >= m.expires && m.state == 'active') m.state = 'held';
        return 'uncertain';
    }
    m.failures++;
    m.phase = 'observing';
    if (m.failures >= 3) { m.state = 'retiring'; m.qualified = false; return 'retire'; }
    if (m.failures >= 2) { m.state = 'suspect'; m.qualified = false; return 'refill'; }
    return 'recheck';
};

export function admission(m, members, now) {
    let c = counts(members, now);
    if (!m.bound || !qualified(m, now) || m.expires <= now ||
        index(['ready', 'suspect', 'held'], m.state) < 0 || c.working >= LIMITS.target)
        return false;
    if ((m.state == 'suspect' || m.state == 'held') && m.recoveries < 2) return false;
    m.state = 'active'; m.recoveries = 0;
    return true;
};

export function episode(now) {
    return { started: now, attempted: 0, failures: 0, next: now };
};

export function discovery(e, members, now, prerequisites) {
    let c = counts(members, now);
    if (c.qualified >= LIMITS.target) return 'full';
    if (!prerequisites) return 'blocked';
    if (now < e.next) return 'backoff';
    if (!can_bind(members, now)) return 'binding_budget';
    if (e.attempted >= LIMITS.episode || now - e.started >= LIMITS.episode_seconds) return 'end';
    return 'attempt';
};

export function next_episode(e, now, working, progress) {
    e.failures = progress ? 0 : e.failures + 1;
    e.next = progress ? now : now + retry(e.failures - 1, working);
    e.started = e.next; e.attempted = 0;
};
