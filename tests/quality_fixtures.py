"""Offline quality corpus. Reuses showcase design/timing, never downloads stock media."""
import array
import copy
import importlib.util
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import wave


def run(command, **kwargs):
    result = subprocess.run([str(x) for x in command], capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f'{command[0]} failed: {result.stderr.decode(errors="replace")}')
    return result.stdout


def scale_scene(document, factor):
    doc = copy.deepcopy(document)
    doc['canvas']['width'] = round(doc['canvas']['width'] * factor)
    doc['canvas']['height'] = round(doc['canvas']['height'] * factor)
    for layer in doc['layers']:
        if 'bounds' in layer:
            layer['bounds'] = [v * factor for v in layer['bounds']]
        if 'corner_radius' in layer:
            layer['corner_radius'] *= factor
        if 'stroke' in layer:
            layer['stroke']['width'] *= factor
        if layer['type'] == 'text':
            layer['position'] = [v * factor for v in layer['position']]
            layer['font_size'] *= factor
        for effect in layer.get('effects', []):
            if 'origin' in effect:
                effect['origin'] = [v * factor for v in effect['origin']]
            if effect['type'] == 'translation':
                if 'value' in effect:
                    effect['value'] = [v * factor for v in effect['value']]
                for key in effect.get('keyframes', []):
                    key['value'] = [v * factor for v in key['value']]
    return doc


def footage(path, ffmpeg, width, height, frames, rate):
    # Low-frequency moving RGB gradients exercise color/decode while keeping
    # 4:2:0 chroma loss distinguishable from an actual rendering regression.
    raw = bytearray()
    for frame in range(frames):
        for y in range(36):
            for x in range(64):
                wave_value = math.sin((x + frame * .7) / 16 + y / 25)
                raw.extend((round(65 + x * 1.4 + wave_value * 12),
                            round(85 + y * 2 + wave_value * 15),
                            round(110 + x * .6 + y * .5)))
    run([ffmpeg, '-v', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'rgb24',
         '-video_size', '64x36', '-framerate', rate, '-i', '-',
         '-vf', f'scale={width}:{height}', '-c:v', 'ffv1', '-pix_fmt', 'bgr0', path], input=bytes(raw))


def tone(path, seconds):
    rate = 48000
    samples = array.array('h')
    for i in range(math.ceil(seconds * rate)):
        t = i / rate
        envelope = min(t / .03, 1, max((seconds - t) / .03, 0))
        samples.extend((round(8000 * envelope * math.sin(2 * math.pi * 440 * t)),
                        round(6000 * envelope * math.sin(2 * math.pi * 660 * t))))
    if sys.byteorder != 'little':
        samples.byteswap()
    with wave.open(str(path), 'wb') as stream:
        stream.setparams((2, 2, rate, 0, 'NONE', 'not compressed'))
        stream.writeframes(samples.tobytes())


def fixtures(root, ffmpeg, font):
    assets = root / 'assets'
    assets.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(font, assets / 'font.ttf')
    spec = importlib.util.spec_from_file_location('showcase', Path(__file__).resolve().parents[1] / 'examples/showcase/generate.py')
    showcase = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(showcase)
    doc = scale_scene(showcase.composition(), 1 / 6)
    for asset in doc['assets']:
        asset['path'] = {'ocean': 'assets/ocean.mkv', 'bold': 'assets/font.ttf',
                         'regular': 'assets/font.ttf', 'music': 'assets/music.wav'}[asset['id']]
    footage(assets / 'ocean.mkv', ffmpeg, 320, 180, 540, '30')
    showcase.music(assets / 'music.wav')
    documents = [('showcase-proxy', doc, [12, 72, 150, 180, 240, 318, 350, 390, 444, 470, 510, 539])]
    for name, width, height, frames, numerator, denominator in [
            ('fractional', 160, 90, 36, 30000, 1001), ('full-hd', 1920, 1080, 12, 30, 1)]:
        ratio = width / 320
        footage(assets / f'{name}.mkv', ffmpeg, width, height, frames, f'{numerator}/{denominator}')
        tone(assets / f'{name}.wav', frames * denominator / numerator)
        simple = dict(schema_version=1, canvas=dict(width=320, height=180,
                      frame_rate=dict(numerator=numerator, denominator=denominator), duration_frames=frames),
                      assets=[dict(id='clip', type='video', path=f'assets/{name}.mkv'),
                              dict(id='font', type='font', path='assets/font.ttf'),
                              dict(id='sound', type='audio', path=f'assets/{name}.wav')], layers=[
            dict(id='base', type='video', asset='clip', start_frame=0, end_frame=frames, fit='cover'),
            dict(id='card', type='shape', shape='rounded_rectangle', bounds=[24.25, 20.5, 210, 120], corner_radius=14,
                 fill=[.03, .06, .09, .85], stroke=dict(color=[.5, .7, .6, .7], width=1.25), start_frame=0, end_frame=frames,
                 effects=[dict(type='translation', keyframes=[dict(frame=0, value=[-8.25, 3.5], easing='ease_out'),
                          dict(frame=frames-1, value=[10.5, -2.25])]), dict(type='opacity', value=.9)]),
            dict(id='title', type='text', font='font', text='Chroma / ffi Á', font_size=24, position=[32.5, 83.75],
                 color=[.9, .95, .9, 1], start_frame=0, end_frame=frames,
                 effects=[dict(type='rotation', origin=[140, 80], keyframes=[dict(frame=0, value=-4), dict(frame=frames-1, value=4)])]),
            dict(id='mask', type='shape', shape='ellipse', bounds=[240, 110, 50, 40], fill=[.15, .6, .4, .8],
                 start_frame=0, end_frame=frames, effects=[dict(type='gaussian_blur', radius=.7)])],
            audio_tracks=[dict(id='tone', asset='sound', start_frame=0, end_frame=frames, gain=.7)])
        documents.append((name, scale_scene(simple, ratio), [0, frames // 2, frames - 1]))
    result = []
    for name, document, snapshots in documents:
        path = root / f'{name}.json'
        path.write_text(json.dumps(document, indent=2) + '\n')
        result.append((name, path, snapshots))
    return result
