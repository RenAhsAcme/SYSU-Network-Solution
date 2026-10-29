#!/usr/bin/env python3
"""Run static binaries and protocol tests natively or through qemu-user."""
import argparse
import importlib.util
import pathlib
import subprocess
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('universal', REPO / 'scripts/build-universal.py')
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)
EMULATORS = {'x86_64': None, 'x86': None, 'arm': 'arm', 'aarch64': 'aarch64',
             'mips': 'mips', 'mipsel': 'mipsel', 'mips64': 'mips64', 'mips64el': 'mips64el',
             'powerpc': 'ppc', 'powerpc64': 'ppc64', 'powerpc64le': 'ppc64le', 'riscv64': 'riscv64'}


def run(args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True, timeout=90)
    if result.returncode:
        raise RuntimeError(str(args) + '\n' + result.stdout + result.stderr)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True, type=pathlib.Path)
    args = parser.parse_args()
    source = REPO / 'minieap/src'
    tests = REPO / 'minieap/tests'
    hashes = source / 'packet_plugin/rjv3/rjv3_hashes'
    with tempfile.TemporaryDirectory(prefix='sysu-cross-test-') as directory:
        temp = pathlib.Path(directory)
        config = temp / 'config'
        for arch, (target, cpu) in builder.TARGETS.items():
            emulator = EMULATORS[arch]
            prefix = [REPO / '.build' / ('qemu-' + emulator + '-static')] if emulator else []
            binary = REPO / '.build/universal-bin' / arch
            assert run([*prefix, binary, '--version']).strip() == 'SYSU Auth 1.1.0'
            for debug in ('', 'debug=0\n', 'debug=1\n'):
                config.write_text('username=test\npassword=test\nnic=eth0\n' + debug)
                config.chmod(0o600)
                assert 'Configuration valid' in run([*prefix, binary, '--conf-file', config, '--check'])
            config.write_text('username=test\npassword=test\nnic=eth0\ndebug=2\n')
            invalid = subprocess.run([str(v) for v in [*prefix, binary, '--conf-file', config, '--check']], capture_output=True)
            assert invalid.returncode == 2
            compiler = [args.zig, 'cc', '-target', target, '-mcpu=' + cpu, '-static', '-Os',
                        '-D_GNU_SOURCE', '-UNDEBUG', '-std=gnu11', '-I' + str(source / 'include'), '-I' + str(hashes)]
            groups = {
                'protocol': [tests / 'test_protocol.c', source / 'packet_builder.c', source / 'sysu_protocol.c', source / 'util/packet_util.c', source / 'md5.c'],
                'session': [tests / 'test_session.c', source / 'md5.c'],
                'vz': [tests / 'test_vz.c', *[hashes / f for f in ('checkV4.c', 'rjmd5.c', 'rjsha1.c', 'rjripemd128.c', 'rjtiger.c', 'rjtiger_sbox.c', 'rjwhirlpool.c', 'rjwhirlpool_sbox.c', 'byte_order.c')], source / 'md5.c'],
            }
            for name, files in groups.items():
                executable = temp / (arch + '-' + name)
                run([*compiler, *files, '-o', executable])
                print(arch + ': ' + run([*prefix, executable]).strip(), flush=True)
            print(arch + ': startup and normal/debug configuration passed', flush=True)


if __name__ == '__main__':
    main()
