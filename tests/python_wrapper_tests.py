"""Integration tests: ctypes calls the real shared library, never a mock renderer."""
import argparse
import ctypes
import gc
import json
from pathlib import Path
import sys
import tempfile
import threading
from dataclasses import FrozenInstanceError
import unittest
import weakref

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python/src'))
from chroma_render import Engine, Composition, Capabilities, ChromaError, InvalidCompositionError, Status, LibraryLoadError
from chroma_render import _native, RenderPhase, RenderCancelledError

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

    def video_engine(self):
        engine = self.engine()
        if not engine.capabilities & Capabilities.FFMPEG:
            self.skipTest('FFmpeg disabled')
        return engine

    def test_progress_snapshots_and_completed_return(self):
        engine = self.video_engine()
        events = []
        with engine.parse_composition(document()) as scene, tempfile.TemporaryDirectory() as root:
            def progress(event):
                events.append(event)
                self.assertEqual(threading.get_ident(), caller)
                return event.phase == RenderPhase.COMPLETED
            caller = threading.get_ident()
            profile = engine.render(scene, Path(root) / 'video.mp4', progress=progress)
            self.assertEqual(profile['frames_encoded'], 6)
        self.assertEqual(events[0].phase, RenderPhase.PREPARING)
        self.assertEqual(events[-1].phase, RenderPhase.COMPLETED)
        self.assertIn(RenderPhase.FINALIZING, [e.phase for e in events])
        self.assertEqual(events[-1].frames_encoded, 6)
        self.assertEqual(events[-1].audio_samples_encoded, 0)
        self.assertEqual([e.frames_encoded for e in events], sorted(e.frames_encoded for e in events))
        self.assertEqual([e.elapsed_nanoseconds for e in events], sorted(e.elapsed_nanoseconds for e in events))
        with self.assertRaises(FrozenInstanceError): events[0].total_frames = 0

    def test_callback_cancellation_and_exceptions_preserve_destination(self):
        engine = self.video_engine()
        class CallbackFailure(Exception): pass
        failure = CallbackFailure('original callback failure')
        interrupt = KeyboardInterrupt('callback interrupt')
        with engine.parse_composition(document()) as scene, tempfile.TemporaryDirectory() as root:
            output = Path(root) / 'video.mp4'
            def fails(event): raise failure
            def interrupts(event): raise interrupt
            for callback, expected in ((lambda event: event.frames_encoded >= 2, RenderCancelledError),
                                       (fails, CallbackFailure), (interrupts, KeyboardInterrupt),
                                       (lambda event: 'wrong return type', TypeError)):
                output.write_bytes(b'original')
                with self.assertRaises(expected) as caught:
                    engine.render(scene, output, progress=callback)
                if expected is CallbackFailure: self.assertIs(caught.exception, failure)
                if expected is KeyboardInterrupt: self.assertIs(caught.exception, interrupt)
                self.assertEqual(output.read_bytes(), b'original')
                self.assertEqual(list(Path(root).glob('.chroma-render-*')), [])
            engine.render(scene, output)  # Engine remains usable after failures.
            self.assertNotEqual(output.read_bytes(), b'original')

    def test_completed_exception_reports_already_published_output(self):
        engine = self.video_engine()
        with engine.parse_composition(document()) as scene, tempfile.TemporaryDirectory() as root:
            output = Path(root) / 'video.mp4'
            def progress(event):
                if event.phase == RenderPhase.COMPLETED: raise ValueError('completed observer failed')
            with self.assertRaisesRegex(ValueError, 'completed observer failed'):
                engine.render(scene, output, progress=progress)
            self.assertGreater(output.stat().st_size, 0)
            self.assertEqual(list(Path(root).glob('.chroma-render-*')), [])

    def test_thread_cancellation_and_pinned_lifetimes(self):
        engine = self.video_engine()
        entered, cancelled = threading.Event(), threading.Event()
        errors = []
        with engine.parse_composition(document()) as scene, engine.cancellation_token() as token, tempfile.TemporaryDirectory() as root:
            output = Path(root) / 'video.mp4'
            def worker():
                try:
                    if not entered.wait(5): raise AssertionError('callback not reached')
                    for handle in (engine, scene, token):
                        with self.assertRaises(RuntimeError): handle.close()
                    with self.assertRaises(RuntimeError): engine.render_frame(scene)
                    token.cancel()
                except BaseException as error: errors.append(error)
                finally: cancelled.set()
            thread = threading.Thread(target=worker)
            thread.start()
            def progress(event):
                entered.set()
                if not cancelled.wait(5): raise AssertionError('GIL was not released')
            try:
                with self.assertRaises(RenderCancelledError):
                    engine.render(scene, output, progress=progress, cancel_token=token)
            finally:
                entered.set(); thread.join(6)
            self.assertFalse(thread.is_alive())
            self.assertFalse(errors, errors)
            self.assertFalse(output.exists())
            # Cancellation is latched, and can be requested repeatedly.
            token.cancel()
            with self.assertRaises(RenderCancelledError): engine.render(scene, output, cancel_token=token)
            engine.render(scene, output)

    def test_callback_reentrancy_fails_safely(self):
        engine = self.video_engine()
        with engine.parse_composition(document()) as scene, tempfile.TemporaryDirectory() as root:
            output = Path(root) / 'video.mp4'
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                engine.render(scene, output, progress=lambda event: engine.clear_caches())
            self.assertFalse(output.exists())
            with self.assertRaises(TypeError): engine.render(scene, output, progress=3)
            with self.assertRaises(TypeError): engine.render(scene, output, cancel_token=object())
            with engine.cancellation_token() as token: pass
            with self.assertRaises(RuntimeError): engine.render(scene, output, cancel_token=token)
            with self.assertRaises(RuntimeError): token.cancel()


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *rest])
