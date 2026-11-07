'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../files/sysu-snat.js'), 'utf8');
const flush = () => new Promise(resolve => setImmediate(resolve));
function element(tag, attrs = {}, children = []) {
    const node = { tag, children: [], style: {}, disabled: !!attrs.disabled, hidden: 'hidden' in attrs,
        textContent: typeof children === 'string' ? children : '', value: '', listeners: {},
        addEventListener(type, fn) { this.listeners[type] = fn; },
        replaceChildren(...items) { this.children = items; }, select() {} };
    node.appendChild = item => (node.children.push(item), item);
    if (Array.isArray(children)) children.forEach(node.appendChild);
    return node;
}
class Document extends EventTarget {
    registrations = [];
    addEventListener(type, fn, options) { this.registrations.push(type); super.addEventListener(type, fn, options); }
}
async function render(readonly) {
    const calls = [], notifications = [], document = new Document(), fields = [];
    const option = (...args) => { fields.push(args); return { value() {}, depends() {} }; };
    const state = { families: {} };
    for (const family of [4, 6]) state.families['ipv' + family] = {
        state: 'active', count: 2, usable: 2, interface: 'wan', device: 'eth0', verified: 1,
        native: [{ address: family === 4 ? '10.1.2.2' : '2001:db8::1', prefix: family === 4 ? 29 : 64, primary: true, source: '系统配置' }],
        pool: [{ address: '10.1.2.3', usable: true }, { address: '10.1.2.4', usable: true }], other: []
    };
    const sandbox = { document, navigator: {}, window: {}, E: element,
        view: { extend: methods => methods },
        form: { Map: class {
            constructor() { this.readonly = readonly; }
            section() { return { tab() {}, taboption: option, option }; }
            render() { return Promise.resolve(element('form')); }
        } },
        network: { getNetworks: async () => [{ getName: () => 'wan' }] }, poll: { add() {} },
        ui: { createHandlerFn: (ctx, fn) => fn.bind(ctx), addNotification: (...args) => notifications.push(args) },
        rpc: { declare({ method }) {
            if (method === 'status') return async () => state;
            if (method === 'logs') return async () => '';
            assert.equal(method, 'control');
            return async action => { calls.push(action); return { code: 0 }; };
        } }
    };
    const page = vm.runInNewContext(`(function() {\n${source}\n})()`, sandbox, { timeout: 1000 });
    const tree = await page.render(await page.load());
    await flush();
    return { document, tree, calls, notifications, fields };
}
function descendants(node) {
    return typeof node === 'object' && node !== null ? [node, ...node.children.flatMap(descendants)] : [];
}
(async () => {
    const writable = await render(false);
    assert.ok(writable.fields.every(args => !['pool_size', 'parallel', 'retry_seconds', 'mapping_mode'].some(key => args.includes(key))));
    const text = descendants(writable.tree).map(n => n.textContent).join('\n');
    assert.match(text, /WAN 原生地址/); assert.match(text, /主地址/); assert.match(text, /2 个可用/);
    assert.match(text, /地址池运行中/);
    for (let i = 1; i <= 2; i++) {
        writable.document.dispatchEvent(new Event('uci-applied')); await flush();
        assert.deepEqual(writable.calls, Array(i).fill('apply'));
    }
    assert.deepEqual(writable.notifications, []);
    const readonly = await render(true);
    assert.deepEqual(readonly.document.registrations, []);
    readonly.document.dispatchEvent(new Event('uci-applied')); await flush();
    assert.deepEqual(readonly.calls, []);
    const controls = descendants(readonly.tree).filter(n => ['启动', '停止', '检查', '补齐', '逐个轮换'].includes(n.textContent));
    assert.equal(controls.length, 5); assert.ok(controls.every(n => n.disabled));
    console.log('LuCI: address roles/status/repeated apply/read-only controls passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
