#!/usr/bin/env python3
"""Compare scalar and lookup output conversion on the frozen showcase."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time

EXPECTED_COMPOSITION = 'a8faa46b091597b7952bc49799497872a2ef1ed09ff791bb8e8324e826182cb6'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--optimized', type=Path, required=True)
    parser.add_argument('--composition', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--runs', type=int, default=3)
    args = parser.parse_args()
    if args.runs < 3:
        parser.error('at least three measured runs per variant are required')
    digest = hashlib.sha256(args.composition.read_bytes()).hexdigest()
    if digest != EXPECTED_COMPOSITION:
        parser.error('composition differs from the frozen 1080p showcase')
    document = json.loads(args.composition.read_text())
    assets = {asset['id']: args.composition.parent / asset['path'] for asset in document['assets']}
    asset_hashes = {key: hashlib.sha256(path.read_bytes()).hexdigest() for key, path in assets.items()}
    manifest = json.loads((args.composition.parent / 'assets.json').read_text())
    if any(asset_hashes[key] != manifest['sha256'][path.name] for key, path in assets.items()):
        parser.error('prepared asset differs from the showcase manifest')
    args.output.mkdir(parents=True, exist_ok=True)
    samples = []
    # A complete warm-up of each variant is discarded. Each measured sample
    # launches a fresh context/process and performs full audio/video encoding.
    schedule = [('warmup', 'lookup'), ('warmup', 'scalar')]
    for index in range(args.runs):
        modes = ('lookup', 'scalar') if index % 2 == 0 else ('scalar', 'lookup')
        schedule += [(str(index + 1), mode) for mode in modes]
    for index, mode in schedule:
        name = f'{mode}-{index}'
        profile = args.output / f'{name}.json'
        command = [str((args.optimized if mode == 'lookup' else args.reference).resolve()), 'benchmark', str(args.composition.resolve()),
                   '--iterations', '1', '--output', str(profile.resolve())]
        start = time.perf_counter()
        with (args.output / f'{name}.log').open('w') as log:
            subprocess.run(['/usr/bin/time', '-l' if platform.system() == 'Darwin' else '-v', *command],
                           stdout=log, stderr=log, check=True)
        wall = time.perf_counter() - start
        log = (args.output / f'{name}.log').read_text()
        pattern = (r'(\d+)\s+maximum resident set size' if platform.system() == 'Darwin'
                   else r'Maximum resident set size \(kbytes\):\s*(\d+)')
        match = re.search(pattern, log)
        if not match:
            raise RuntimeError('cannot parse process peak RSS')
        rss = int(match[1]) * (1 if platform.system() == 'Darwin' else 1024)
        data = json.loads(profile.read_text())['iterations'][0]
        if data['frames_encoded'] != 540 or data['audio_samples_encoded'] != 864000:
            raise RuntimeError('incomplete showcase render')
        graphics = data['graphics']
        if graphics['bounded_operations'] == 0:
            raise RuntimeError('bounded graphics was not active')
        print(f'{name}: {wall:.2f}s, {540/wall:.2f}fps, {rss/1024**2:.1f}MiB', flush=True)
        if index == 'warmup':
            continue
        samples.append(dict(mode=mode, run=int(index), wall_seconds=wall, peak_rss_bytes=rss, profile=data))
        report = dict(platform=platform.platform(), processor=platform.processor(),
                      composition_sha256=digest, asset_sha256=asset_hashes,
                      executable_sha256={name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in [('scalar', args.reference), ('lookup', args.optimized)]},
                      method='Two discarded full-encode warmups; fresh-process alternating samples; identical bounded graphics/caches/media fusion; CPU only; binaries differ by CHROMA_REFERENCE_OUTPUT_CONVERSION.',
                      samples=samples)
        (args.output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    for mode in ('lookup', 'scalar'):
        values = [s['wall_seconds'] for s in samples if s['mode'] == mode]
        print(f'{mode}: median {statistics.median(values):.2f}s, range {min(values):.2f}–{max(values):.2f}s', flush=True)


if __name__ == '__main__':
    main()
