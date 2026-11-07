import * as fs from 'fs';

export function quote(s) {
    s = '' + s;
    if (index(s, '\x00') >= 0) die('NUL in argument');
    return "'" + replace(s, /'/g, "'\\''") + "'";
};
export function capture(args, limit) {
    let p = fs.popen('exec ' + join(' ', map(args, quote)) + ' 2>/dev/null');
    if (!p) return { code: 127, text: '' };
    let text = p.read(limit || 1048576) || '';
    let extra = p.read(1), status = p.close();
    return { code: length(extra || '') ? 125 : status, text };
};
export function json_command(args) {
    let r = capture(args);
    if (r.code) die('command failed: ' + args[0]);
    return json(r.text);
};
export function save(path, data) {
    if (match(path, /\/state\.json$/)) {
        let previous = fs.readfile(path, 1048576);
        if (previous != null) {
            json(previous);
            if (!fs.writefile(path + '.previous.next', previous) || !fs.chmod(path + '.previous.next', 0600) ||
                !fs.rename(path + '.previous.next', path + '.previous')) die('state checkpoint failed');
        }
    }
    if (!fs.writefile(path + '.next', sprintf('%J\n', data)) || !fs.chmod(path + '.next', 0600) ||
        !fs.rename(path + '.next', path)) die('state publication failed');
};
export function load(path, fallback) {
    try { return json(fs.readfile(path, 1048576) || 'null') || fallback; } catch (e) {
        if (match(path, /\/state\.json$/)) {
            let previous = json(fs.readfile(path + '.previous', 1048576) || 'null');
            if (previous) {
                if (!fs.writefile(path, sprintf('%J\n', previous)) || !fs.chmod(path, 0600)) die('state recovery failed');
                return previous;
            }
        }
        die('invalid state ' + path);
    }
};
export function clock() { return +split(fs.readfile('/proc/uptime'), ' ')[0]; };
export function bounded(helper, args, seconds) { return capture([helper, 'run', '' + (seconds || 12), ...args]); };
