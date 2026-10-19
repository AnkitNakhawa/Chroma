"""Chroma video renderer's dependency-free Python binding to the stable C API."""
from __future__ import annotations

from contextlib import contextmanager, ExitStack
import ctypes as C
from dataclasses import dataclass
from enum import IntEnum, IntFlag
import json
import os
import threading
import weakref

from . import _native as N
from ._native import LibraryLoadError

__version__ = '0.1.0'
__all__ = ['Engine', 'Composition', 'Frame', 'CompositionInfo', 'Capabilities', 'Status',
           'ChromaError', 'InvalidCompositionError', 'LibraryLoadError', 'CancellationToken',
           'RenderCancelledError', 'RenderPhase', 'RenderProgress']


class Status(IntEnum):
    OK = 0
    INVALID_ARGUMENT = 1
    UNSUPPORTED_ABI = 2
    INVALID_COMPOSITION = 3
    OUT_OF_RANGE = 4
    UNAVAILABLE = 5
    OUT_OF_MEMORY = 6
    IO = 7
    RENDER = 8
    INTERNAL = 9
    CANCELLED = 10


class Capabilities(IntFlag):
    CPU_RENDER = 1
    FFMPEG = 2
    TEXT = 4
    RENDER_CONTROL = 8


class ChromaError(RuntimeError):
    def __init__(self, status: int, message: str):
        self.status = Status(status)
        super().__init__(message)


class RenderCancelledError(ChromaError):
    """Rendering stopped before publishing the output."""


class InvalidCompositionError(ChromaError):
    def __init__(self, message, diagnostics):
        self.diagnostics = diagnostics
        super().__init__(Status.INVALID_COMPOSITION, message)


def _message(library):
    return library.api.chroma_last_error().decode('utf-8', errors='replace')


def _check(library, status, message=None):
    if status:
        error = RenderCancelledError if status == Status.CANCELLED else ChromaError
        raise error(status, message if message is not None else _message(library))


def _integer(value, name, maximum, minimum=0):
    # ctypes otherwise silently wraps negative or overflowing integers.
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f'{name} must be an integer')
    if not minimum <= value <= maximum:
        raise ValueError(f'{name} must be between {minimum} and {maximum}')
    return value


def _path(value):
    text = os.fspath(value)
    if not isinstance(text, str):
        raise TypeError('paths must be str or a PathLike returning str')
    if not text or '\0' in text:
        raise ValueError('path must be nonempty and contain no NUL')
    return text.encode('utf-8')


def _owned_json(library, handle):
    try:
        data = library.api.chroma_string_data(handle)
        length = library.api.chroma_string_size(handle)
        return json.loads(C.string_at(data, length).decode('utf-8'))
    finally:
        library.api.chroma_string_destroy(handle)


def _release(library, name, handle):
    # Retaining the library here prevents a finalizer from outliving its DLL.
    getattr(library.api, name)(handle)


class _Handle:
    def _init_handle(self, library, handle, destroy):
        self._native, self._handle = library, handle
        self._lock, self._active = threading.RLock(), 0
        self._finalizer = weakref.finalize(self, _release, library, destroy, handle)

    @contextmanager
    def _borrow(self, *, exclusive=False):
        with self._lock:
            if not self._handle:
                raise RuntimeError(f'{type(self).__name__} is closed')
            if exclusive and self._active:
                raise RuntimeError(f'{type(self).__name__} is busy; concurrent/reentrant use is not allowed')
            self._active += 1
            handle = self._handle
        try:
            yield handle
        finally:
            with self._lock:
                self._active -= 1

    @property
    def closed(self):
        with self._lock:
            return not bool(self._handle)

    def close(self):
        with self._lock:
            if self._active:
                raise RuntimeError(f'{type(self).__name__} is in use; cannot close it')
            if self._handle:
                self._finalizer()
                self._handle = None

    def __enter__(self):
        with self._borrow():
            return self

    def __exit__(self, *unused):
        self.close()


def _matching(library, value, kind):
    if not isinstance(value, kind):
        raise TypeError(f'expected {kind.__name__}')
    if value._native is not library:
        raise ValueError('handles must belong to the same loaded Chroma library')


@dataclass(frozen=True)
class CompositionInfo:
    width: int
    height: int
    schema_version: int
    frame_rate_numerator: int
    frame_rate_denominator: int
    start_frame: int
    duration_frames: int


