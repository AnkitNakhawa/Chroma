"""Offline composition and original-score checks; no stock downloads in CI."""
import array
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('showcase', root/'examples/showcase/generate.py')
showcase = importlib.util.module_from_spec(spec)
spec.loader.exec_module(showcase)
doc = showcase.composition()
assert doc['canvas'] == dict(width=1920, height=1080, duration_frames=540,
                             frame_rate=dict(numerator=30, denominator=1))
ids = [layer['id'] for layer in doc['layers']]
assert len(ids) == len(set(ids))
assert {'text', 'shape', 'video', 'solid'} <= {layer['type'] for layer in doc['layers']}
assert all(0 <= layer['start_frame'] < layer['end_frame'] <= 540 for layer in doc['layers'])
assert all(any(layer['start_frame'] <= frame < layer['end_frame'] for layer in doc['layers']) for frame in range(540))
assert doc['audio_tracks'][0]['end_frame'] == 540
# Check the actual public CLI parser, including every effect/keyframe and asset reference.
with tempfile.TemporaryDirectory(prefix='chroma-showcase-') as temporary:
    folder = Path(temporary)
    composition = folder/'composition.json'
    composition.write_text(json.dumps(doc))
    subprocess.run([sys.argv[1], 'validate', str(composition)], check=True)
    audio = folder/'music.wav'
    peak = showcase.music(audio)
    assert .2 < peak < .8
    with wave.open(str(audio), 'rb') as stream:
        assert (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(), stream.getnframes()) == (2, 2, 48000, 864000)
        samples = array.array('h', stream.readframes(stream.getnframes()))
    if sys.byteorder != 'little': samples.byteswap()
    assert samples[0] == samples[1] == 0
    assert max(abs(v) for v in samples[-960:]) < 200
    assert sum(v*v for v in samples) / len(samples) > 100000
    assert any(samples[i] != samples[i+1] for i in range(0, 10000, 2))
print('Showcase composition, timing, original score and CLI validation passed')
