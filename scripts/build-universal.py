#!/usr/bin/env python3
"""Build static multi-architecture OpenWrt APK/IPK packages with Zig and an OpenWrt tree."""
import argparse
import concurrent.futures
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
TARGETS = {
    'x86_64': ('x86_64-linux-musl', 'x86_64'),
    'x86': ('x86-linux-musl', 'i486'),
    'arm': ('arm-linux-musleabi', 'arm926ej_s'),
    'aarch64': ('aarch64-linux-musl', 'generic'),
    'mips': ('mips-linux-musl', 'mips32'),
    'mipsel': ('mipsel-linux-musl', 'mips32'),
    'mips64': ('mips64-linux-musl', 'mips64'),
    'mips64el': ('mips64el-linux-musl', 'mips64'),
    'powerpc': ('powerpc-linux-musl', 'ppc'),
    'powerpc64le': ('powerpc64le-linux-musl', 'ppc64le'),
    'powerpc64': ('powerpc64-linux-musl', 'ppc64'),
    'riscv64': ('riscv64-linux-musl', 'baseline'),
}
FILES = [
    ('sysu-auth-import.uc', 'usr/sbin/sysu-auth-import', 0o755),
    ('sysu-auth.init', 'etc/init.d/sysu-auth', 0o755),
    ('sysu-auth.config', 'etc/config/sysu-auth', 0o600),
    ('sysu-auth.rpc', 'usr/libexec/rpcd/sysu-auth', 0o755),
    ('sysu-auth.acl.json', 'usr/share/rpcd/acl.d/sysu-auth.json', 0o644),
    ('sysu-auth.menu.json', 'usr/share/luci/menu.d/sysu-auth.json', 0o644),
    ('sysu-auth.js', 'www/luci-static/resources/view/sysu-auth.js', 0o644),
]


def put(root, name, text, mode=0o644):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    path.chmod(mode)