@dataclass(frozen=True)
class Frame:
    """Python-owned native-endian float32 linear premultiplied RGBA bytes."""
    width: int
    height: int
    data: bytes
    pixel_format: int = 1

    @property
    def pixels(self):
        """Read-only flat float memoryview, retained independently of native handles."""
        return memoryview(self.data).cast('f')

    @property
    def row_stride_bytes(self):
        return self.width * 16


class RenderPhase(IntEnum):
    PREPARING = 0
    RENDERING = 1
    FINALIZING = 2
    COMPLETED = 3


@dataclass(frozen=True)
class RenderProgress:
    phase: RenderPhase
    frames_encoded: int
    total_frames: int
    audio_samples_encoded: int
    total_audio_samples: int
    elapsed_nanoseconds: int


class CancellationToken(_Handle):
    """Thread-safe, latched cancellation request. Create a new token to reset."""
    def __init__(self, *, library_path=None):
        library = N.load_library(library_path)
        if not library.has_render_control:
            raise LibraryLoadError('This library lacks render control support')
        handle = N.Handle()
        _check(library, library.api.chroma_cancel_token_create(C.byref(handle)))
        self._init_handle(library, handle, 'chroma_cancel_token_destroy')

    def cancel(self):
        with self._borrow() as handle:
            self._native.api.chroma_cancel_token_request(handle)


class Composition(_Handle):
    def __init__(self, *args, **kwargs):
        raise TypeError('Use Engine.parse_composition() or Engine.load_composition()')

    @classmethod
    def _from_handle(cls, library, handle):
        instance = object.__new__(cls)
        instance._init_handle(library, handle, 'chroma_composition_destroy')
        return instance

    @property
    def info(self):
        with self._borrow() as handle:
            info = N.CompositionInfo()
            _check(self._native, self._native.api.chroma_composition_get_info(handle, C.byref(info), C.sizeof(info)))
            return CompositionInfo(*(getattr(info, name) for name, _ in N.CompositionInfo._fields_[1:]))

    def inspect_plan(self, frame=0, *, optimized=False):
        _integer(frame, 'frame', 2**63 - 1)
        if not isinstance(optimized, bool):
            raise TypeError('optimized must be bool')
        with self._borrow() as handle:
            result = N.Handle()
            _check(self._native, self._native.api.chroma_composition_inspect_plan(
                handle, frame, optimized, C.byref(result)))
            return _owned_json(self._native, result)


