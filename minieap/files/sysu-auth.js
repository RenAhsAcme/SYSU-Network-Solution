'use strict';
'require view';
'require form';
'require rpc';
'require poll';
'require ui';
'require network';

var callStatus = rpc.declare({ object: 'sysu-auth', method: 'status', expect: { '': {} } });
var callLogs = rpc.declare({ object: 'sysu-auth', method: 'logs', expect: { text: '' } });
var callControl = rpc.declare({ object: 'sysu-auth', method: 'control', params: ['action'], expect: { '': {} } });
var states = {
    stopped: '已停止', starting: '正在启动', waiting: '等待链路',
    authenticating: '正在认证', authenticated: '认证已通过', retrying: '等待重试', error: '运行异常'
};
var reasons = {
    stopped: '服务尚未运行', starting: '正在准备认证', link_down: '请检查网线与认证设备',
    discovering: '正在查找认证服务器', identity: '正在回应身份请求', challenge: '正在回应认证挑战',
    accepted: '服务端已接受认证',
    link_ready: '链路已恢复，正在准备认证', server_reauth: '服务端要求重新认证',
    round_timeout: '本轮认证超过总时限',
    timeout: '服务端响应超时', rejected: '服务端拒绝认证，请检查账号、密码或接入状态',
    server_logoff: '服务端结束了当前会话', send_failed: '报文发送失败', heartbeat_send_failed: '心跳报文发送失败',
    maintenance_timeout: '会话维护连续未获回应，正在等待重新认证'
};

return view.extend({
    load: function() { return network.getDevices(); },
    render: function(devices) {
        var m = new form.Map('sysu-auth');
        var s = m.section(form.NamedSection, 'main', 'authentication');
        s.tab('basic', '基本设置');
        s.tab('advanced', '高级设置');
        var o = s.taboption('basic', form.Flag, 'enabled', '启用');
        o.rmempty = false;
        o = s.taboption('basic', form.ListValue, 'device', '认证设备');
        devices.forEach(function(dev) {
            if (dev.getName() != 'lo') o.value(dev.getName(), dev.getName());
        });
        o.rmempty = false;
        o = s.taboption('basic', form.Value, 'username', '账号');
        o.rmempty = false;
        o.datatype = 'and(minlength(1),maxlength(64))';
        o = s.taboption('basic', form.Value, 'password', '密码');
        o.password = true;
        o.rmempty = false;
        o.datatype = 'and(minlength(1),maxlength(64))';
        o = s.taboption('advanced', form.Value, 'heartbeat', '旧版 NAS 心跳间隔（秒）');
        o.default = '0';
        o.datatype = 'range(0,3600)';
        o.rmempty = false;
        o.description = '0 为自动。仅旧版 NAS 需要手动覆盖间隔，标准会话按服务器参数自动维护。';
        o = s.taboption('advanced', form.ListValue, 'log_mode', '日志模式');
        o.value('normal', '常规');
        o.value('debug', '调试');
        o.default = 'normal';
        o.rmempty = false;
        o.description = '调试模式记录协议阶段、重试与会话维护过程，不记录密码、密钥或报文内容。排查完成后建议恢复常规模式。';
        [['stage_timeout', '阶段超时（秒）', '5', 'range(1,60)'],
         ['retry_delay', '重试起始间隔（秒）', '30', 'range(5,300)'],
         ['max_retries', '阶段重发次数', '3', 'range(1,10)']].forEach(function(field) {
            var opt = s.taboption('advanced', form.Value, field[0], field[1]);
            opt.default = field[2];
            opt.datatype = field[3];
            opt.rmempty = false;
        });
        var status = E('p', {}, '正在读取状态…');
        var details = E('p', { 'class': 'description' });
        var timing = E('p', { 'class': 'description' });
        var logs = E('pre', { 'style': 'display:none;white-space:pre-wrap;overflow-wrap:anywhere;max-height:22em;overflow:auto', 'hidden': '' });
        var update = function() {
            return callStatus().then(function(data) {
                status.textContent = states[data.state] || '状态未知';
                details.textContent = reasons[data.reason] || '请查看诊断日志';
                var info = [];
                if (data.device) info.push('设备：' + data.device);
                if (data.state == 'authenticated') {
                    info.push('会话：' + Math.max(0, data.now - data.since) + ' 秒');
                    var maintenance = { none: '未收到维护参数', pending: '正在确认会话', confirmed: '会话维护正常', backoff: '维护暂未回应，等待重试' };
                    if (maintenance[data.maintenance]) info.push(maintenance[data.maintenance]);
                }
                if (data.retry_at) info.push('下次重试：' + Math.max(0, data.retry_at - data.now) + ' 秒后');
                timing.textContent = info.join(' · ');
            }).catch(function() { status.textContent = '暂时无法读取状态'; details.textContent = '请检查服务与访问权限'; });
        };
        var actions = E('div', { 'class': 'cbi-page-actions' });
        var controls = [];
        [['connect', '连接'], ['disconnect', '断开'], ['reconnect', '重新认证']].forEach(function(action) {
            var button = E('button', {
                'class': 'cbi-button cbi-button-action', 'type': 'button',
                'disabled': true,
                'click': ui.createHandlerFn(this, function() {
                    return callControl(action[0]).then(function(result) {
                        if (result.code === 2) throw new Error('请先启用认证，并保存应用设置');
                        if (result.code !== 0) throw new Error('操作未完成，请检查配置及旧版 MiniEAP 是否仍在运行');
                        return update();
                    }).catch(function(err) { ui.addNotification(null, E('p', {}, err.message), 'error'); });
                })
            }, action[1]);
            controls.push(button);
            actions.appendChild(button);
        }, this);
        actions.appendChild(E('button', {
            'class': 'cbi-button', 'type': 'button',
            'click': ui.createHandlerFn(this, function() {
                if (!logs.hidden) { logs.hidden = true; logs.style.display = 'none'; return; }
                return callLogs().then(function(text) { logs.hidden = false; logs.style.display = ''; logs.textContent = text || '暂无日志'; });
            })
        }, '诊断日志'));
        return m.render().then(function(formNode) {
            controls.forEach(function(button) { button.disabled = !!m.readonly; });
            poll.add(update, 5);
            if (!m.readonly) document.addEventListener('uci-applied', function() {
                callControl('apply').then(function(result) {
                    if (result.code !== 0) throw new Error('设置已保存，但认证服务未能应用，请查看诊断日志');
                    return update();
                }).catch(function(err) { ui.addNotification(null, E('p', {}, err.message), 'error'); });
            });
            update();
            return E('div', {}, [E('h2', {}, 'SYSU 认证'),
                E('p', { 'class': 'cbi-map-descr' }, '中山大学有线校园网认证。地址与路由由网络接口配置管理。'),
                E('div', { 'class': 'cbi-section' }, [E('h3', {}, '认证状态'), status, details, timing, actions, logs]), formNode]);
        });
    }
});
