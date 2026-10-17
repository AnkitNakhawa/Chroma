"""Check animated geometry, lowered plans, and MP4 output through the actual CLI."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

chroma, ffmpeg, ffprobe = sys.argv[1:]


def run(*args):
    result = subprocess.run([str(arg) for arg in args], capture_output=True)
    if result.returncode:
        raise AssertionError(result.stderr.decode(errors="replace"))
    return result.stdout


with tempfile.TemporaryDirectory(prefix="chroma-motion-") as directory:
    root = Path(directory)
    composition = root / "motion.json"
    output = root / "motion.mp4"
    document = dict(schema_version=1, canvas=dict(width=64, height=32,
                    frame_rate=dict(numerator=30000, denominator=1001), duration_frames=60),
                    layers=[dict(id="background", type="solid", start_frame=0, end_frame=60,
                                 color=[0, 0, 0, 1]),
                            dict(id="moving", type="shape", shape="rectangle", start_frame=5,
                                 end_frame=55, bounds=[4, 12, 8, 4], fill=[1, 0, 0, 1], effects=[
                                     dict(type="scale", origin=[8, 14], keyframes=[
                                         dict(frame=0, value=[1, 1]), dict(frame=25, value=[2, 2])]),
                                     dict(type="rotation", origin=[8, 14], keyframes=[
                                         dict(frame=0, value=0), dict(frame=25, value=90)]),
                                     dict(type="translation", keyframes=[
                                         dict(frame=0, value=[0, 0]), dict(frame=25, value=[24, 0])])])])
    composition.write_text(json.dumps(document))
    run(chroma, "validate", composition)
    # At local frame 25 the declared scale, rotation, translation combine into
    # one exact matrix: x' = -2y + 60, y' = 2x - 2.
    plan = json.loads(run(chroma, "inspect-plan", composition, "--frame", "30"))
    encoded_plan = json.dumps(plan)
    assert "animated_" not in encoded_plan
    def matrices(value):
        if isinstance(value, dict):
            if value.get("op") == "transform_2d":
                yield value["matrix"]
            for child in value.values():
                yield from matrices(child)
        elif isinstance(value, list):
            for child in value:
                yield from matrices(child)
    assert list(matrices(plan)) == [[0, -2, 60, 2, 0, -2]], plan
    assert '"effects_frame_varying": true' in encoded_plan
    run(chroma, "render", composition, "--output", output)
    metadata = json.loads(run(ffprobe, "-v", "error", "-show_streams", "-of", "json", output))
    video = metadata["streams"][0]
    assert video["codec_name"] == "h264"
    assert int(video["nb_frames"]) == 60
    assert video["avg_frame_rate"] == "30000/1001"
    assert abs(float(video["duration"]) - 2.002) < 1e-6
    pixels = run(ffmpeg, "-v", "error", "-i", output, "-f", "rawvideo", "-pix_fmt", "rgb24", "-")
    frame_bytes = 64 * 32 * 3
    assert len(pixels) == 60 * frame_bytes
    def red_pixels(frame):
        data = pixels[frame * frame_bytes:(frame + 1) * frame_bytes]
        return [(index % 64, index // 64) for index in range(64 * 32)
                if data[index * 3] > 150 and data[index * 3 + 1] < 50 and data[index * 3 + 2] < 50]
    assert not red_pixels(4) and not red_pixels(55)
    for frame, center_x in [(5, 7.5), (17, 19.02), (30, 31.5), (54, 31.5)]:
        red = red_pixels(frame)
        assert red, frame
        assert abs(sum(p[0] for p in red) / len(red) - center_x) < 0.75, (frame, red)
        assert abs(sum(p[1] for p in red) / len(red) - 13.5) < 0.75
    # A doubled, quarter-turned 8x4 card is 8x16 at the final keyframe.
    final = red_pixels(30)
    assert abs(max(p[0] for p in final) - min(p[0] for p in final) + 1 - 8) <= 2
    assert abs(max(p[1] for p in final) - min(p[1] for p in final) + 1 - 16) <= 2

print("Animation CLI integration passed: geometry, layer timing, matrix lowering, rational-rate MP4")
