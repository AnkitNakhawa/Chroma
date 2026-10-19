"""Real-file preflight integration, including fail-before-output and media boundaries."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

parser = argparse.ArgumentParser()
parser.add_argument('--chroma', required=True)
parser.add_argument('--ffmpeg')
parser.add_argument('--font')
args = parser.parse_args()


def run(*command):
    return subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=40)


def document(frames=60):
    return dict(schema_version=1, canvas=dict(width=32, height=18,
        frame_rate=dict(numerator=30, denominator=1), duration_frames=frames), assets=[], layers=[])


with tempfile.TemporaryDirectory(prefix='chroma preflight café ') as temp:
    root = Path(temp)
    source, output = root / 'scene.json', root / 'output.mp4'

    def check(doc, ready, code=None, severity='error', inputs_only=True):
        source.write_text(json.dumps(doc), encoding='utf-8')
        extra = ['--inputs-only'] if inputs_only else []
        result = run(args.chroma, 'preflight', source, '--json', *extra)
        report = json.loads(result.stdout)
        assert report['ready'] is ready and result.returncode == (0 if ready else 4), (result, report)
        assert report['schema_version'] == 1
        if code:
            assert any(d['code'] == code and d['severity'] == severity for d in report['diagnostics']), report
        return report

    def media_doc(kind, path, frames=60, start=0):
        doc = document(frames)
        doc['assets'] = [dict(id='source', type=kind, path=str(path))]
        if kind == 'audio':
            doc['audio_tracks'] = [dict(id='track', asset='source', start_frame=0, end_frame=frames, source_start_frame=start)]
        elif kind == 'font':
            doc['layers'] = [dict(id='title', type='text', font='source', text='Hello world', font_size=14,
                position=[0,14], color=[1,1,1,1], start_frame=0, end_frame=frames)]
        else:
            layer = dict(id='layer', type=kind, asset='source', start_frame=0, end_frame=frames)
            if kind == 'video': layer['source_start_frame'] = start
            doc['layers'] = [layer]
        return doc

    check(document(), True)
    unused = document()
    unused['assets'] = [dict(id='unused', type='image', path='missing.png')]
    report = check(unused, True, 'asset.missing', 'warning')
    assert report['assets_checked'] == 1
    for kind in ('image','video','audio','font'):
        check(media_doc(kind, 'missing.file'), False, 'asset.missing')
    check(media_doc('image', 'valid.png\0suffix'), False, 'asset.invalid_path')
    (root / 'empty').touch()
    check(media_doc('font','empty'), False, 'asset.empty')
    check(media_doc('font','.'), False, 'asset.not_regular')
    (root / 'broken-link').symlink_to(root / 'absent')
    check(media_doc('image','broken-link'), False, 'asset.missing')
    if os.geteuid() != 0:
        locked = root / 'locked'
        locked.write_text('contents'); locked.chmod(0)
        try: check(media_doc('image','locked'), False, 'asset.unreadable')
        finally: locked.chmod(0o600)

    # Collect independent late-layer errors rather than failing on the first.
    aggregate = media_doc('video', 'absent-video', frames=120)
    aggregate['layers'][0].update(start_frame=90)
    aggregate['assets'].append(dict(id='font',type='font',path='absent-font'))
    aggregate['layers'].append(dict(id='title',type='text',font='font',text='Hello',font_size=14,
        position=[0,14],color=[1,1,1,1],start_frame=100,end_frame=120))
    report = check(aggregate, False, 'asset.missing')
    assert {d['asset_id'] for d in report['diagnostics']} == {'source','font'}
    assert run(args.chroma,'validate',source).returncode == 0  # Structural validation stays offline.
    if args.ffmpeg:
        output.write_bytes(b'original')
        result = run(args.chroma,'render',source,'--output',output,'--progress','json')
        assert result.returncode == 4 and 'asset.missing' in result.stderr, result
        events = [json.loads(line) for line in result.stdout.splitlines()]
        assert [e['phase'] for e in events] == ['preparing'], events
        assert output.read_bytes() == b'original' and not list(root.glob('.chroma-render-*'))

    for bad in ('--bad','--json'):
        assert run(args.chroma,'preflight',source,'--json',bad).returncode == 2
    result = run(args.chroma,'preflight',root/'missing.json','--json')
    assert result.returncode == 4 and json.loads(result.stdout)['diagnostics'][0]['code'] == 'composition.unreadable'
    source.write_text('{')
    result = run(args.chroma,'preflight',source,'--json')
    assert result.returncode == 3 and not json.loads(result.stdout)['ready']
    bad_timeline = document()
    bad_timeline['layers'] = [dict(id='solid',type='solid',color=[1,1,1,1],start_frame=0,end_frame=61)]
    source.write_text(json.dumps(bad_timeline))
    result = run(args.chroma,'preflight',source,'--json')
    assert result.returncode == 3 and any(d['path'].startswith('/layers/0') for d in json.loads(result.stdout)['diagnostics'])
    odd = document(); odd['canvas']['width'] = 31
    check(odd, True)
    check(odd, False, 'output.even_dimensions', inputs_only=False)
    check(media_doc('video','missing',start=2**63-1), False, 'timeline.video_range_overflow')

    (root/'bad-font').write_bytes(b'not a font')
    font_report = check(media_doc('font','bad-font'), False)
    assert any(d['code'] in ('font.incompatible','font.backend_unavailable') for d in font_report['diagnostics'])
    if args.font:
        check(media_doc('font',args.font), True)

    if args.ffmpeg:
        def ffmpeg(*parameters):
            result = run(args.ffmpeg,'-v','error','-y',*parameters)
            assert result.returncode == 0, result.stderr
        with wave.open(str(root/'tone.wav'),'wb') as stream:
            stream.setparams((2,2,48000,0,'NONE','not compressed'))
            stream.writeframes(struct.pack('<hh',1000,-1000)*48000)
        ffmpeg('-f','lavfi','-i','color=c=red:size=32x18:rate=30:duration=2','-c:v','ffv1',root/'clip.mkv')
        ffmpeg('-f','lavfi','-i','color=c=red:size=32x18','-frames:v',1,'-threads',1,root/'image.png')
        check(media_doc('video','clip.mkv'), True)  # Last request at 59/30 < 2 seconds.
        check(media_doc('video','clip.mkv',start=1), False, 'timeline.video_out_of_range')
        check(media_doc('video','clip.mkv',frames=1,start=60), False, 'timeline.video_out_of_range')
        check(media_doc('image','image.png'), True)
        check(media_doc('audio','tone.wav',frames=30), True)  # Exclusive sample end may equal EOF.
        check(media_doc('audio','tone.wav',frames=31), False, 'timeline.audio_out_of_range')
        check(media_doc('audio','tone.wav',frames=1,start=30), False, 'timeline.audio_out_of_range')
        check(media_doc('video','tone.wav'), False, 'media.incompatible')
        check(media_doc('audio','clip.mkv'), False, 'media.incompatible')
        check(media_doc('image','tone.wav'), False, 'media.incompatible')
        (root/'corrupt.mkv').write_bytes((root/'clip.mkv').read_bytes()[:80])
        check(media_doc('video','corrupt.mkv'), False, 'media.incompatible')
        ffmpeg('-i',root/'tone.wav','-c:a','aac',root/'tone.m4a')
        check(media_doc('audio','tone.m4a',frames=90), True, 'timeline.audio_duration_estimate', 'warning')
        ffmpeg('-f','lavfi','-i','color=size=32x18:rate=30:duration=1','-c:v','ffv1',
               '-vf','setparams=color_trc=smpte2084:color_primaries=bt2020:colorspace=bt2020nc',root/'hdr.mkv')
        check(media_doc('video','hdr.mkv',frames=1), False, 'media.hdr_unsupported')
        tiny = media_doc('audio','tone.wav',frames=1)
        tiny['canvas']['frame_rate'] = dict(numerator=192000,denominator=1)
        check(tiny, False, 'timeline.audio_empty')
        # Both last-frame and audio range errors must be found before encoding.
        invalid = media_doc('video','clip.mkv',start=1)
        invalid['assets'].append(dict(id='sound',type='audio',path='tone.wav'))
        invalid['audio_tracks'] = [dict(id='music',asset='sound',start_frame=0,end_frame=60)]
        report = check(invalid, False)
        assert {'timeline.video_out_of_range','timeline.audio_out_of_range'} <= {d['code'] for d in report['diagnostics']}
print('PASS: preflight aggregation, real assets/fonts/media, timeline boundaries, early output preservation')