class Engine(_Handle):
    def __init__(self, *, library_path=None, image_cache_bytes=64 * 1024 * 1024,
                 text_cache_bytes=16 * 1024 * 1024):
        _integer(image_cache_bytes, 'image_cache_bytes', 2**64 - 1)
        _integer(text_cache_bytes, 'text_cache_bytes', 2**64 - 1)
        library = N.load_library(library_path)
        options = N.EngineOptions(C.sizeof(N.EngineOptions), 1, image_cache_bytes, text_cache_bytes)
        handle = N.Handle()
        _check(library, library.api.chroma_engine_create(C.byref(options), C.byref(handle)))
        self._init_handle(library, handle, 'chroma_engine_destroy')

    @property
    def library_path(self):
        return self._native.path

    @property
    def version(self):
        return self._native.api.chroma_version_string().decode('utf-8')

    @property
    def capabilities(self):
        return Capabilities(self._native.api.chroma_capabilities())

    def _composition(self, operation):
        # Parse/load don't touch the engine but a lease rejects use after close
        # and callback reentrancy consistently across all Engine operations.
        with self._borrow(exclusive=True):
            handle, diagnostics = N.Handle(), N.Handle()
            status = operation(C.byref(handle), C.byref(diagnostics))
            message = _message(self._native) if status else ''
            try:
                details = _owned_json(self._native, diagnostics) if diagnostics else []
                if status == Status.INVALID_COMPOSITION:
                    raise InvalidCompositionError(message, details)
                _check(self._native, status, message)
                result = Composition._from_handle(self._native, handle)
                handle = None
                return result
            finally:
                if handle:
                    self._native.api.chroma_composition_destroy(handle)

    def parse_composition(self, source, *, base_directory=None):
        if isinstance(source, dict):
            source = json.dumps(source, ensure_ascii=False)
        if isinstance(source, str):
            source = source.encode('utf-8')
        if not isinstance(source, bytes):
            raise TypeError('source must be a JSON str, bytes, or dict')
        if len(source) > 67108864 or b'\0' in source:
            raise ValueError('JSON must contain no NUL and be at most 64 MiB')
        base = _path(base_directory) if base_directory is not None else None
        return self._composition(lambda out, diagnostics: self._native.api.chroma_composition_parse(
            source, len(source), base, out, diagnostics))

    def load_composition(self, path):
        path = _path(path)
        return self._composition(lambda out, diagnostics: self._native.api.chroma_composition_load_file(
            path, out, diagnostics))

    def render_frame(self, composition, frame=0):
        _matching(self._native, composition, Composition)
        _integer(frame, 'frame', 2**63 - 1)
        with self._borrow(exclusive=True) as engine, composition._borrow() as scene:
            handle = N.Handle()
            _check(self._native, self._native.api.chroma_engine_render_frame(engine, scene, frame, C.byref(handle)))
            try:
                info = N.FrameInfo()
                _check(self._native, self._native.api.chroma_frame_get_info(handle, C.byref(info), C.sizeof(info)))
                if info.pixel_format != 1 or info.data_bytes != info.width * info.height * 16:
                    raise RuntimeError('unsupported native frame format/layout')
                pixels = self._native.api.chroma_frame_data(handle)
                return Frame(info.width, info.height, C.string_at(pixels, info.data_bytes), info.pixel_format)
            finally:
                self._native.api.chroma_frame_destroy(handle)

    def cancellation_token(self):
        with self._borrow(exclusive=True):
            return CancellationToken(library_path=self.library_path)

    def render(self, composition, output_path, *, progress=None, cancel_token=None):
        """Synchronously render; progress(snapshot) returns True to cancel."""
        _matching(self._native, composition, Composition)
        if progress is not None and not callable(progress):
            raise TypeError('progress must be callable or None')
        if cancel_token is not None:
            _matching(self._native, cancel_token, CancellationToken)
        path = _path(output_path)
        if not self._native.has_render_control and (progress is not None or cancel_token is not None):
            raise LibraryLoadError('This library lacks render control support')
        callback_error = None

        @N.Callback
        def callback(pointer, unused):
            nonlocal callback_error
            try:
                if callback_error is not None:
                    return 1
                native = pointer.contents
                snapshot = RenderProgress(RenderPhase(native.phase), *(getattr(native, name)
                    for name, _ in N.NativeProgress._fields_[2:]))
                response = progress(snapshot) if progress is not None else None
                if response is not None and not isinstance(response, bool):
                    raise TypeError('progress callback must return bool or None')
                return int(bool(response))
            except BaseException as error:
                # Never unwind Python exceptions through a C callback. Native
                # cleanup runs first, then the original exception is re-raised.
                callback_error = error
                return 1

        with ExitStack() as stack:
            engine = stack.enter_context(self._borrow(exclusive=True))
            scene = stack.enter_context(composition._borrow())
            token = stack.enter_context(cancel_token._borrow()) if cancel_token is not None else None
            result = N.Handle()
            if self._native.has_render_control:
                options = N.RenderOptions(C.sizeof(N.RenderOptions), 1, callback, None, token)
                status = self._native.api.chroma_engine_render_video_with_options(
                    engine, scene, path, C.byref(options), C.byref(result))
            else:
                status = self._native.api.chroma_engine_render_video(engine, scene, path, C.byref(result))
            message = _message(self._native) if status else ''
            if callback_error is not None:
                if result:
                    self._native.api.chroma_string_destroy(result)
                raise callback_error
            _check(self._native, status, message)
            return _owned_json(self._native, result)

    @property
    def cache_usage(self):
        with self._borrow(exclusive=True) as handle:
            result = N.Handle()
            _check(self._native, self._native.api.chroma_engine_cache_usage(handle, C.byref(result)))
            return _owned_json(self._native, result)

    def set_cache_budgets(self, image_bytes, text_bytes):
        _integer(image_bytes, 'image_bytes', 2**64 - 1)
        _integer(text_bytes, 'text_bytes', 2**64 - 1)
        with self._borrow(exclusive=True) as handle:
            _check(self._native, self._native.api.chroma_engine_set_cache_budgets(handle, image_bytes, text_bytes))

    def clear_caches(self):
        with self._borrow(exclusive=True) as handle:
            _check(self._native, self._native.api.chroma_engine_clear_caches(handle))
