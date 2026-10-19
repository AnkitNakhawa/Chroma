#!/usr/bin/env python3
"""Generate self-contained runtime tests before entering a clean machine/container."""
import argparse
import hashlib
import io
import tarfile
import urllib.request
import json
from pathlib import Path
import shutil
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from quality_fixtures import fixtures

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--ffmpeg', required=True)
p.add_argument('--font', help='Optional local font; default downloads hash-verified DejaVu with its license')
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
if not a.font:
    url = 'https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2'
    with urllib.request.urlopen(url, timeout=120) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != 'fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7':
        raise RuntimeError('Test font hash mismatch')
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:bz2') as tar:
        a.font = a.output / 'test-font.ttf'
        a.font.write_bytes(tar.extractfile('dejavu-fonts-ttf-2.37/ttf/DejaVuSans.ttf').read())
        (a.output / 'FONT-LICENSE.txt').write_bytes(tar.extractfile('dejavu-fonts-ttf-2.37/LICENSE').read())
fixtures(a.output, a.ffmpeg, a.font)
if (a.output / 'test-font.ttf').exists():
    (a.output / 'test-font.ttf').unlink()
# Runtime smoke is short but covers text, image, footage, shapes, transforms and audio.
# Asset paths are relative; it must survive moving to a different machine.
doc = json.loads((a.output / 'fractional.json').read_text())
(a.output / 'smoke.json').write_text(json.dumps(doc, indent=2) + '\n')
tiny = dict(schema_version=1, canvas=dict(width=8, height=4,
            frame_rate=dict(numerator=30, denominator=1), duration_frames=1), layers=[])
(a.output / 'tiny.json').write_text(json.dumps(tiny) + '\n')
shutil.copy2(Path(__file__).with_name('smoke.sh'), a.output / 'smoke.sh')
