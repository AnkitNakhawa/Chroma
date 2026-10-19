#!/usr/bin/env python3
"""Render a composition with progress; optionally cancel after N frames."""
import argparse
import sys
from chroma_render import Engine, RenderCancelledError

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('composition')
parser.add_argument('output')
parser.add_argument('--library')
parser.add_argument('--cancel-after', type=int)
args = parser.parse_args()


def progress(event):
    print(f'{event.phase.name}: {event.frames_encoded}/{event.total_frames} frames', file=sys.stderr)
    return args.cancel_after is not None and event.frames_encoded >= args.cancel_after


try:
    with Engine(library_path=args.library) as engine, engine.load_composition(args.composition) as scene:
        engine.render(scene, args.output, progress=progress)
except (RenderCancelledError, KeyboardInterrupt):
    print('Render cancelled', file=sys.stderr)
    sys.exit(130)
