import * as fs from 'fs';
import { load, save } from '../io.uc';
let directory = '/tmp/sysu-snat-io-' + trim(fs.readfile('/proc/sys/kernel/random/uuid'));
if (!fs.mkdir(directory, 0700)) die('temporary directory unavailable');
let path = directory + '/state.json', checks = 0;
let failure;
function check(ok) { checks++; if (!ok) die('checkpoint check failed'); }
try {
    save(path, { generation: 1 });
    check(load(path, null).generation == 1);
    save(path, { generation: 2 });
    check(load(path, null).generation == 2);
    check(json(fs.readfile(path + '.previous')).generation == 1);
    fs.writefile(path, '{broken');
    check(load(path, null).generation == 1);
    check(json(fs.readfile(path)).generation == 1);
    fs.writefile(directory + '/intent.json', '{broken');
    let rejected = false;
    try { load(directory + '/intent.json', {}); } catch (e) { rejected = true; }
    check(rejected);
} catch (e) { failure = e; }
for (let name in ['state.json', 'state.json.previous', 'state.json.next', 'state.json.previous.next', 'intent.json'])
    fs.unlink(directory + '/' + name);
fs.rmdir(directory);
if (failure) die(failure);
printf('state checkpoints: %d checks passed\n', checks);
