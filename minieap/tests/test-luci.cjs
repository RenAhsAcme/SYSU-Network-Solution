'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../files/sysu-auth.js'), 'utf8');
const flush = () => new Promise(resolve => setImmediate(resolve));

function element(tag, attrs = {}, children = []) {
    const node = { tag, children: [], style: {}, disabled: !!attrs.disabled,
        textContent: typeof children === 'string' ? children : '' };
    node.appendChild = child => (node.children.push(child), child);
    if (Array.isArray(children)) children.forEach(node.appendChild);
    return node;
}

class Document extends EventTarget {
    registrations = [];
    addEventListener(type, callback, options) {
        this.registrations.push(type);
        super.addEventListener(type, callback, options);
    }
}

async function render(readonly) {
    const calls = [], notifications = [], document = new Document();
    const sandbox = {
        document, E: element,
        view: { extend: methods => methods },
        form: {
            Map: class {
                constructor() { this.readonly = readonly; }
                section() { return { tab() {}, taboption() { return { value() {} }; } }; }
                render() { return Promise.resolve(element('form')); }
            }
        },
        network: { getDevices: async () => [{ getName: () => 'test0' }] },
        poll: { add() {} },
        ui: {
            createHandlerFn: (ctx, callback) => callback.bind(ctx),
            addNotification: (...args) => notifications.push(args)
        },
        rpc: {
            declare({ method }) {
                if (method === 'status') return async () => ({ state: 'authenticated',
                    reason: 'accepted', maintenance: 'confirmed', device: 'test0', now: 120, since: 100 });
                if (method === 'logs') return async () => '';
                assert.equal(method, 'control');
                return async action => { calls.push(action); return { code: 0 }; };
            }
        }
    };
    const page = vm.runInNewContext(`(function() {\n${source}\n})()`, sandbox,
        { filename: 'sysu-auth.js', timeout: 1000 });
    const tree = await page.render(await page.load());
    await flush();
    return { document, tree, calls, notifications };
}

function descendants(node) {
    return [node, ...node.children.flatMap(descendants)];
}

(async () => {
    const writable = await render(false);
    assert.deepEqual(writable.document.registrations, ['uci-applied']);
    for (let i = 1; i <= 2; i++) {
        writable.document.dispatchEvent(new Event('uci-applied'));
        await flush();
        assert.deepEqual(writable.calls, Array(i).fill('apply'));
    }
    const text = descendants(writable.tree).map(node => node.textContent).join('\n');
    assert.match(text, /认证已通过/);
    assert.match(text, /会话：20 秒/);
    assert.match(text, /会话维护正常/);
    assert.deepEqual(writable.notifications, []);
    console.log('PASS: repeated configuration apply and confirmed maintenance status');

    const readonly = await render(true);
    assert.deepEqual(readonly.document.registrations, []);
    readonly.document.dispatchEvent(new Event('uci-applied'));
    readonly.document.dispatchEvent(new Event('uci-applied'));
    await flush();
    assert.deepEqual(readonly.calls, []);
    const controls = descendants(readonly.tree).filter(node =>
        ['连接', '断开', '重新认证'].includes(node.textContent));
    assert.equal(controls.length, 3);
    assert.ok(controls.every(node => node.disabled));
    assert.deepEqual(readonly.notifications, []);
    console.log('PASS: read-only page does not register or send configuration controls');
})().catch(error => { console.error(error); process.exitCode = 1; });
