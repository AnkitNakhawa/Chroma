"""Independent FFmpeg quality checks. Thresholds apply equally to every renderer mode."""
import array
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import sys
import wave

from quality_fixtures import run

RGB_MAE_LIMIT = 3.0
RGB_PSNR_MIN = 30.0
AUDIO_RMSE_LIMIT = .015
AUDIO_CORRELATION_MIN = .98


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def probe(ffprobe, path):
    return json.loads(run([ffprobe, '-v', 'error', '-count_frames', '-show_streams', '-of', 'json', path]))


def video_metadata(info, document):
    streams = [s for s in info['streams'] if s['codec_type'] == 'video']
    check(len(streams) == 1, 'expected exactly one video stream')
    stream = streams[0]
    canvas = document['canvas']
    rate = Fraction(canvas['frame_rate']['numerator'], canvas['frame_rate']['denominator'])
    frames = canvas['duration_frames']
    check((stream['codec_name'], stream['width'], stream['height'], stream['pix_fmt']) ==
          ('h264', canvas['width'], canvas['height'], 'yuv420p'), 'video format changed')
    check(int(stream['nb_read_frames']) == frames, 'encoded frame count changed')
    check(Fraction(stream['avg_frame_rate']) == rate, 'encoded frame rate changed')
    check(abs(float(stream['duration']) - float(frames / rate)) < 1e-6, 'video duration changed')


def audio_metadata(info, document):
    streams = [s for s in info['streams'] if s['codec_type'] == 'audio']
    check(len(streams) == 1, 'expected exactly one audio stream')
    stream = streams[0]
    canvas = document['canvas']
    count = canvas['duration_frames'] * canvas['frame_rate']['denominator'] * 48000 // canvas['frame_rate']['numerator']
    check((stream['codec_name'], int(stream['sample_rate']), stream['channels']) == ('aac', 48000, 2), 'audio format changed')
    check(abs(float(stream['duration']) - count / 48000) <= 1 / 48000 + 1e-6, 'audio duration changed')
    return count


def timestamps(ffprobe, path, document):
    data = json.loads(run([ffprobe, '-v', 'error', '-select_streams', 'v:0', '-show_frames',
                          '-show_entries', 'frame=best_effort_timestamp_time', '-of', 'json', path]))['frames']
    canvas = document['canvas']
    check(len(data) == canvas['duration_frames'], 'presentation timestamp count changed')
    for index, frame in enumerate(data):
        expected = index * canvas['frame_rate']['denominator'] / canvas['frame_rate']['numerator']
        check(abs(float(frame['best_effort_timestamp_time']) - expected) < 1e-6,
              f'presentation timestamp changed at frame {index}')


def video_digest(ffmpeg, path, expected_frames):
    raw = run([ffmpeg, '-v', 'error', '-xerror', '-i', path, '-map', '0:v:0', '-an', '-f', 'framemd5', '-'])
    frames = [line.strip() for line in raw.splitlines() if line and not line.startswith(b'#')]
    check(len(frames) == expected_frames, 'decoded video frame count changed')
    return hashlib.sha256(b'\n'.join(frames)).hexdigest()


def decoded_frame(ffmpeg, path, frame):
    return run([ffmpeg, '-v', 'error', '-xerror', '-i', path, '-vf', f'select=eq(n\\,{frame})',
                '-frames:v', '1', '-pix_fmt', 'rgb24', '-f', 'rawvideo', '-'])


def rgb_fidelity(native_ppm, rgb):
    header = native_ppm.split(b'\n', 3)
    check(header[0] == b'P6' and header[2] == b'255', 'invalid native snapshot')
    expected = header[3]
    check(len(rgb) == len(expected) and len(rgb) > 0, 'decoded RGB size changed')
    total = squared = 0
    for a, b in zip(expected, rgb):
        difference = abs(a - b)
        total += difference
        squared += difference * difference
    mae = total / len(rgb)
    psnr = 10 * math.log10(255 ** 2 / (squared / len(rgb))) if squared else 100.0
    check(mae < RGB_MAE_LIMIT, f'RGB MAE {mae:.3f} exceeds {RGB_MAE_LIMIT}/255')
    check(psnr >= RGB_PSNR_MIN, f'RGB PSNR {psnr:.3f} below {RGB_PSNR_MIN} dB')
    return dict(mae=mae, psnr_db=psnr)


def pcm(ffmpeg, path, count):
    return run([ffmpeg, '-v', 'error', '-xerror', '-i', path, '-map', '0:a:0',
                '-af', f'atrim=end_sample={count}', '-f', 's16le', '-'])


