#!/usr/bin/env python3
"""Build the hash-locked release dependency prefix; never use host media libraries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tarfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def run(command, cwd=None, env=None):
    print('+', ' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--jobs', type=int, default=2)
    a = p.parse_args()
    work = a.work.resolve()
    prefix = work / 'prefix'
    sources = work / 'sources'
    sources.mkdir(parents=True, exist_ok=True)
    lock = json.loads((ROOT / 'packaging/sources.lock.json').read_text())
    env = os.environ.copy()
    env['PKG_CONFIG_LIBDIR'] = str(prefix / 'lib/pkgconfig')
    env['PKG_CONFIG_PATH'] = ''
    env.pop('PKG_CONFIG_SYSROOT_DIR', None)
    cmake_flags = ['-DCMAKE_BUILD_TYPE=Release', '-DBUILD_SHARED_LIBS=ON',
                   f'-DCMAKE_INSTALL_PREFIX={prefix}', f'-DCMAKE_PREFIX_PATH={prefix}', '-DCMAKE_INSTALL_LIBDIR=lib']
    cflags = f'-I{prefix}/include'
    ldflags = f'-L{prefix}/lib -Wl,-rpath,{prefix}/lib'
    if platform.system() == 'Darwin':
        sdk = env.get('SDKROOT') or subprocess.check_output(['xcrun', '--show-sdk-path'], text=True).strip()
        env['SDKROOT'] = sdk
        env['MACOSX_DEPLOYMENT_TARGET'] = '14.0'
        cmake_flags += [f'-DCMAKE_OSX_SYSROOT={sdk}', '-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0']
        cflags += f' -isysroot {sdk} -mmacosx-version-min=14.0'
        ldflags += f' -isysroot {sdk} -mmacosx-version-min=14.0'
    recipe = hashlib.sha256(Path(__file__).read_bytes() + json.dumps(lock, sort_keys=True).encode() +
                            json.dumps([platform.system(), platform.machine(), cmake_flags, cflags, ldflags,
                                        {k: env.get(k) for k in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS')}]).encode()).hexdigest()
    for name in ('zlib', 'openh264', 'ffmpeg', 'freetype', 'harfbuzz', 'json'):
        spec = lock[name]
        archive = sources / spec['archive']
        if not archive.exists():
            with urllib.request.urlopen(spec['url'], timeout=120) as response:
                archive.write_bytes(response.read())
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if digest != spec['sha256']:
            raise RuntimeError(f'{archive}: SHA-256 mismatch')
        source = work / 'src' / name
        stamp = work / f'{name}.stamp'
        if stamp.exists():
            if stamp.read_text().strip() != recipe:
                raise RuntimeError('Build recipe changed; use a fresh --work directory')
            continue
        shutil.rmtree(source, ignore_errors=True)
        source.mkdir(parents=True)
        with tarfile.open(archive) as tar:
            # Strip the single upstream root; reject path traversal and unsafe links.
            members = tar.getmembers()
            roots = {Path(m.name).parts[0] for m in members}
            if len(roots) != 1:
                raise RuntimeError('Expected one source archive root')
            extract = work / 'extract' / name
            shutil.rmtree(extract, ignore_errors=True)
            extract.mkdir(parents=True)
            tar.extractall(extract, filter='data')
            source.rmdir()
            shutil.move(str(extract / next(iter(roots))), source)
        if name == 'openh264':
            make_env = env.copy()
            make_env['CFLAGS'] = cflags
            make_env['CXXFLAGS'] = cflags
            make_env['LDFLAGS'] = ldflags
            run(['make', f'-j{a.jobs}', 'libraries', 'USE_ASM=No', f'PREFIX={prefix}'], source, make_env)
            run(['make', 'install-shared', 'USE_ASM=No', f'PREFIX={prefix}'], source, make_env)
        elif name == 'ffmpeg':
            build = work / 'build' / name
            build.mkdir(parents=True, exist_ok=True)
            run([source / 'configure', f'--prefix={prefix}', '--enable-shared', '--disable-static',
                 '--enable-pic', '--disable-autodetect', '--disable-gpl', '--disable-nonfree',
                 '--enable-libopenh264', '--enable-zlib', '--disable-doc', '--disable-debug',
                 '--disable-network', '--disable-x86asm',
                 f'--extra-cflags={cflags}', f'--extra-ldflags={ldflags}'], build, env)
            run(['make', f'-j{a.jobs}'], build, env)
            run(['make', 'install'], build, env)
        else:
            options = {
                'zlib': ['-DZLIB_BUILD_TESTING=OFF'],
                'freetype': [f'-DFT_DISABLE_{dep}=ON' for dep in ('ZLIB', 'BZIP2', 'PNG', 'HARFBUZZ', 'BROTLI')],
                'harfbuzz': [f'-DHB_{option}=OFF' for option in (
                    'HAVE_CORETEXT', 'HAVE_GLIB', 'HAVE_ICU', 'HAVE_CAIRO',
                    'BUILD_UTILS', 'BUILD_SUBSET', 'BUILD_RASTER', 'BUILD_VECTOR', 'BUILD_GPU', 'BUILD_GPU_DEMO')] + ['-DHB_HAVE_FREETYPE=ON', f'-DFREETYPE_LIBRARY_RELEASE={prefix}/lib/' + ('libfreetype.dylib' if platform.system() == 'Darwin' else 'libfreetype.so'), f'-DFREETYPE_INCLUDE_DIR_freetype2={prefix}/include/freetype2', f'-DFREETYPE_INCLUDE_DIR_ft2build={prefix}/include/freetype2'],
                'json': ['-DJSON_BuildTests=OFF'],
            }[name]
            build = work / 'build' / name
            run(['cmake', '-S', source, '-B', build, *cmake_flags, *options], env=env)
            run(['cmake', '--build', build, '--parallel', a.jobs], env=env)
            run(['cmake', '--install', build], env=env)
        stamp.write_text(recipe + '\n')
    print(prefix)


if __name__ == '__main__':
    main()
