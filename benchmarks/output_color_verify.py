#!/usr/bin/env python3
"""Compare complete decoded media from controlled builds of one revision."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from quality_media import (probe, video_metadata, audio_metadata, timestamps,
                           video_digest, pcm)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('reference', 'optimized', 'composition', 'ffmpeg', 'ffprobe', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--experiment', choices=['output-color', 'composition-passes'], default='output-color')
    args = parser.parse_args()
    reference_name, optimized_name = ('scalar', 'lookup') if args.experiment == 'output-color' else ('materialized', 'optimized')
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'equivalence.json').write_text(json.dumps({'passed': False}) + '\n')
    scene = args.composition.resolve()
    document = json.loads(scene.read_text())
    results = {}
    for name, executable in [(reference_name, args.reference), (optimized_name, args.optimized)]:
        video = (args.output / (name + '.mp4')).resolve()
        subprocess.run([str(executable.resolve()), 'render', str(scene), '--output', str(video)], check=True)
        info = probe(str(args.ffprobe), str(video))
        video_metadata(info, document)
        samples = audio_metadata(info, document)
        timestamps(str(args.ffprobe), str(video), document)
        results[name] = dict(video_digest=video_digest(str(args.ffmpeg), str(video),
            document['canvas']['duration_frames']),
            audio_digest=hashlib.sha256(pcm(str(args.ffmpeg), str(video), samples)).hexdigest(),
            frames=document['canvas']['duration_frames'], audio_samples=samples)
    if results[reference_name] != results[optimized_name]:
        raise AssertionError('decoded media differs: ' + json.dumps(results))
    report = dict(passed=True, comparison='exact decoded video frames/timestamps and audio PCM',
                  composition_sha256=hashlib.sha256(scene.read_bytes()).hexdigest(), results=results)
    (args.output / 'equivalence.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
