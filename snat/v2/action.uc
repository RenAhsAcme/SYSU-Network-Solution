#!/usr/bin/ucode
import { save, load } from './io.uc';
import { RUN } from './backend.uc';
let action = ARGV[0], id = ARGV[1];
if (index(['start', 'stop', 'apply'], action) < 0 || !match(id || '', /^[a-f0-9-]{36}$/)) die('invalid internal action');
let result = system(['/etc/init.d/sysu-snat', action == 'stop' ? 'stop' : 'reload']);
let op = load(RUN + '/operation.json', {});
if (op.id == id) {
    op.state = result ? 'failed' : 'completed'; op.code = result; op.finished = time();
    save(RUN + '/operation.json', op);
}
