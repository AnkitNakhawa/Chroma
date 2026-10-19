"""Render relative image/audio assets through a plain C consumer and inspect MP4."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

consumer, ffmpeg, ffprobe = sys.argv[1:]


def run(*args):
    result = subprocess.run([str(arg) for arg in args], capture_output=True)
    if result.returncode:
        raise AssertionError(result.stderr.decode(errors="replace"))
    return result.stdout


with tempfile.TemporaryDirectory(prefix="chroma-c-abi-") as directory:
    root = Path(directory) / "assets café"
    root.mkdir()
    run(ffmpeg, "-v", "error", "-f", "lavfi", "-i", "color=c=red:size=32x32",
        "-frames:v", "1", "-threads", "1", root / "red.png")
    with wave.open(str(root / "sound.wav"), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(b"\0\0" * 48000)
    source = dict(schema_version=1, canvas=dict(width=32, height=32,
                  frame_rate=dict(numerator=30000, denominator=1001), duration_frames=12),
                  assets=[dict(id="image", type="image", path="red.png"),
                          dict(id="sound", type="audio", path="sound.wav")],
                  layers=[dict(id="image", type="image", asset="image", start_frame=0,
                               end_frame=12, effects=[dict(type="translation", keyframes=[
                                   dict(frame=0, value=[0, 0]), dict(frame=11, value=[8, 0])])])],
                  audio_tracks=[dict(id="audio", asset="sound", start_frame=0, end_frame=12,
                                     gain=0.5, source_start_frame=1)])
    composition = root / "composition.json"
    composition.write_text(json.dumps(source))
    reference = None
    for disabled in [False, True]:
        output = root / ("disabled.mp4" if disabled else "cached.mp4")
        report = json.loads(run(consumer, composition, output, *(["--no-cache"] if disabled else [])))
        assert report["frames_encoded"] == 12
        assert report["audio_samples_encoded"] == 19219
        image_cache = report["cache"]["images"]
        assert image_cache["misses"] == (12 if disabled else 1)
        assert image_cache["hits"] == (0 if disabled else 11)
        metadata = json.loads(run(ffprobe, "-v", "error", "-show_streams", "-of", "json", output))
        streams = {stream["codec_type"]: stream for stream in metadata["streams"]}
        assert streams["video"]["codec_name"] == "h264"
        assert int(streams["video"]["nb_frames"]) == 12
        assert streams["audio"]["codec_name"] == "aac"
        assert int(streams["audio"]["duration_ts"]) == 19219
        pixels = run(ffmpeg, "-v", "error", "-i", output, "-map", "0:v:0", "-f", "rawvideo", "-pix_fmt", "rgb24", "-")
        if reference is None:
            reference = pixels
        assert reference == pixels
    # Errors return through the C boundary, including preparation failures after
    # the output container is opened. Partial media must be removed by the core.
    (root / "sound.wav").unlink()
    result = subprocess.run([consumer, str(composition), str(root / "failed.mp4")], capture_output=True)
    assert result.returncode != 0
    assert not (root / "failed.mp4").exists()
print("C API integration passed: C99 consumer, UTF-8 paths, assets, animation, audio, cache controls, MP4 timing")
