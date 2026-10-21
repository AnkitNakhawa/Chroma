#!/usr/bin/env python3
"""Exercise the real publication shell with fake GitHub writes and tiny assets."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[1]
workflow = (ROOT / '.github/workflows/release.yml').read_text()
shell = textwrap.dedent(workflow.split('      - name: Validate tag and publish all tested archives\n', 1)[1].split('        run: |\n', 1)[1])
# macOS has shasum; Ubuntu publication uses sha256sum.
if not shutil.which('sha256sum'):
    shell = 'sha256sum() { shasum -a 256 "$@"; }\n' + shell

for case in ('manual', 'tag', 'existing-tag', 'wrong-version', 'missing-asset', 'bad-checksum', 'missing-notes', 'api-failure'):
    with tempfile.TemporaryDirectory(prefix='chroma publication ') as name:
        root = Path(name)
        (root / 'bin').mkdir()
        (root / 'bin/gh').write_text('''#!/bin/sh
set -eu
if [ "$1" = api ]; then
  test "$CASE" != api-failure
  if [ "$CASE" = existing-tag ]; then echo 1; else echo 0; fi
else
  printf '%s\\n' "$@" > "$PWD/published-arguments"
fi
''')
        (root / 'bin/gh').chmod(0o755)
        shutil.copy2(ROOT / 'CMakeLists.txt', root / 'CMakeLists.txt')
        notes = root / 'docs/releases'
        notes.mkdir(parents=True)
        if case != 'missing-notes':
            (notes / 'v0.1.0.md').write_text('Release notes\n')
        for target in ('linux-x86_64', 'darwin-arm64', 'darwin-x86_64'):
            folder = root / 'downloaded' / ('release ' + target) / 'dist'
            folder.mkdir(parents=True)
            lines = []
            for suffix in ('.tar.gz', '-sources.tar.gz'):
                archive = folder / ('chroma-0.1.0-' + target + suffix)
                archive.write_bytes(b'test archive')
                lines.append(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name)
            (folder / ('chroma-0.1.0-' + target + '.sha256')).write_text('\n'.join(lines) + '\n')
        if case == 'missing-asset':
            archive.unlink()
        if case == 'bad-checksum':
            archive.write_bytes(b'corrupted')
        env = dict(os.environ, CASE=case, PATH=str(root / 'bin') + os.pathsep + os.environ['PATH'],
                   GITHUB_REPOSITORY='AnkitNakhawa/Chroma', GITHUB_SHA='tested-commit',
                   GITHUB_REF='refs/heads/main', GITHUB_REF_NAME='main')
        if case in ('tag', 'wrong-version'):
            env.update(GITHUB_REF='refs/tags/v0.1.0', GITHUB_REF_NAME='v0.1.0' if case == 'tag' else 'v0.2.0')
        result = subprocess.run(['bash', '-eu', '-o', 'pipefail', '-c', shell], cwd=root, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        success = case in ('manual', 'tag')
        assert (result.returncode == 0) == success, (case, result.stdout)
        published = root / 'published-arguments'
        assert published.exists() == success, case
        if success:
            args = published.read_text().splitlines()
            assert args[:3] == ['release', 'create', 'v0.1.0'], args
            assert sum(a.endswith(('.tar.gz', '.sha256')) for a in args) == 9, args
            assert args[-2:] == ['--notes-file', 'docs/releases/v0.1.0.md'], args
            assert ('--verify-tag' in args) == (case == 'tag'), args
            assert ('tested-commit' in args) == (case == 'manual'), args
        print('PASS:', case)