def run(args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def build(arch, zig, work, binaries):
    target, cpu = TARGETS[arch]
    source = work / arch
    shutil.copytree(REPO / 'minieap/src', source,
                    ignore=shutil.ignore_patterns('build', 'sysu-auth'))
    log = work / (arch + '.log')
    with log.open('w') as output:
        try:
            run(['make', '-s', '-j2', 'CC=' + str(zig) + ' cc -target ' + target + ' -mcpu=' + cpu,
                 'CFLAGS=-Os', 'LDFLAGS=-static -s'], cwd=source, stdout=output, stderr=output)
        except subprocess.CalledProcessError:
            raise RuntimeError(arch + ': ' + log.read_text()[-6000:])
    shutil.copyfile(source / 'sysu-auth', binaries / arch)
    (binaries / arch).chmod(0o755)
    print('Built ' + arch, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zig', required=True, type=pathlib.Path)
    parser.add_argument('--sdk', required=True, type=pathlib.Path)
    parser.add_argument('--output', type=pathlib.Path, default=REPO / 'dist')
    parser.add_argument('--binaries', type=pathlib.Path, default=REPO / '.build/universal-bin')
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--only', nargs='+', choices=TARGETS)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    args.binaries.mkdir(parents=True, exist_ok=True)
    makefile = (REPO / 'minieap/Makefile').read_text()
    version = re.search(r'^PKG_VERSION:=(.+)$', makefile, re.M)[1] + '-r' + re.search(r'^PKG_RELEASE:=(.+)$', makefile, re.M)[1]
    with tempfile.TemporaryDirectory(prefix='sysu-universal-') as temp:
        work = pathlib.Path(temp)
        if not args.skip_build:
            with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
                list(pool.map(lambda arch: build(arch, args.zig, work, args.binaries), args.only or TARGETS))
        stage = work / 'root'
        for source, dest, mode in FILES:
            put(stage, dest, (REPO / 'minieap/files' / source).read_text(), mode)
        put(stage, 'usr/sbin/sysu-auth', (REPO / 'minieap/files/sysu-auth.universal').read_text(), 0o755)
        for arch in TARGETS:
            dest = stage / 'usr/libexec/sysu-auth' / arch / 'sysu-auth'
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(args.binaries / arch, dest)
            dest.chmod(0o755)
        put(stage, 'usr/share/licenses/sysu-auth/LICENSE', (REPO / 'minieap/src/LICENSE').read_text())
        preinstall = '#!/bin/sh\n[ -n "${IPKG_INSTROOT}" ] && exit 0\n' + (REPO / 'minieap/files/sysu-auth.universal').read_text().split('\n', 1)[1].replace('exec /usr/libexec/sysu-auth/"$arch"/sysu-auth "$@"', 'exit 0')
        for license_file in (REPO / 'minieap/files/licenses').iterdir():
            put(stage, 'usr/share/licenses/sysu-auth/' + license_file.name, license_file.read_text())
        post = '#!/bin/sh\n[ "${IPKG_NO_SCRIPT}" = 1 ] && exit 0\n[ -s ${IPKG_INSTROOT}/lib/functions.sh ] || exit 0\n. ${IPKG_INSTROOT}/lib/functions.sh\nexport root="${IPKG_INSTROOT}"\nexport pkgname="sysu-auth"\nadd_group_and_user\ndefault_postinst\n'
        pre = '#!/bin/sh\n[ -s ${IPKG_INSTROOT}/lib/functions.sh ] || exit 0\n. ${IPKG_INSTROOT}/lib/functions.sh\nexport root="${IPKG_INSTROOT}"\nexport pkgname="sysu-auth"\ndefault_prerm\n'
        preupgrade = preinstall.rsplit('exit 0', 1)[0] + pre.split('\n', 1)[1]
        preupgrade = preupgrade.replace('#!/bin/sh\n', '#!/bin/sh\nexport PKG_UPGRADE=1\n', 1)
        deps = ['luci-base', 'rpcd', 'rpcd-mod-file', 'jshn', 'ucode', 'ucode-mod-fs', 'ucode-mod-uci']
        control = stage / 'CONTROL'
        put(stage, 'CONTROL/control', 'Package: sysu-auth\nVersion: ' + version + '\nArchitecture: all\nMaintainer: RenAhsAcme\nLicense: GPL-3.0\nSection: net\nPriority: optional\nRequire-User: sysu-auth:sysu-auth\nDepends: ' + ', '.join(deps) + '\nDescription: SYSU wired authentication and automatic session maintenance with LuCI\n')
        put(stage, 'CONTROL/conffiles', '/etc/config/sysu-auth\n')
        put(stage, 'CONTROL/preinst', preinstall, 0o755)
        put(stage, 'CONTROL/postinst', post, 0o755)
        put(stage, 'CONTROL/prerm', pre, 0o755)
        fakeroot = args.sdk / 'staging_dir/host/bin/fakeroot'
        run([fakeroot, '--', args.sdk / 'scripts/ipkg-build', stage, args.output])
        shutil.rmtree(control)
        meta = stage / 'lib/apk/packages'
        put(stage, 'lib/apk/packages/sysu-auth.rusers', 'sysu-auth:sysu-auth\n')
        put(stage, 'lib/apk/packages/sysu-auth.conffiles', '/etc/config/sysu-auth\n')
        digest = hashlib.sha256((stage / 'etc/config/sysu-auth').read_bytes()).hexdigest()
        put(stage, 'lib/apk/packages/sysu-auth.conffiles_static', '/etc/config/sysu-auth ' + digest + '\n')
        listing = sorted('/' + str(p.relative_to(stage)) for p in stage.rglob('*') if p.is_file())
        listing.append('/lib/apk/packages/sysu-auth.list')
        put(stage, 'lib/apk/packages/sysu-auth.list', '\n'.join(sorted(listing)) + '\n')
        put(work, 'pre-install', preinstall, 0o755)
        put(work, 'pre-upgrade', preupgrade, 0o755)
        put(work, 'post-install', post, 0o755)
        put(work, 'post-upgrade', post.replace('#!/bin/sh\n', '#!/bin/sh\nexport PKG_UPGRADE=1\n', 1), 0o755)
        put(work, 'pre-deinstall', pre, 0o755)
        apk = [args.sdk / 'staging_dir/host/bin/apk', 'mkpkg']
        info = {'name': 'sysu-auth', 'version': version, 'arch': 'noarch', 'license': 'GPL-3.0',
                'description': 'SYSU wired authentication and automatic session maintenance with LuCI',
                'origin': 'sysu-auth', 'maintainer': 'RenAhsAcme',
                'url': 'https://github.com/RenAhsAcme/SYSU-Network-Solution', 'depends': ' '.join(deps)}
        for key, value in info.items():
            apk.extend(['--info', key + ':' + value])
        for name in ('pre-install', 'pre-upgrade', 'post-install', 'post-upgrade', 'pre-deinstall'):
            apk.extend(['--script', name + ':' + str(work / name)])
        apk.extend(['--files', stage, '--output', args.output / ('sysu-auth-' + version + '-universal.apk')])
        run([fakeroot, '--', 'sh', '-c', 'chown -R 0:0 "$1"; shift; exec "$@"', 'sh', stage, *apk])
        ipk = args.output / ('sysu-auth_' + version + '_all.ipk')
        ipk.rename(args.output / ('sysu-auth-' + version + '-universal.ipk'))
    hashes = []
    for package in sorted(args.output.glob('sysu-auth-' + version + '-universal.*')):
        hashes.append(hashlib.sha256(package.read_bytes()).hexdigest() + '  ' + package.name)
    (args.output / 'SHA256SUMS').write_text('\n'.join(hashes) + '\n')
    manifest = {arch: {'target': TARGETS[arch][0], 'sha256': hashlib.sha256((args.binaries / arch).read_bytes()).hexdigest()} for arch in TARGETS}
    (args.output / 'architectures.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    main()
