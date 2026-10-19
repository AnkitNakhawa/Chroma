"""Private ABI-v1 declarations. All pointers retain their native type/ownership."""
from __future__ import annotations

import ctypes as C
import ctypes.util
import os
from pathlib import Path
import sys
import threading
import weakref

U32, U64, I32, I64 = C.c_uint32, C.c_uint64, C.c_int32, C.c_int64
Handle = C.c_void_p


class EngineOptions(C.Structure):
    _fields_ = [('struct_size', U32), ('abi_version', U32), ('image_bytes', U64), ('text_bytes', U64)]


class CompositionInfo(C.Structure):
    _fields_ = [('struct_size', U32), ('width', I32), ('height', I32), ('schema_version', I32),
                ('frame_rate_numerator', I64), ('frame_rate_denominator', I64),
                ('start_frame', I64), ('duration_frames', I64)]


class FrameInfo(C.Structure):
    _fields_ = [('struct_size', U32), ('width', I32), ('height', I32), ('pixel_format', U32),
                ('row_stride_bytes', U64), ('data_bytes', U64)]


class NativeProgress(C.Structure):
    _fields_ = [('struct_size', U32), ('phase', U32), ('frames_encoded', I64), ('total_frames', I64),
                ('audio_samples_encoded', I64), ('total_audio_samples', I64), ('elapsed_nanoseconds', I64)]


Callback = C.CFUNCTYPE(I32, C.POINTER(NativeProgress), Handle)


class RenderOptions(C.Structure):
    _fields_ = [('struct_size', U32), ('abi_version', U32), ('progress', Callback),
                ('user_data', Handle), ('cancel_token', Handle)]


class LibraryLoadError(RuntimeError):
    """The shared library could not be loaded or lacks the required C interface."""


class NativeLibrary:
    def __init__(self, path: str):
        self.path = path
        try:
            # CDLL releases the GIL during native calls; CFUNCTYPE reacquires it.
            self.api = C.CDLL(path)
        except OSError as error:
            raise LibraryLoadError(f'Cannot load Chroma library {path!r}: {error}') from error
        try:
            self._bind('abi_version', U32)
            if self.api.chroma_abi_version() != 1:
                raise LibraryLoadError('This wrapper requires Chroma C ABI version 1')
            self._bind('version_string', C.c_char_p)
            self._bind('capabilities', U64)
            self._bind('last_error', C.c_char_p)
            self._bind('engine_create', I32, C.POINTER(EngineOptions), C.POINTER(Handle))
            self._bind('engine_destroy', None, Handle)
            self._bind('engine_set_cache_budgets', I32, Handle, U64, U64)
            self._bind('engine_clear_caches', I32, Handle)
            self._bind('engine_cache_usage', I32, Handle, C.POINTER(Handle))
            self._bind('composition_parse', I32, C.c_char_p, U64, C.c_char_p,
                       C.POINTER(Handle), C.POINTER(Handle))
            self._bind('composition_load_file', I32, C.c_char_p, C.POINTER(Handle), C.POINTER(Handle))
            self._bind('composition_destroy', None, Handle)
            self._bind('composition_get_info', I32, Handle, C.POINTER(CompositionInfo), U64)
            self._bind('composition_inspect_plan', I32, Handle, I64, U32, C.POINTER(Handle))
            self._bind('engine_render_frame', I32, Handle, Handle, I64, C.POINTER(Handle))
            self._bind('frame_get_info', I32, Handle, C.POINTER(FrameInfo), U64)
            self._bind('frame_data', C.POINTER(C.c_float), Handle)
            self._bind('frame_destroy', None, Handle)
            self._bind('engine_render_video', I32, Handle, Handle, C.c_char_p, C.POINTER(Handle))
            # Do not use c_char_p for an OWNED string handle: ctypes would turn
            # that address into bytes, losing the pointer needed by destroy().
            self._bind('string_data', Handle, Handle)
            self._bind('string_size', U64, Handle)
            self._bind('string_destroy', None, Handle)
        except AttributeError as error:
            raise LibraryLoadError(f'Library {path!r} is missing required Chroma C API symbols') from error

    def _bind(self, name, result, *arguments):
        function = getattr(self.api, 'chroma_' + name)
        function.restype, function.argtypes = result, list(arguments)


_cache: weakref.WeakValueDictionary[str, NativeLibrary] = weakref.WeakValueDictionary()
_cache_lock = threading.Lock()


def load_library(path=None):
    requested = path if path is not None else os.environ.get('CHROMA_LIBRARY_PATH')
    if requested is None:
        filename = 'libchroma.dylib' if sys.platform == 'darwin' else 'libchroma.so'
        # Relocatable SDK layout: <prefix>/share/chroma/python/chroma_render/.
        for parent in Path(__file__).resolve().parents:
            candidate = parent / 'lib' / filename
            if candidate.is_file():
                requested = str(candidate)
                break
        if requested is None:
            requested = ctypes.util.find_library('chroma')
    if not requested:
        raise LibraryLoadError('Chroma library not found. Set CHROMA_LIBRARY_PATH or pass library_path=')
    requested = os.fsdecode(os.fspath(requested))
    if '\0' in requested:
        raise ValueError('library path must not contain NUL')
    if Path(requested).exists() or os.path.sep in requested:
        requested = str(Path(requested).resolve())
    with _cache_lock:
        library = _cache.get(requested)
        if library is None:
            library = NativeLibrary(requested)
            _cache[requested] = library
        return library
