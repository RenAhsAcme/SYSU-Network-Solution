#!/usr/bin/env python3
"""Read live v2 state and check native WAN HTTPS without changing router configuration."""
import argparse
import ipaddress
import json
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--router', default='OpenWrt', help='SSH host or configured alias')
args = parser.parse_args()


def remote(*command):
    return subprocess.check_output(
        ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', args.router,
         ' '.join(map(shlex.quote, command))], timeout=25).decode().strip()


service = json.loads(remote('ubus', 'call', 'service', 'list', '{"name":"sysu-snat"}'))
instances = service.get('sysu-snat', {}).get('instances', {})
assert list(instances) == ['controller'] and instances['controller']['running'], instances
status = json.loads(remote('ubus', 'call', 'sysu-snat', 'status'))
assert status.get('version') == '2.0.0' and not status.get('error') and not status.get('dns_error'), status.get('error')
for key, family in [('ipv4', 4), ('ipv6', 6)]:
    state = status['families'][key]
    assert state['state'] == 'active' and state['qualified'] == 5 and state['usable'] == 5, state
    assert state['native_ok'] and state['rules'], state
    primary = next(a['address'] for a in state['native'] if a['primary'])
    assert ipaddress.ip_address(primary) not in {ipaddress.ip_address(a['address']) for a in state['pool']}, state
    code = remote('curl', f'-{family}', '--noproxy', '*', '--interface', f'ifhost!{state["device"]}!{primary}',
                  '--max-time', '8', '-fsS', '-o', '/dev/null', '-w', '%{http_code}', 'https://mirrors.ustc.edu.cn/')
    assert code == '200', (key, code)
    print(f'PASS {key}: 5 qualified members, native WAN HTTPS {code}')
print('PASS: one v2 controller, no extra service instances, no published service errors')
