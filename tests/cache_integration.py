"""Exercise CLI budgets, retained-byte counters, eviction and output equivalence."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

chroma, ffmpeg = sys.argv[1:3]
font = Path(sys.argv[3]) if len(sys.argv) > 3 else None


def run(*args, ok=True):
    result = subprocess.run([str(arg) for arg in args], capture_output=True)
    if ok and result.returncode:
        raise AssertionError(result.stderr.decode(errors="replace"))
    if not ok:
        assert result.returncode == 2, (result.returncode, result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="chroma-cache-cli-") as directory:
    root = Path(directory)
    run(ffmpeg, "-v", "error", "-f", "lavfi", "-i", "color=c=red:size=32x32",
        "-frames:v", "1", "-threads", "1", root / "a.png")
    for name in ["b", "c"]:
        shutil.copyfile(root / "a.png", root / (name + ".png"))
    assets = [dict(id=name, type="image", path=name + ".png") for name in "abc"]
    if font:
        for name in "abc":
            shutil.copyfile(font, root / (name + ".ttf"))
            assets.append(dict(id="font" + name, type="font", path=name + ".ttf"))
    layers = [dict(id="image" + str(index), type="image", asset=name,
                   start_frame=0, end_frame=6, effects=[dict(type="opacity", value=0.3)])
              for index, name in enumerate("abac")]
    if font:
        layers += [dict(id="text" + str(index), type="text", font="font" + name,
                        text="Hi", font_size=16, position=[4, 20], color=[1, 1, 1, 1],
                        start_frame=0, end_frame=6) for index, name in enumerate("abac")]
    document = dict(schema_version=1, canvas=dict(width=64, height=32,
                    frame_rate=dict(numerator=30, denominator=1), duration_frames=6),
                    assets=assets, layers=layers)
    composition = root / "composition.json"
    report = root / "cache.json"
    frame = root / "frame.ppm"
    seed = dict(document, layers=[layers[0]] + ([layers[4]] if font else []))
    composition.write_text(json.dumps(seed))
    run(chroma, "frame", composition, "--frame", "0", "--output", frame, "--cache-report", report)
    usage = json.loads(report.read_text())
    image_bytes = usage["images"]["resident_bytes"]
    text_bytes = usage["text"]["resident_bytes"]
    assert image_bytes >= 32 * 32 * 16
    assert not font or text_bytes > 0
    composition.write_text(json.dumps(document))
    modes = [([], "default"), (["--image-cache-bytes", "0", "--text-cache-bytes", "0"], "disabled"),
             (["--image-cache-bytes", str(image_bytes * 2), "--text-cache-bytes", str(text_bytes * 2)], "bounded")]
    reference = None
    for options, name in modes:
        output = root / (name + ".mp4")
        run(chroma, "render", composition, "--output", output, *options, "--cache-report", report)
        cache = json.loads(report.read_text())
        assert cache["policy"] == "lru" and cache["accounting"] == "retained_resource_bytes"
        for kind in ["images"] + (["text"] if font else []):
            value = cache[kind]
            assert value["resident_bytes"] <= value["budget_bytes"]
            assert value["peak_resident_bytes"] <= value["budget_bytes"]
            assert value["load_failures"] == 0
            if name == "disabled":
                assert value["hits"] == value["entries"] == value["resident_bytes"] == 0
                assert value["misses"] == value["bypasses"] == 24, value
            elif name == "default":
                assert value["misses"] == 3 and value["hits"] == 21, value
                assert value["entries"] == 3 and value["evictions"] == 0
            else:
                assert value["misses"] == 13 and value["hits"] == 11, value
                assert value["entries"] == 2 and value["evictions"] == 11, value
        pixels = run(ffmpeg, "-v", "error", "-i", output, "-f", "rawvideo", "-pix_fmt", "rgb24", "-")
        if reference is None:
            reference = pixels
        assert pixels == reference, name
        run(chroma, "frame", composition, "--frame", "0", "--output", frame, *options)
        frame_data = frame.read_bytes()
        if name == "default":
            reference_frame = frame_data
        assert frame_data == reference_frame
        run(chroma, "benchmark", composition, "--iterations", "2", "--output", report, *options)
        iterations = json.loads(report.read_text())["iterations"]
        assert len(iterations) == 2
        # Each benchmark iteration is an independent job, not an accumulating cache.
        assert iterations[0]["cache"] == cache and iterations[1]["cache"] == cache
    # Zero/oversized entries never retain data; flags reject malformed/ambiguous budgets.
    for bad in ["-1", "+1", "1.5", "", " 3", "3x", "18446744073709551616"]:
        run(chroma, "render", composition, "--output", root / "bad.mp4", "--image-cache-bytes", bad, ok=False)
    run(chroma, "frame", composition, "--frame", "0", "--output", frame,
        "--text-cache-bytes", "0", "--text-cache-bytes", "0", ok=False)
    run(chroma, "render", composition, "--output", root / "bad.mp4", "--image-cache-bytes", ok=False)
    run(chroma, "render", composition, "--output", root / "bad.mp4", "--unknown", "0", ok=False)
    run(chroma, "render", composition, "--output", root / "bad.mp4",
        "--cache-report", "one.json", "--cache-report", "two.json", ok=False)
    run(chroma, "benchmark", composition, "--iterations", "1", "--output", report,
        "--cache-report", "one.json", ok=False)
    # Full uint64 budgets round-trip as integers, without floating-point rounding.
    run(chroma, "frame", composition, "--frame", "0", "--output", frame,
        "--image-cache-bytes", "18446744073709551615", "--cache-report", report)
    assert json.loads(report.read_text())["images"]["budget_bytes"] == 18446744073709551615
    # Report path aliases must not replace successfully rendered media.
    existing = root / "existing.mp4"
    existing.write_bytes(b"preserve")
    result = subprocess.run([chroma, "render", str(composition), "--output", str(existing),
                             "--cache-report", str(existing)], capture_output=True)
    assert result.returncode != 0 and existing.read_bytes() == b"preserve"
    alias = root / "alias.json"
    os.link(existing, alias)
    result = subprocess.run([chroma, "render", str(composition), "--output", str(existing),
                             "--cache-report", str(alias)], capture_output=True)
    assert result.returncode != 0 and existing.read_bytes() == b"preserve"

print("Cache CLI integration passed: budgets, exact LRU counters, frame/video equivalence, benchmark snapshots")
