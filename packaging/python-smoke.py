#!/usr/bin/env python3
"""Run inside an isolated venv with the packaged wheel and relocated SDK."""
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

import chroma_render
from chroma_render import Engine, RenderPhase, RenderCancelledError, ChromaError, Status

package, fixture = (Path(p).resolve() for p in sys.argv[1:3])
assert Path(chroma_render.__file__).is_relative_to(Path(sys.prefix)), chroma_render.__file__
assert importlib.metadata.version('chroma-render') == chroma_render.__version__
assert not importlib.metadata.requires('chroma-render')
with tempfile.TemporaryDirectory(prefix='chroma python smoke ') as root:
    root = Path(root)
    cli, python = root / 'cli.mp4', root / 'python.mp4'
    # The quickstart's installed example and Python calls need no input assets.
    with Engine() as engine:
        with engine.load_composition(package / 'share/chroma/examples/quickstart.json') as scene:
            report = scene.preflight(for_video=True)
            assert report['ready'] and report['assets_checked'] == 0
            progress = []
            profile = engine.render(scene, root / 'quickstart.mp4', progress=progress.append)
            assert profile['frames_encoded'] == 90
            assert progress[-1].phase == RenderPhase.COMPLETED

    subprocess.run([package / 'bin/chroma', 'render', fixture / 'smoke.json', '--output', cli], check=True)
    events = []
    with Engine() as engine, engine.load_composition(fixture / 'smoke.json') as scene:
        terminal = engine.render_report(scene,root/'terminal.mp4')
        assert terminal['status']=='succeeded' and terminal['output']['published']
        assert terminal['output']['metadata']['frame_count']==36
        before = engine.cache_usage
        report = engine.preflight(scene)
        assert report['ready'] and report['assets_checked'] > 0
        assert engine.cache_usage == before
        bad = json.loads((fixture / 'missing-preflight.json').read_text())
        with engine.parse_composition(bad, base_directory=fixture) as invalid:
            terminal=engine.render_report(invalid,root/'terminal-failure.mp4')
            assert terminal['status']=='failed' and not terminal['output']['published']
            assert terminal['diagnostics'][0]['code']=='asset.missing'
            report = invalid.preflight()
            assert not report['ready'] and any(d['code'] == 'asset.missing' for d in report['diagnostics'])
            failed = root / 'preflight-failure.mp4'
            failed.write_bytes(b'original')
            try:
                engine.render(invalid, failed)
                raise AssertionError('missing input did not stop rendering')
            except ChromaError as error:
                assert error.status == Status.RENDER and 'asset.missing' in str(error)
            assert failed.read_bytes() == b'original' and not list(root.glob('.chroma-render-*'))
        frame = engine.render_frame(scene)
        profile = engine.render(scene, python, progress=events.append)
        assert profile['frames_encoded'] == 36
        assert events[0].phase == RenderPhase.PREPARING
        assert events[-1].phase == RenderPhase.COMPLETED
        assert events[-1].frames_encoded == 36
        assert events[-1].audio_samples_encoded == events[-1].total_audio_samples > 0
        assert [e.audio_samples_encoded for e in events] == sorted(e.audio_samples_encoded for e in events)
        with engine.cancellation_token() as token:
            cancelled = root / 'cancelled.mp4'
            cancelled.write_bytes(b'original')
            def progress(event):
                if event.frames_encoded >= 2:
                    thread = threading.Thread(target=token.cancel)
                    thread.start(); thread.join(timeout=5)
                    assert not thread.is_alive()
            try:
                engine.render(scene, cancelled, progress=progress, cancel_token=token)
                raise AssertionError('render was not cancelled')
            except RenderCancelledError:
                pass
            assert cancelled.read_bytes() == b'original'
            assert not list(root.glob('.chroma-render-*'))
    assert len(frame.pixels) == frame.width * frame.height * 4
    for stream, format_, extra in [('0:v:0', 'rawvideo', ['-pix_fmt', 'rgba']), ('0:a:0', 's16le', [])]:
        def decode(path):
            return subprocess.check_output([package / 'libexec/chroma/ffmpeg', '-v', 'error',
                '-i', path, '-map', stream, *extra, '-f', format_, '-'])
        reference, actual = decode(cli), decode(python)
        assert reference and reference == actual, f'Python/CLI decoded {stream} differs'
print('PASS: installed wheel, progress/audio counters, cross-thread cancellation, owned frames, identical decoded CLI/Python RGBA/PCM')
