#!/usr/bin/env python3
"""Create and audit a relocatable CLI/C SDK archive from the controlled prefix."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
SYSTEM_ELF = re.compile(r'^(lib(c|m|pthread|dl|rt|stdc\+\+|gcc_s)\.so(?:\..*)?|ld-linux[^/]*\.so(?:\..*)?)$')


def output(args):
    return subprocess.check_output(list(map(str, args)), text=True)


def run(args):
    subprocess.run(list(map(str, args)), check=True)


def binaries(root):
    return sorted(p for folder in ('bin', 'lib', 'libexec/chroma')
                  for p in (root / folder).glob('*')
                  if p.is_file() and not p.is_symlink() and (folder != 'lib' or '.so' in p.name or p.suffix == '.dylib'))


def deps(path):
    if platform.system() == 'Darwin':
        lines = output(['otool', '-L', path]).splitlines()[1:]
        links = [line.strip().split(' (compatibility')[0] for line in lines]
        # otool -L lists a dylib's own install ID first; it is not a load edge.
        return links[1:] if path.suffix == '.dylib' else links
    return re.findall(r'\(NEEDED\).*\[(.*?)\]', output(['readelf', '-d', path]))


def audit(root):
    evidence = {}
    for link in (root / 'lib').glob('*'):
        if link.is_symlink() and (not link.resolve().is_relative_to(root.resolve()) or not link.is_file()):
            raise RuntimeError(f'Library symlink escapes package or is broken: {link.name}')
    for path in binaries(root):
        relative = str(path.relative_to(root))
        links = deps(path)
        for link in links:
            if platform.system() == 'Darwin':
                if link.startswith(('/usr/lib/', '/System/Library/')):
                    continue
                if not link.startswith('@loader_path/'):
                    raise RuntimeError(f'{relative}: non-relative dependency {link}')
                resolved = (path.parent / link.removeprefix('@loader_path/')).resolve()
                if not resolved.is_relative_to(root.resolve()) or not resolved.is_file():
                    raise RuntimeError(f'{relative}: missing or escaped dependency {link}')
            elif not SYSTEM_ELF.fullmatch(link) and not (root / 'lib' / link).is_file():
                raise RuntimeError(f'{relative}: unbundled dependency {link}')
        if platform.system() == 'Linux':
            dynamic = output(['readelf', '-d', path])
            rpaths = re.findall(r'\((?:RUNPATH|RPATH)\).*\[(.*?)\]', dynamic)
            expected = '$ORIGIN' if path.parent == root / 'lib' else '$ORIGIN/' + os.path.relpath(root / 'lib', path.parent)
            if rpaths != [expected]:
                raise RuntimeError(f'{relative}: unexpected search paths {rpaths}, expected {expected}')
        else:
            loads = output(['otool', '-l', path])
            if 'cmd LC_RPATH' in loads:
                raise RuntimeError(f'{relative}: unnecessary runtime search path')
            if path.suffix == '.dylib':
                install_id = output(['otool', '-D', path]).splitlines()[1].strip()
                alias = root / 'lib' / install_id.removeprefix('@rpath/')
                if not install_id.startswith('@rpath/') or alias.resolve() != path.resolve():
                    raise RuntimeError(f'{relative}: SDK install ID must support consumer RPATHs: {install_id}')
            run(['codesign', '--verify', '--strict', path])
        evidence[relative] = links
    if not evidence:
        raise RuntimeError('No package binaries found')
    return evidence


def relocate(root):
    libs = root / 'lib'
    names = {p.name for p in libs.iterdir() if p.is_file()}
    for path in binaries(root):
        if platform.system() == 'Darwin':
            # Keep the existing signature allocation while editing: removing it
            # first can leave a __LINKEDIT hole rejected by Xcode 16 tools.
            # install_name_tool invalidates the signature; re-sign below.
            changes = []
            if path.suffix == '.dylib':
                original_id = output(['otool', '-D', path]).splitlines()[1].strip()
                # Preserve the ABI-major name, not the physical file's version.
                changes += ['-id', '@rpath/' + Path(original_id).name]
            for link in deps(path):
                if link.startswith(('/usr/lib/', '/System/Library/')):
                    continue
                name = Path(link).name
                if name not in names:
                    raise RuntimeError(f'{path.name}: dependency not from controlled prefix: {link}')
                replacement = '@loader_path/' + os.path.relpath(libs / name, path.parent)
                changes += ['-change', link, replacement]
            loads = output(['otool', '-l', path])
            for rpath in re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', loads):
                changes += ['-delete_rpath', rpath]
            if changes:
                run(['install_name_tool', *changes, path])
            run(['codesign', '--force', '--sign', '-', path])
        else:
            run(['patchelf', '--set-rpath', '$ORIGIN' if path.parent == libs else
                 '$ORIGIN/' + os.path.relpath(libs, path.parent), path])
    return audit(root)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path)
    p.add_argument('--work', type=Path)
    p.add_argument('--output', type=Path, default=ROOT / 'dist')
    p.add_argument('--audit', type=Path)
    a = p.parse_args()
    if a.audit:
        print(json.dumps(audit(a.audit.resolve()), indent=2))
        return
    if not a.build or not a.work:
        p.error('--build and --work are required')
    build, work, out = a.build.resolve(), a.work.resolve(), a.output.resolve()
    prefix = work / 'prefix'
    configuration = output([prefix / 'bin/ffmpeg', '-hide_banner', '-buildconf'])
    for required in ('--disable-gpl', '--disable-nonfree', '--enable-shared', '--enable-libopenh264'):
        if required not in configuration:
            raise RuntimeError(f'Unexpected FFmpeg build: missing {required}')
    if '--enable-gpl' in configuration or '--enable-nonfree' in configuration:
        raise RuntimeError('GPL/nonfree FFmpeg cannot be packaged by this recipe')
    version = re.search(r'\bVERSION (\d+\.\d+\.\d+)', (ROOT / 'CMakeLists.txt').read_text())[1]
    arch = {'aarch64': 'arm64', 'amd64': 'x86_64'}.get(platform.machine().lower(), platform.machine().lower())
    if platform.system() == 'Linux' and (arch != 'x86_64' or os.confstr('CS_GNU_LIBC_VERSION') != 'glibc 2.35'):
        raise RuntimeError('Linux release packages must be built on the Ubuntu 22.04/glibc 2.35 x86_64 baseline')
    if platform.system() not in ('Linux', 'Darwin') or arch not in ('arm64', 'x86_64'):
        raise RuntimeError('Unsupported release target')
    name = f'chroma-{version}-{platform.system().lower()}-{arch}'
    stage = out / name
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    run(['cmake', '--install', build, '--prefix', stage])
    lib = stage / 'lib'
    for source in (prefix / 'lib').iterdir():
        if '.so' in source.name or source.suffix == '.dylib':
            target = lib / source.name
            if source.is_symlink():
                link = os.readlink(source)
                if os.path.isabs(link) or '/' in link:
                    raise RuntimeError(f'Unsafe library symlink: {source}')
                target.symlink_to(link)
            else:
                shutil.copy2(source, target)
    tools = stage / 'libexec/chroma'
    tools.mkdir(parents=True, exist_ok=True)
    for tool in ('ffmpeg', 'ffprobe'):
        shutil.copy2(prefix / 'bin' / tool, tools / tool)
    # Verify the exported CMake package without enabling the C++ language.
    consumer_source = work / 'sdk-consumer-source'
    consumer_source.mkdir(exist_ok=True)
    shutil.copy2(ROOT / 'packaging/consumer.cmake', consumer_source / 'CMakeLists.txt')
    shutil.copy2(ROOT / 'tests/install/main.c', consumer_source / 'main.c')
    consumer_build = work / 'sdk-consumer-build'
    args = ['cmake', '-S', consumer_source, '-B', consumer_build,
            f'-DCMAKE_PREFIX_PATH={stage}', '-DCMAKE_BUILD_TYPE=Release']
    if platform.system() == 'Darwin':
        args += ['-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0']
        if os.environ.get('SDKROOT'):
            args += [f"-DCMAKE_OSX_SYSROOT={os.environ['SDKROOT']}"]
    run(args)
    run(['cmake', '--build', consumer_build, '--parallel', '2'])
    run(['cmake', '--install', consumer_build, '--prefix', stage])
    evidence = relocate(stage)
    # Compile again against the actual relocated SDK. In particular, a dylib
    # install ID must let a NEW consumer resolve it through its own RPATH.
    run(['cmake', '--build', consumer_build, '--clean-first', '--parallel', '2'])
    subprocess.run([str(consumer_build / 'chroma-sdk-consumer')], check=True,
                   env={'PATH': '/usr/bin:/bin', 'HOME': str(work)})
    share = stage / 'share/chroma'
    licenses = share / 'licenses'
    licenses.mkdir()
    lock = json.loads((ROOT / 'packaging/sources.lock.json').read_text())
    license_files = {'ffmpeg': ['COPYING.LGPLv2.1', 'LICENSE.md'], 'openh264': ['LICENSE'],
                     'freetype': ['docs/FTL.TXT', 'LICENSE.TXT'], 'harfbuzz': ['COPYING'],
                     'zlib': ['LICENSE'], 'json': ['LICENSE.MIT']}
    for dependency, files in license_files.items():
        dest = licenses / dependency
        dest.mkdir()
        for file in files:
            shutil.copy2(work / 'src' / dependency / file, dest / Path(file).name)
    shutil.copy2(ROOT / 'packaging/README.md', share / 'RELEASE.md')
    shutil.copy2(ROOT / 'packaging/sources.lock.json', share)
    revision = output(['git', '-C', ROOT, 'rev-parse', 'HEAD']).strip()
    manifest = {'version': version, 'revision': revision, 'platform': platform.system(), 'architecture': arch,
                'dependencies': lock, 'runtime_dependencies': evidence,
                'minimum_os': 'macOS 14.0' if platform.system() == 'Darwin' else 'glibc 2.35 / Ubuntu 22.04',
                'encoder': 'libopenh264', 'abi': 1}
    (share / 'release.json').write_text(json.dumps(manifest, indent=2) + '\n')
    archive = out / (name + '.tar.gz')
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(stage, arcname=name)
    # Supply exact dependency source archives and the recipe beside every binary.
    source_archive = out / (name + '-sources.tar.gz')
    source_tar = work / 'chroma-source.tar'
    run(['git', '-C', ROOT, 'archive', '--format=tar', f'--output={source_tar}', 'HEAD'])
    with tarfile.open(source_archive, 'w:gz') as tar:
        tar.add(source_tar, arcname='sources/chroma.tar')
        for dep in lock.values():
            tar.add(work / 'sources' / dep['archive'], arcname='sources/' + dep['archive'])
        tar.add(ROOT / 'packaging', arcname='sources/packaging', filter=lambda info: None if '__pycache__' in info.name else info)
    checksums = out / (name + '.sha256')
    checksums.write_text(''.join(f'{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.name}\n' for f in (archive, source_archive)))
    print(archive)


if __name__ == '__main__':
    main()