def audio_fidelity(raw, source_path, count, gain):
    actual = array.array('h', raw)
    with wave.open(str(source_path), 'rb') as stream:
        check((stream.getnchannels(), stream.getframerate(), stream.getsampwidth()) == (2, 48000, 2), 'invalid audio oracle')
        expected = array.array('h', stream.readframes(count))
    if sys.byteorder != 'little':
        actual.byteswap()
        expected.byteswap()
    check(len(actual) == len(expected) == count * 2, 'decoded audio sample count changed')
    errors = energy_actual = energy_expected = cross = 0.0
    for x, y in zip(actual, expected):
        a, b = x / 32768, y * gain / 32768
        errors += (a - b) ** 2
        energy_actual += a * a
        energy_expected += b * b
        cross += a * b
    rmse = (errors / len(actual)) ** .5
    correlation = cross / math.sqrt(energy_actual * energy_expected) if energy_actual else 0
    check(rmse < AUDIO_RMSE_LIMIT, f'audio RMSE {rmse:.5f} exceeds {AUDIO_RMSE_LIMIT}')
    check(correlation >= AUDIO_CORRELATION_MIN, f'audio correlation {correlation:.5f} below {AUDIO_CORRELATION_MIN}')
    peak = max(abs(v) for v in actual) / 32768
    check(0 < peak < .99, 'muted or clipped audio')
    return dict(rmse=rmse, correlation=correlation, peak=peak, samples=count)


def encoded_case(args, name, composition, snapshots):
    document = json.loads(composition.read_text())
    case = args.output / name
    case.mkdir(exist_ok=True)
    outputs = {}
    for mode in ('bounded', 'full-frame'):
        path = case / f'{mode}.mp4'
        run([args.chroma, 'render', composition, '--output', path, '--graphics-mode', mode])
        info = probe(args.ffprobe, path)
        video_metadata(info, document)
        count = audio_metadata(info, document)
        timestamps(args.ffprobe, path, document)
        digest = video_digest(args.ffmpeg, path, document['canvas']['duration_frames'])
        raw_audio = pcm(args.ffmpeg, path, count)
        track = document['audio_tracks'][0]
        asset = next(a for a in document['assets'] if a['id'] == track['asset'])
        fidelity = audio_fidelity(raw_audio, composition.parent / asset['path'], count, track['gain'])
        outputs[mode] = dict(video_digest=digest, audio_digest=hashlib.sha256(raw_audio).hexdigest(), audio=fidelity)
    check(outputs['bounded']['video_digest'] == outputs['full-frame']['video_digest'], 'decoded video differs between renderer modes')
    check(outputs['bounded']['audio_digest'] == outputs['full-frame']['audio_digest'], 'decoded audio differs between renderer modes')
    quality = {}
    for frame in snapshots:
        native = case / f'reference-{frame}.ppm'
        run([args.chroma, 'frame', composition, '--frame', str(frame), '--output', native, '--graphics-mode', 'full-frame'])
        quality[str(frame)] = rgb_fidelity(native.read_bytes(), decoded_frame(args.ffmpeg, case / 'bounded.mp4', frame))
    return dict(modes=outputs, rgb=quality)


def negative_controls(args):
    """Use real corrupted encodes to prove quality failures cannot pass silently."""
    composition = args.output / 'fractional.json'
    document = json.loads(composition.read_text())
    clean = args.output / 'fractional' / 'bounded.mp4'
    degraded = args.output / 'fractional' / 'degraded.mp4'
    run([args.ffmpeg, '-v', 'error', '-y', '-i', clean, '-vf', 'eq=brightness=0.2',
         '-af', 'volume=0', '-c:v', 'libx264', '-crf', '40', '-c:a', 'aac', degraded])
    middle = document['canvas']['duration_frames'] // 2
    native = (args.output / 'fractional' / f'reference-{middle}.ppm').read_bytes()
    failures = {}
    def rejected(name, operation):
        try:
            operation()
        except AssertionError as error:
            failures[name] = str(error)
        else:
            raise AssertionError(f'negative control unexpectedly passed: {name}')
    rejected('degraded_video', lambda: rgb_fidelity(native, decoded_frame(args.ffmpeg, degraded, middle)))
    count = document['canvas']['duration_frames'] * document['canvas']['frame_rate']['denominator'] * 48000 // document['canvas']['frame_rate']['numerator']
    rejected('muted_audio', lambda: audio_fidelity(pcm(args.ffmpeg, degraded, count),
             args.output / 'assets' / 'fractional.wav', count, .7))
    short = args.output / 'fractional' / 'truncated.mp4'
    run([args.ffmpeg, '-v', 'error', '-y', '-i', clean, '-map', '0:v:0', '-frames:v', str(document['canvas']['duration_frames'] - 1),
         '-c:v', 'copy', '-an', short])
    rejected('truncated_video', lambda: video_metadata(probe(args.ffprobe, short), document))
    # Both-mode equality must itself reject a divergence, independent of fidelity limits.
    rejected('renderer_video_mismatch', lambda: check(
        video_digest(args.ffmpeg, clean, document['canvas']['duration_frames']) == video_digest(args.ffmpeg, degraded, document['canvas']['duration_frames']),
        'decoded video differs between renderer modes'))
    return failures
