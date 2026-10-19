"""CLI contract: structured progress, signal cancellation, atomic outputs, pipe closure."""
import json
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time
import wave

cli, ffprobe = sys.argv[1:3]

def run(*args):
    return subprocess.run([cli, *map(str, args)], capture_output=True, text=True, timeout=30)

def progress_lines(text):
    return [json.loads(line) for line in text.splitlines()]

with tempfile.TemporaryDirectory(prefix='chroma progress ') as temp:
    root = Path(temp)
    source = root / 'scene.json'
    output = root / 'result.mp4'
    report = root / 'cache.json'
    with wave.open(str(root / 'tone.wav'), 'wb') as stream:
        stream.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        stream.writeframes(struct.pack('<hh', 1000, -1000) * 96000)
    doc = dict(schema_version=1, canvas=dict(width=160, height=90,
               frame_rate=dict(numerator=30000, denominator=1001), duration_frames=36),
               layers=[], assets=[dict(id='tone', type='audio', path='tone.wav')],
               audio_tracks=[dict(id='sound', asset='tone', start_frame=0, end_frame=36)])
    source.write_text(json.dumps(doc))
    output.write_text('original')
    result = run('render', source, '--output', output, '--progress', 'json', '--cache-report', report)
    assert result.returncode == 0, result.stderr
    events = progress_lines(result.stdout)
    assert len(events) == 39 and all(p['event'] == 'progress' for p in events)
    assert [events[i]['phase'] for i in (0, -2, -1)] == ['preparing', 'finalizing', 'completed']
    assert events[0]['frames_encoded'] == events[0]['audio_samples_encoded'] == 0
    assert events[-1]['frames_encoded'] == 36 and events[-1]['audio_samples_encoded'] == 57658
    assert all(p['total_frames'] == 36 and p['total_audio_samples'] == 57658 for p in events)
    for key in ('frames_encoded', 'audio_samples_encoded', 'elapsed_nanoseconds'):
        assert all(a[key] <= b[key] for a, b in zip(events, events[1:])), key
    metadata = json.loads(subprocess.check_output([ffprobe, '-v', 'error', '-count_frames',
        '-show_streams', '-of', 'json', str(output)], text=True))
    assert next(s for s in metadata['streams'] if s['codec_type'] == 'video')['nb_read_frames'] == '36'
    assert json.loads(report.read_text())
    # Existing human stdout remains unchanged; text progress stays on stderr.
    for mode in ('none', 'text'):
        result = run('render', source, '--output', output, '--progress', mode)
        assert result.returncode == 0 and result.stdout.startswith('rendered 36 frames'), result.stderr
        assert ('preparing:' in result.stderr) == (mode == 'text')
        if mode == 'text': assert 'completed: 36/36' in result.stderr
    for extra in (['--progress', 'bad'], ['--progress'], ['--progress', 'json', '--progress', 'text']):
        output.write_text('original')
        result = run('render', source, '--output', output, *extra)
        assert result.returncode == 2 and output.read_text() == 'original'
    # Much longer than the timeout: cancellation must stop real work, not await EOF.
    doc['canvas'].update(width=640, height=360, duration_frames=10000)
    doc['audio_tracks'] = []
    source.write_text(json.dumps(doc))
    for sig, expected in ((signal.SIGINT, 130), (signal.SIGTERM, 143)):
        for existing in (False, True):
            output.unlink(missing_ok=True)
            if existing: output.write_text('original')
            report.write_text('old report')
            proc = subprocess.Popen([cli, 'render', str(source), '--output', str(output),
                        '--progress', 'json', '--cache-report', str(report)],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            seen = []
            try:
                # A blocking read is guarded by the CTest timeout and child cleanup.
                while True:
                    ready, _, _ = select.select([proc.stdout], [], [], 10)
                    # TextIO may already have buffered lines; use one initial ready
                    # check, then readline while the renderer is actively producing.
                    if not ready and not seen: raise AssertionError('no progress before timeout')
                    line = proc.stdout.readline()
                    assert line, 'renderer exited before cancellation'
                    seen.append(json.loads(line))
                    if seen[-1].get('frames_encoded', 0) >= 2: break
                start = time.monotonic()
                proc.send_signal(sig)
                stdout, stderr = proc.communicate(timeout=10)
                assert time.monotonic() - start < 10
                assert proc.returncode == expected, (proc.returncode, stderr)
                seen += progress_lines(stdout)
                assert seen[-1] == dict(event='cancelled')
                assert not any(e.get('phase') == 'completed' for e in seen)
                assert 'render cancelled' in stderr
                assert output.read_text() == 'original' if existing else not output.exists()
                assert report.read_text() == 'old report'
                assert not list(root.glob('.chroma-render-*'))
            finally:
                if proc.poll() is None: proc.kill()
                proc.wait()
    # A progress consumer closing its pipe must not leave temporary media behind.
    if hasattr(signal, 'SIGPIPE'):
        output.write_text('original')
        proc = subprocess.Popen([cli, 'render', str(source), '--output', str(output), '--progress', 'json'],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            assert proc.stdout.readline()
            proc.stdout.close()
            assert proc.wait(timeout=10) == 130
            assert output.read_text() == 'original' and not list(root.glob('.chroma-render-*'))
        finally:
            if proc.poll() is None: proc.kill()
            proc.wait()
            proc.stderr.close()
    # Signals also work without enabling progress: wait until output is reserved.
    output.write_text('original')
    proc = subprocess.Popen([cli, 'render', str(source), '--output', str(output)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 10
        while not list(root.glob('.chroma-render-*')):
            assert proc.poll() is None and time.monotonic() < deadline
            time.sleep(.01)
        proc.send_signal(signal.SIGINT)
        stdout, stderr = proc.communicate(timeout=10)
        assert proc.returncode == 130 and not stdout and b'cancelled' in stderr
        assert output.read_text() == 'original' and not list(root.glob('.chroma-render-*'))
    finally:
        if proc.poll() is None: proc.kill()
        proc.wait()
print('progress, audio counters, SIGINT/SIGTERM, pipe closure, preservation and cleanup passed')
