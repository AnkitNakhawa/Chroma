"""Exercise the real CLI, codecs and MP4 timing with independent FFmpeg readers."""
import array
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
import wave

chroma, ffmpeg, ffprobe = sys.argv[1:]


def run(*args, ok=True):
    result = subprocess.run([str(arg) for arg in args], capture_output=True)
    if ok and result.returncode:
        raise AssertionError(result.stderr.decode(errors="replace"))
    if not ok:
        assert result.returncode != 0, "expected render failure"
    return result.stdout


def probe(path):
    return json.loads(run(ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", path))


def samples_at(frame, numerator, denominator):
    return (frame * 48000 * denominator * 2 + numerator) // (2 * numerator)


with tempfile.TemporaryDirectory(prefix="chroma-audio-integration-") as directory:
    root = Path(directory)
    source = array.array("h")
    for sample in range(96000):
        amplitude = 0.2 if sample < 48000 else 0.6
        source.append(round(amplitude * 32767 * math.sin(2 * math.pi * 440 * sample / 48000)))
        source.append(round(amplitude * 0.5 * 32767 * math.sin(2 * math.pi * 660 * sample / 48000)))
    with wave.open(str(root / "sound.wav"), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(48000)
        out.writeframes(source.tobytes())

    for numerator, denominator in [(30, 1), (30000, 1001)]:
        label = str(numerator)
        # 29 frames of source after a one-second trim also fits the 29.97 fixture.
        tracks = [dict(id="trim", asset="sound", start_frame=15, end_frame=44,
                       source_start_frame=30, gain=0.5),
                  dict(id="overlap", asset="sound", start_frame=30, end_frame=59, gain=1)]
        document = dict(schema_version=1,
                        canvas=dict(width=32, height=18,
                                    frame_rate=dict(numerator=numerator, denominator=denominator),
                                    duration_frames=90),
                        assets=[dict(id="sound", type="audio", path="sound.wav")],
                        layers=[dict(id="background", type="solid", start_frame=0,
                                     end_frame=90, color=[0, 0, 1, 1]),
                                dict(id="marker", type="solid", start_frame=45,
                                     end_frame=90, color=[1, 0, 0, 1])],
                        audio_tracks=tracks)
        composition = root / (label + ".json")
        output = root / (label + ".mp4")
        composition.write_text(json.dumps(document))
        run(chroma, "render", composition, "--output", output)
        metadata = probe(output)
        streams = {stream["codec_type"]: stream for stream in metadata["streams"]}
        audio, video = streams["audio"], streams["video"]
        total = samples_at(90, numerator, denominator)
        assert audio["codec_name"] == "aac"
        assert audio["sample_rate"] == "48000" and audio["channels"] == 2
        assert audio["time_base"] == "1/48000"
        assert int(audio["duration_ts"]) == total, audio
        assert abs(float(audio["start_time"])) < 1e-6, audio
        assert video["codec_name"] == "h264" and int(video["nb_frames"]) == 90
        assert abs(float(video["duration"]) - 90 * denominator / numerator) < 1e-6
        assert abs(float(metadata["format"]["duration"]) - 90 * denominator / numerator) <= 0.001
        assert abs(float(video["start_time"])) < 1e-6

        decoded = array.array("f")
        decoded.frombytes(run(ffmpeg, "-v", "error", "-i", output, "-map", "0:a:0",
                              "-f", "f32le", "-acodec", "pcm_f32le", "-"))
        assert total <= len(decoded) // 2 < total + 1024
        expected = [0.0] * (total * 2)
        for track in tracks:
            start = samples_at(track["start_frame"], numerator, denominator)
            end = samples_at(track["end_frame"], numerator, denominator)
            trim = samples_at(track.get("source_start_frame", 0), numerator, denominator)
            for sample in range(start, end):
                for channel in range(2):
                    expected[sample * 2 + channel] += source[(trim + sample - start) * 2 + channel] / 32768 * track["gain"]
        # Compare interior windows independently for silence, trim/gain, overlap,
        # second track alone, and trailing silence. AAC is intentionally lossy.
        for begin, end in [(2000, 20000), (26000, 45000), (51000, 68000), (73000, 91000), (100000, 140000)]:
            error = math.sqrt(sum((decoded[i] - expected[i]) ** 2
                                  for i in range(begin * 2, end * 2)) / ((end - begin) * 2))
            assert error < 0.015, (label, begin, error)
        # Video frame marker remains aligned with the audio timeline.
        pixels = run(ffmpeg, "-v", "error", "-i", output, "-vf", "select=eq(n\\,45)",
                     "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-")
        assert pixels[0] > 240 and pixels[2] < 15
        report = root / "benchmark.json"
        run(chroma, "benchmark", composition, "--iterations", "1", "--output", report)
        telemetry = json.loads(report.read_text())
        assert telemetry["iterations"][0]["audio_samples_encoded"] == total
        assert telemetry["iterations"][0]["milliseconds"]["audio"] > 0

        # No tracks must continue producing a video-only file.
        document.pop("audio_tracks")
        composition.write_text(json.dumps(document))
        run(chroma, "render", composition, "--output", output)
        assert [s["codec_type"] for s in probe(output)["streams"]] == ["video"]

        # Source EOF after output creation must clean up the partial MP4.
        document["audio_tracks"] = [dict(id="short", asset="sound", start_frame=0, end_frame=90)]
        composition.write_text(json.dumps(document))
        bad = root / "failed.mp4"
        run(chroma, "render", composition, "--output", bad, ok=False)
        assert not bad.exists()

    # A single video frame exercises AAC priming and a short final audio block.
    document["canvas"]["frame_rate"] = dict(numerator=30, denominator=1)
    document["canvas"]["duration_frames"] = 1
    document["layers"] = []
    document["audio_tracks"] = [dict(id="short", asset="sound", start_frame=0, end_frame=1)]
    composition.write_text(json.dumps(document))
    run(chroma, "render", composition, "--output", output)
    audio = next(s for s in probe(output)["streams"] if s["codec_type"] == "audio")
    assert int(audio["duration_ts"]) == 1600, audio

print("Audio CLI integration passed: sample content, MP4/AAC timing, rational rates, short tails, failure cleanup")
