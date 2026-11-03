#!/usr/bin/env python3
"""Build architecture-independent SYSU SNAT APK/IPK packages with an OpenWrt SDK."""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import tempfile

REPO = pathlib.Path(__file__).resolve().parents[1]
FILES = [
    ('files/sysu-snat.config', 'etc/config/sysu-snat', 0o600),
    ('files/sysu-snat.init', 'etc/init.d/sysu-snat', 0o755),
    ('files/sysu-snat-config.uc', 'usr/libexec/sysu-snat/config', 0o755),
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
    parser.add_argument('--output', type=pathlib.Path, default=REPO / 'dist/snat')
    args = parser.parse_args()
    source = REPO / 'snat'
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
        for path in (source / 'src').iterdir():
            if path.suffix == '.lua':
                put('usr/lib/sysu-snat/' + path.name, path.read_text())
            elif path.name in ('ipv4', 'ipv6', 'worker'):
                put('usr/libexec/sysu-snat/' + path.name, path.read_text(), 0o755)
        common = '#!/bin/sh\n[ -s "${IPKG_INSTROOT}/lib/functions.sh" ] || exit 0\n. "${IPKG_INSTROOT}/lib/functions.sh"\nexport root="${IPKG_INSTROOT}"\nexport pkgname="sysu-snat"\n'
        post = common + 'default_postinst\n[ -n "$IPKG_INSTROOT" ] || { [ "$(uci -q get sysu-snat.main.enabled)" = 1 ] || /etc/init.d/sysu-snat disable; }\n'
        pre = common + 'default_prerm\n'
        put('CONTROL/control', f'Package: sysu-snat\nVersion: {version}\nArchitecture: all\nMaintainer: RenAhsAcme\nLicense: GPL-3.0-only\nSection: net\nPriority: optional\nDepends: ' + ', '.join(deps) + '\nDescription: SYSU SNAT source pools and connection policies with LuCI\n')
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
        metadata = {'name': 'sysu-snat', 'version': version, 'arch': 'noarch', 'license': 'GPL-3.0-only',
                    'origin': 'sysu-snat', 'maintainer': 'RenAhsAcme', 'description': 'SYSU SNAT source pools and connection policies with LuCI',
                    'url': 'https://github.com/RenAhsAcme/SYSU-Network-Solution', 'depends': ' '.join(deps)}
        for key, value in metadata.items():
            apk.extend(['--info', key + ':' + value])
        for name, script in [('post-install', post), ('post-upgrade', post), ('pre-upgrade', pre), ('pre-deinstall', pre)]:
            path = work / name
            path.write_text(script)
            path.chmod(0o755)
            apk.extend(['--script', name + ':' + str(path)])
        apk.extend(['--files', stage, '--output', args.output / f'sysu-snat-{version}-all.apk'])
        subprocess.run([host / 'fakeroot', '--', 'sh', '-c', 'chown -R 0:0 "$1"; shift; exec "$@"', 'sh', stage, *apk], check=True)
        (args.output / f'sysu-snat_{version}_all.ipk').rename(args.output / f'sysu-snat-{version}-all.ipk')
    packages = sorted(args.output.glob(f'sysu-snat-{version}-all.*'))
    (args.output / 'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + p.name + '\n' for p in packages))
    for path in packages:
        print(path)


if __name__ == '__main__':
    main()
