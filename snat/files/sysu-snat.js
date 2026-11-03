'use strict';
'require view';
'require form';
'require rpc';
'require poll';
'require ui';
'require network';

var callStatus = rpc.declare({ object: 'sysu-snat', method: 'status', expect: { '': {} } });
var callLogs = rpc.declare({ object: 'sysu-snat', method: 'logs', expect: { text: '' } });
var callControl = rpc.declare({ object: 'sysu-snat', method: 'control', params: ['action'], expect: { '': {} } });
var labels = { stopped: '已停止', starting: '正在启动', waiting: '等待网络接口',
    building: '正在构建地址池', active: '地址池运行中', fallback: '使用回退策略', retrying: '等待重试' };
var modes = [['original', '保留原出口'], ['primary', '使用 WAN 主地址'],
    ['random', '地址池 · 按连接随机'], ['host', '地址池 · 按来源地址保持一致'],
    ['endpoint', '地址池 · 按 UDP 源端点保持一致']];
function list(v) { return Array.isArray(v) ? v : []; }
function choices(o, udp) {
    modes.forEach(function(v) { if (udp || v[0] != 'endpoint') o.value(v[0], v[1]); });
    o.rmempty = false;
    return o;
}
function timestamp(v) { return v ? new Date(v * 1000).toLocaleString() : '尚未验证'; }

