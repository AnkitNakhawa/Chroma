"""Integration tests: ctypes calls the real shared library, never a mock renderer."""
import argparse
import ctypes
import gc
import json
from pathlib import Path
import sys
import tempfile
import unittest
import weakref

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python/src'))
from chroma_render import Engine, Composition, Capabilities, ChromaError, InvalidCompositionError, Status, LibraryLoadError
from chroma_render import _native

parser = argparse.ArgumentParser()
parser.add_argument('--library', required=True)
args, rest = parser.parse_known_args()


def document(frames=6, width=32, height=18):
    return dict(schema_version=1, canvas=dict(width=width, height=height,
        frame_rate=dict(numerator=30000, denominator=1001), duration_frames=frames),
        layers=[dict(id='red', type='solid', color=[1, 0, 0, 1], start_frame=0, end_frame=frames)])


class WrapperTests(unittest.TestCase):
    def engine(self, **kwargs):
        engine = Engine(library_path=args.library, **kwargs)
        self.addCleanup(engine.close)
        return engine

    def test_real_abi_layouts(self):
        self.assertEqual([ctypes.sizeof(c) for c in (_native.EngineOptions, _native.CompositionInfo,
            _native.FrameInfo, _native.NativeProgress, _native.RenderOptions)], [24, 48, 32, 48, 32])

    def test_frame_lives_after_all_native_handles_close(self):
        engine = self.engine()
        scene = engine.parse_composition(document(width=2, height=2))
        self.assertEqual(scene.info.width, 2)
        pixels = engine.render_frame(scene).pixels
        scene.close(); engine.close(); gc.collect()
        self.assertTrue(pixels.readonly)
        self.assertEqual(list(pixels), [1., 0., 0., 1.] * 4)
        with self.assertRaises(TypeError): pixels[0] = 0

    def test_independent_composition_and_cache_controls(self):
        engine = self.engine(image_cache_bytes=123, text_cache_bytes=456)
        scene = engine.parse_composition(document())
        self.addCleanup(scene.close)
        self.assertEqual(scene.info.frame_rate_denominator, 1001)
        self.assertEqual(scene.info.duration_frames, 6)
        self.assertIsInstance(scene.inspect_plan(0), dict)
        self.assertIsInstance(scene.inspect_plan(0, optimized=True), dict)
        engine.close()
        other = self.engine()
        self.assertEqual(other.render_frame(scene).width, 32)
        other.set_cache_budgets(13, 17); other.clear_caches()
        self.assertEqual(other.cache_usage['images']['budget_bytes'], 13)
        self.assertEqual(other.cache_usage['text']['budget_bytes'], 17)

    def test_unicode_paths_and_validation_diagnostics(self):
        engine = self.engine()
        with tempfile.TemporaryDirectory(prefix='chroma python ') as root:
            path = Path(root) / 'scène 🎬.json'
            path.write_text(json.dumps(document()), encoding='utf-8')
            with engine.load_composition(path) as scene:
                self.assertEqual(engine.render_frame(scene).height, 18)
        with self.assertRaises(InvalidCompositionError) as caught:
            engine.parse_composition('{}')
        self.assertEqual(caught.exception.status, Status.INVALID_COMPOSITION)
        self.assertTrue(caught.exception.diagnostics)
        with self.assertRaises(ChromaError) as caught:
            engine.load_composition('/chroma/nonexistent/scene.json')
        self.assertEqual(caught.exception.status, Status.IO)

    def test_close_and_invalid_python_arguments(self):
        engine = self.engine()
        scene = engine.parse_composition(document())
        self.addCleanup(scene.close)
        for frame in (-1, 2**63):
            with self.assertRaises(ValueError): engine.render_frame(scene, frame)
        for frame in (True, 1.5):
            with self.assertRaises(TypeError): engine.render_frame(scene, frame)
        with self.assertRaises(ValueError): engine.set_cache_budgets(-1, 0)
        with self.assertRaises(ValueError): engine.set_cache_budgets(2**64, 0)
        with self.assertRaises(ValueError): engine.parse_composition(b'{}\0')
        with self.assertRaises(ValueError): engine.load_composition('x\0y')
        with self.assertRaises(TypeError): Composition()
        with self.assertRaises(TypeError): engine.render_frame(object())
        scene.close(); scene.close()
        with self.assertRaises(RuntimeError): engine.render_frame(scene)
        with self.assertRaises(RuntimeError): _ = scene.info
        engine.close(); engine.close()
        with self.assertRaises(RuntimeError): engine.parse_composition(document())

    def test_gc_owns_destruction_and_releases_library(self):
        engine = Engine(library_path=args.library)
        scene = engine.parse_composition(document())
        native = weakref.ref(engine._native)
        engine_ref, scene_ref = weakref.ref(engine), weakref.ref(scene)
        del engine; gc.collect()
        self.assertIsNone(engine_ref())
        self.assertIsNotNone(native())
        del scene; gc.collect()
        self.assertIsNone(scene_ref())
        self.assertIsNone(native())

    def test_load_failure_is_actionable(self):
        with self.assertRaises(LibraryLoadError):
            Engine(library_path='/chroma/nonexistent/library.so')

    def test_real_video_or_unavailable_backend(self):
        engine = self.engine()
        with engine.parse_composition(document()) as scene, tempfile.TemporaryDirectory() as root:
            output = Path(root) / 'résultat.mp4'
            if engine.capabilities & Capabilities.FFMPEG:
                result = engine.render(scene, output)
                self.assertEqual(result['frames_encoded'], 6)
                self.assertGreater(output.stat().st_size, 0)
            else:
                with self.assertRaises(ChromaError) as caught: engine.render(scene, output)
                self.assertEqual(caught.exception.status, Status.UNAVAILABLE)
                self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *rest])
