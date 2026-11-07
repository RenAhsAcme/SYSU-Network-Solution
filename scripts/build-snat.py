#!/usr/bin/env python3
"""Build the single SYSU SNAT package, including native helpers, with an OpenWrt SDK."""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile
import os

REPO = pathlib.Path(__file__).resolve().parents[1]
FILES = [
    ('files/sysu-snat.config', 'etc/config/sysu-snat', 0o600),
    ('files/sysu-snat-preupgrade', 'usr/libexec/sysu-snat/preupgrade', 0o755),
    ('files/sysu-snat-postinst', 'usr/libexec/sysu-snat/postinst', 0o755),
    ('files/sysu-snat.init', 'etc/init.d/sysu-snat', 0o755),
    ('files/sysu-snat-dns', 'usr/libexec/sysu-snat/dns', 0o755),
    ('files/sysu-snat-dns.uc', 'usr/libexec/sysu-snat/dns-config', 0o755),
    ('files/sysu-snat.rpc', 'usr/libexec/rpcd/sysu-snat', 0o755),
    ('files/sysu-snat.acl.json', 'usr/share/rpcd/acl.d/sysu-snat.json', 0o644),
    ('files/sysu-snat.menu.json', 'usr/share/luci/menu.d/sysu-snat.json', 0o644),
    ('files/sysu-snat.js', 'www/luci-static/resources/view/sysu-snat.js', 0o644),
    ('LICENSE', 'usr/share/licenses/sysu-snat/LICENSE', 0o644),
    ('NOTICE.md', 'usr/share/licenses/sysu-snat/NOTICE.md', 0o644),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True, type=pathlib.Path)
    parser.add_argument('--arch', help='Override SDK configured package architecture')
    parser.add_argument('--output', type=pathlib.Path, default=REPO / 'dist/snat')
    args = parser.parse_args()
    source = REPO / 'snat'
    arch = args.arch or re.search(r'^CONFIG_TARGET_ARCH_PACKAGES="([^"]+)"', (args.sdk / '.config').read_text(), re.M)[1]
    toolchains = list((args.sdk / 'staging_dir').glob(f'toolchain-{arch}_gcc-*_musl'))
    if len(toolchains) != 1:
        raise RuntimeError('Cannot identify target toolchain')
    compilers = list((toolchains[0] / 'bin').glob('*-openwrt-linux-musl-gcc'))
    if not compilers:
        raise RuntimeError(f'Toolchain compiler unavailable for {arch}; select the installed target with --arch')
    compiler = compilers[0]
    target = args.sdk / f'staging_dir/target-{arch}_musl'
    makefile = (source / 'Makefile').read_text()
    version = re.search(r'^PKG_VERSION:=(.+)$', makefile, re.M)[1] + '-r' + re.search(r'^PKG_RELEASE:=(.+)$', makefile, re.M)[1]
    deps = re.search(r'^  DEPENDS:=(.+)$', makefile, re.M)[1].replace('+', '').split()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='sysu-snat-') as temp:
        work = pathlib.Path(temp)
        stage = work / 'root'

        def put(name, data, mode=0o644):
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data, newline='\n')
            path.chmod(mode)

        for src, dest, mode in FILES:
            put(dest, (source / src).read_text(), mode)
        for path in (source / 'v2').glob('*.uc'):
            put('usr/libexec/sysu-snat/v2/' + path.name, path.read_text())
        env = dict(os.environ, STAGING_DIR=str(target))
        for name, src in [('arp', 'arp.c'), ('net-helper', 'net.c')]:
            output = stage / 'usr/libexec/sysu-snat' / name
            output.parent.mkdir(parents=True, exist_ok=True)
            command = [str(compiler), '-std=c11', '-Os', '-static', '-s', '-Wall', '-Wextra', '-Werror',
                       '-I' + str(target / 'usr/include'), str(source / 'v2' / src), '-L' + str(target / 'usr/lib'), '-o', str(output)]
            if name == 'net-helper':
                command += ['-lnetfilter_conntrack', '-lnfnetlink', '-lmnl']
            subprocess.run(command, env=env, check=True)
            output.chmod(0o755)
        common = '#!/bin/sh\n[ -s "${IPKG_INSTROOT}/lib/functions.sh" ] || exit 0\n. "${IPKG_INSTROOT}/lib/functions.sh"\nexport root="${IPKG_INSTROOT}"\nexport pkgname="sysu-snat"\n'
        post = '#!/bin/sh\n[ -n "$IPKG_INSTROOT" ] && exit 0\nexec /usr/libexec/sysu-snat/postinst\n'
        pre = common + '[ "$PKG_UPGRADE" != 1 ] || exit 0\ndefault_prerm\n[ -n "$IPKG_INSTROOT" ] || { [ ! -x /usr/libexec/sysu-snat/dns ] || /usr/libexec/sysu-snat/dns release-wait; }\n'
        put('CONTROL/control', f'Package: sysu-snat\nVersion: {version}\nArchitecture: {arch}\nMaintainer: RenAhsAcme\nLicense: GPL-3.0-only\nSection: net\nPriority: optional\nDepends: ' + ', '.join(deps) + '\nDescription: SYSU SNAT source pools and connection policies with LuCI\n')
        put('CONTROL/conffiles', '/etc/config/sysu-snat\n')
        put('CONTROL/postinst', post, 0o755)
        put('CONTROL/prerm', pre, 0o755)
        host = args.sdk / 'staging_dir/host/bin'
        subprocess.run([host / 'fakeroot', '--', args.sdk / 'scripts/ipkg-build', stage, args.output], check=True)
        shutil.rmtree(stage / 'CONTROL')
        put('lib/apk/packages/sysu-snat.conffiles', '/etc/config/sysu-snat\n')
        digest = hashlib.sha256((stage / 'etc/config/sysu-snat').read_bytes()).hexdigest()
        put('lib/apk/packages/sysu-snat.conffiles_static', '/etc/config/sysu-snat ' + digest + '\n')
        listing = sorted('/' + str(p.relative_to(stage)) for p in stage.rglob('*') if p.is_file())
        listing.append('/lib/apk/packages/sysu-snat.list')
        put('lib/apk/packages/sysu-snat.list', '\n'.join(sorted(listing)) + '\n')
        apk = [host / 'apk', 'mkpkg']
        metadata = {'name': 'sysu-snat', 'version': version, 'arch': arch, 'license': 'GPL-3.0-only',
                    'origin': 'sysu-snat', 'maintainer': 'RenAhsAcme', 'description': 'SYSU SNAT source pools and connection policies with LuCI',
                    'url': 'https://github.com/RenAhsAcme/SYSU-Network-Solution', 'depends': ' '.join(deps)}
        for key, value in metadata.items():
            apk.extend(['--info', key + ':' + value])
        for name, script in [('post-install', post), ('post-upgrade', post), ('pre-upgrade', (source / 'files/sysu-snat-preupgrade').read_text()), ('pre-deinstall', pre)]:
            path = work / name
            path.write_text(script)
            path.chmod(0o755)
            apk.extend(['--script', name + ':' + str(path)])
        apk.extend(['--files', stage, '--output', args.output / f'sysu-snat-{version}-{arch}.apk'])
        subprocess.run([host / 'fakeroot', '--', 'sh', '-c', 'chown -R 0:0 "$1"; shift; exec "$@"', 'sh', stage, *apk], check=True)
        (args.output / f'sysu-snat_{version}_{arch}.ipk').rename(args.output / f'sysu-snat-{version}-{arch}.ipk')
    packages = sorted(args.output.glob(f'sysu-snat-{version}-{arch}.*'))
    (args.output / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in packages))
    for path in packages:
        print(path)


if __name__ == '__main__':
    main()