return view.extend({
    load: function() { return Promise.all([network.getNetworks(), callStatus()]); },
    render: function(data) {
        var m = new form.Map('sysu-snat');
        var main = m.section(form.NamedSection, 'main', 'service', '服务设置');
        main.tab('basic', '基本设置');
        main.tab('advanced', '集成与日志');
        var o = main.taboption('basic', form.Flag, 'enabled', '启用');
        o.rmempty = false;
        o.description = '启用并保存应用后立即运行，同时启用开机启动；禁用并应用后清理插件持有的地址、规则与路由。';
        o = main.taboption('advanced', form.Flag, 'proxy_enabled', '接管代理进程组流量');
        o.rmempty = false;
        o = main.taboption('advanced', form.Value, 'proxy_gid', '代理进程组 GID');
        o.datatype = 'range(1,4294967294)';
        o.depends('proxy_enabled', '1');
        o.rmempty = false;
        o.description = '匹配该进程组的全部本机出站流量。代理重新建立连接后，通常无法再按原客户端设备区分。';
        o = main.taboption('advanced', form.Flag, 'tailscale_enabled', '接管 Tailscale 转发流量');
        o.rmempty = false;
        [['tailscale_device', 'Tailscale 设备', 'tailscale0'],
         ['tailscale_ipv4', 'Tailscale IPv4 来源范围', '100.64.0.0/10'],
         ['tailscale_ipv6', 'Tailscale IPv6 来源范围', 'fd7a:115c:a1e0::/48']].forEach(function(field) {
            var opt = main.taboption('advanced', form.Value, field[0], field[1]);
            opt.default = field[2]; opt.rmempty = false; opt.depends('tailscale_enabled', '1');
            opt.datatype = field[0] == 'tailscale_device' ? 'maxlength(15)' : field[0] == 'tailscale_ipv4' ? 'ip4addr' : 'ip6addr';
        });
        o = main.taboption('advanced', form.ListValue, 'log_mode', '日志模式');
        o.value('normal', '常规'); o.value('debug', '调试'); o.rmempty = false;
        o.description = '常规模式仅记录构建摘要、状态变化与故障；调试模式额外记录批次进度。';
        o = main.taboption('advanced', form.Value, 'check_interval', '健康检查间隔（秒）');
        o.datatype = 'range(15,3600)'; o.default = '60'; o.rmempty = false;

        var boxes = {};
        [4, 6].forEach(function(family) {
            var key = 'ipv' + family;
            var s = m.section(form.NamedSection, key, 'family', 'IPv' + family);
            s.tab('basic', '接口与地址'); s.tab('policy', '出口策略'); s.tab('advanced', '构建参数');
            var opt = s.taboption('basic', form.Flag, 'enabled', '启用 IPv' + family);
            opt.rmempty = false;
            ['wan', 'lan'].forEach(function(name) {
                var field = s.taboption('basic', form.ListValue, name, name.toUpperCase() + ' 逻辑接口');
                data[0].forEach(function(net) { field.value(net.getName(), net.getName()); });
                field.rmempty = false;
            });
            opt = s.taboption('basic', form.Value, 'primary', 'WAN 主地址');
            opt.placeholder = '自动选择原生地址'; opt.rmempty = true;
            opt.datatype = family == 4 ? 'ip4addr("nomask")' : 'ip6addr("nomask")';
            list(((data[1].families || {})[key] || {}).native).forEach(function(a) { opt.value(a.address, a.address); });
            opt.description = '只从网络接口自身的原生地址中选择。手动选择的地址失效后将等待恢复，不会改用池地址冒充主地址。';
            opt = s.taboption('policy', form.ListValue, 'fallback', '地址池不可用时');
            opt.value('primary', '使用 WAN 主地址'); opt.value('original', '保留原出口'); opt.rmempty = false;
            [['lan', 'LAN 转发'], ['proxy', '代理进程组'], ['tailscale', 'Tailscale 转发']].forEach(function(scope) {
                [['tcp', 'TCP'], ['udp', 'UDP'], ['other', '其他协议']].forEach(function(proto) {
                    choices(s.taboption('policy', form.ListValue, scope[0] + '_' + proto[0], scope[1] + ' · ' + proto[1]), proto[0] == 'udp');
                });
            });
            s.description = '仅接管所选 WAN 的出站流量，保留对 WAN 本地网段的访问。按来源保持一致以池成员及映射参数不变为前提；代理和 Tailscale 策略需要先启用对应集成。';
            opt = s.taboption('advanced', form.Value, 'probe_url', 'HTTPS 验证地址');
            opt.rmempty = false; opt.datatype = 'maxlength(512)';
            opt.description = '使用具有有效证书的 HTTPS 地址，验证结果仅代表该目标在验证时可达。';
            opt = s.taboption('advanced', form.ListValue, 'parallel', '探测并发数');
            [1, 2, 4, 8].forEach(function(n) { opt.value(String(n)); }); opt.rmempty = false;
            opt = s.taboption('advanced', form.Value, 'retry_seconds', '失败重试间隔（秒）');
            opt.datatype = 'range(30,86400)'; opt.rmempty = false;
            if (family == 6) {
                opt = s.taboption('advanced', form.Value, 'pool_size', 'IPv6 候选数量');
                opt.datatype = 'range(1,512)'; opt.rmempty = false;
            }

            var summary = E('p', {}, '正在读取状态…');
            var native = E('div');
            var poolInfo = E('p', { 'class': 'description' });
            var poolText = E('textarea', { 'readonly': '', 'rows': '10', 'style': 'width:100%;font-family:monospace' });
            var search = E('input', { 'type': 'search', 'placeholder': '筛选池地址', 'style': 'max-width:26em' });
            var others = E('pre', { 'style': 'white-space:pre-wrap;overflow-wrap:anywhere' });
            var detail = E('p', { 'class': 'description', 'style': 'white-space:pre-wrap;overflow-wrap:anywhere' });
            var addresses = [];
            var filter = function() {
                var term = search.value || '';
                poolText.value = addresses.filter(function(a) { return a.address.indexOf(term) >= 0; }).map(function(a) {
                    return a.address + (a.usable ? '  可用' : a.bound ? '  已绑定，未激活' : '  未绑定');
                }).join('\n');
            };
            search.addEventListener('input', filter);
            var copy = E('button', { 'class': 'cbi-button', 'type': 'button', 'click': ui.createHandlerFn(this, function() {
                var text = addresses.filter(function(a) { return a.usable && a.address.indexOf(search.value || '') >= 0; }).map(function(a) { return a.address; }).join('\n');
                if (navigator.clipboard && window.isSecureContext) return navigator.clipboard.writeText(text);
                var saved = poolText.value; poolText.value = text; poolText.select();
                var ok = document.execCommand('copy'); poolText.value = saved;
                if (!ok) ui.addNotification(null, E('p', {}, '请选中地址列表后复制'), 'info');
            }) }, '复制可用地址');
            boxes[key] = { node: E('div', { 'class': 'cbi-section' }, [E('h3', {}, 'IPv' + family + ' 地址状态'), summary, native, poolInfo,
                E('details', {}, [E('summary', {}, '扩展地址池'), search, ' ', copy, poolText]),
                E('details', {}, [E('summary', {}, '其他已绑定地址'), others]), detail]),
                update: function(state) {
                    summary.textContent = (labels[state.state] || '状态未知') + (state.device ? ' · ' + state.interface + ' / ' + state.device : '') +
                        (state.state == 'building' && state.phase ? ' · ' + state.phase : '');
                    native.replaceChildren(E('strong', {}, 'WAN 原生地址'));
                    var originals = list(state.native);
                    if (!originals.length) native.appendChild(E('p', {}, '尚未获取原生地址'));
                    originals.forEach(function(a) {
                        native.appendChild(E('p', {}, a.address + '/' + a.prefix + (a.primary ? ' · 主地址' : '') + ' · ' + a.source +
                            (a.preferred === 0 ? ' · 已弃用' : '')));
                    });
                    addresses = list(state.pool); filter();
                    poolInfo.textContent = '扩展池：' + (state.usable || 0) + ' 个可用 / ' + (state.count || 0) + ' 个记录 · 最近验证：' + timestamp(state.verified);
                    others.textContent = list(state.other).map(function(a) { return a.address + '/' + a.prefix + ' · ' + a.scope; }).join('\n') || '无其他地址';
                    detail.textContent = (state.state == 'fallback' ? '地址池尚不可用，当前按回退设置处理新连接。' : '') +
                        (state.retry_at ? ' 下次尝试：' + timestamp(state.retry_at) : '') + (state.detail ? '\n' + state.detail : '');
                }
            };
        }, this);

        var rules = m.section(form.GridSection, 'rule', '例外策略', '从上到下匹配，首条命中生效；未命中时使用各来源的默认策略。代理来源中的 IP 条件匹配代理实际出站连接。');
        rules.anonymous = true; rules.addremove = true; rules.sortable = true;
        o = rules.option(form.Flag, 'enabled', '启用'); o.default = '1'; o.rmempty = false;
        o = rules.option(form.Value, 'name', '名称'); o.datatype = 'maxlength(64)';
        o = rules.option(form.ListValue, 'family', '地址族'); o.value('4', 'IPv4'); o.value('6', 'IPv6'); o.rmempty = false;
        o = rules.option(form.ListValue, 'scope', '来源');
        [['lan', 'LAN'], ['proxy', '代理进程组'], ['tailscale', 'Tailscale']].forEach(function(v) { o.value(v[0], v[1]); }); o.rmempty = false;
        o = rules.option(form.Value, 'src', '来源 IP / 网段'); o.datatype = 'ipaddr'; o.placeholder = '不限';
        o = rules.option(form.Value, 'dest', '目的 IP / 网段'); o.datatype = 'ipaddr'; o.placeholder = '不限';
        o = rules.option(form.ListValue, 'proto', '协议'); o.value('any', '全部'); o.value('tcp', 'TCP'); o.value('udp', 'UDP'); o.rmempty = false;
        ['sport', 'dport'].forEach(function(key) {
            var opt = rules.option(form.Value, key, key == 'sport' ? '来源端口' : '目的端口');
            opt.datatype = 'portrange'; opt.placeholder = '不限'; opt.depends('proto', 'tcp'); opt.depends('proto', 'udp');
        });
        o = choices(rules.option(form.ListValue, 'mode', '出口分配'), true);
        o.default = 'primary';
        o.validate = function(section, value) {
            return value != 'endpoint' || m.lookupOption('proto', section)[0].formvalue(section) == 'udp' ? true : '源端点方式仅适用于 UDP';
        };

        var logs = E('pre', { 'hidden': '', 'style': 'white-space:pre-wrap;overflow-wrap:anywhere;max-height:24em;overflow:auto' });
        var actions = E('div', { 'class': 'cbi-page-actions' });
        var controls = [];
        var error = E('p', { 'class': 'description' });
        var update = function() {
            return callStatus().then(function(state) {
                error.textContent = state.error || (state.pending ? '已保存设置与运行配置不同，请检查并保存应用。' : '');
                Object.keys(boxes).forEach(function(key) { boxes[key].update((state.families || {})[key] || {}); });
            }).catch(function() { error.textContent = '暂时无法读取状态，请检查服务和访问权限'; });
        };
        [['start', '启动'], ['stop', '停止'], ['rebuild', '重新构建']].forEach(function(action) {
            var button = E('button', { 'class': 'cbi-button cbi-button-action', 'type': 'button', 'disabled': true,
                'click': ui.createHandlerFn(this, function() {
                    return callControl(action[0]).then(function(result) {
                        if (result.code === 2) throw new Error('请先启用服务，并保存应用设置');
                        if (result.code !== 0) throw new Error('操作未完成，请检查日志与配置');
                        return update();
                    }).catch(function(err) { ui.addNotification(null, E('p', {}, err.message), 'error'); });
                }) }, action[1]);
            controls.push(button); actions.appendChild(button);
        }, this);
        actions.appendChild(E('button', { 'class': 'cbi-button', 'type': 'button', 'click': ui.createHandlerFn(this, function() {
            if (!logs.hidden) { logs.hidden = true; return; }
            return callLogs().then(function(text) { logs.textContent = text || '暂无日志'; logs.hidden = false; });
        }) }, '诊断日志'));
        return m.render().then(function(node) {
            controls.forEach(function(button) { button.disabled = !!m.readonly; });
            if (!m.readonly) document.addEventListener('uci-applied', function() {
                callControl('apply').then(function(result) {
                    if (result.code !== 0) throw new Error('配置已保存，但未能应用；当前有效配置保留，请检查日志');
                    return update();
                }).catch(function(err) { ui.addNotification(null, E('p', {}, err.message), 'error'); });
            });
            poll.add(update, 5); update();
            return E('div', {}, [E('h2', {}, 'SYSU SNAT'), E('p', { 'class': 'cbi-map-descr' },
                '管理 WAN 扩展源地址与新连接的出口分配。策略变化保留已有连接映射；重新构建或地址失效可能使连接需要重连。'),
                actions, error, boxes.ipv4.node, boxes.ipv6.node, logs, node]);
        });
    }
});
