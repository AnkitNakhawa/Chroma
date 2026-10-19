"""Reject packages that would accidentally rely on their build machine."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('bundle', Path(__file__).resolve().parents[1] / 'packaging/bundle.py')
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class DependencyAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'bin').mkdir()
        (self.root / 'lib').mkdir()
        self.cli = self.root / 'bin/chroma'
        self.cli.touch()

    def linux(self, dependencies, rpath='$ORIGIN/../lib'):
        dynamic = f' (RUNPATH) Library runpath: [{rpath}]'
        with patch.object(bundle.platform, 'system', return_value='Linux'), \
             patch.object(bundle, 'deps', return_value=dependencies), \
             patch.object(bundle, 'output', return_value=dynamic):
            return bundle.audit(self.root)

    def mac(self, dependencies):
        with patch.object(bundle.platform, 'system', return_value='Darwin'), \
             patch.object(bundle, 'deps', return_value=dependencies), \
             patch.object(bundle, 'output', return_value=''), \
             patch.object(bundle, 'run'):
            return bundle.audit(self.root)

    def test_linux_missing_dependency(self):
        with self.assertRaisesRegex(RuntimeError, 'unbundled'):
            self.linux(['libavcodec.so.63'])

    def test_linux_absolute_dependency(self):
        with self.assertRaisesRegex(RuntimeError, 'unbundled'):
            self.linux(['/opt/development/lib/libc.so.6'])

    def test_linux_external_search_path(self):
        with self.assertRaisesRegex(RuntimeError, 'unexpected search paths'):
            self.linux(['libc.so.6'], '$ORIGIN/../lib:/opt/development/lib')

    def test_linux_os_runtime_allowed(self):
        self.assertEqual(self.linux(['libc.so.6', 'libstdc++.so.6', 'libgcc_s.so.1']),
                         {'bin/chroma': ['libc.so.6', 'libstdc++.so.6', 'libgcc_s.so.1']})

    def test_mac_homebrew_dependency(self):
        with self.assertRaisesRegex(RuntimeError, 'non-relative'):
            self.mac(['/opt/homebrew/lib/libavcodec.63.dylib'])

    def test_mac_missing_relative_dependency(self):
        with self.assertRaisesRegex(RuntimeError, 'missing or escaped'):
            self.mac(['@loader_path/../lib/libavcodec.63.dylib'])

    def test_linux_relative_symlink_escape(self):
        (self.root / 'lib/escape.so').symlink_to(self.cli.parent.parent.parent / 'outside.so')
        with self.assertRaisesRegex(RuntimeError, 'symlink escapes'):
            self.linux(['escape.so'])

    def test_mac_relative_symlink_escape(self):
        (self.root / 'lib/escape.dylib').symlink_to('/usr/lib/libSystem.B.dylib')
        with self.assertRaisesRegex(RuntimeError, 'symlink escapes'):
            self.mac(['@loader_path/../lib/escape.dylib'])

    def test_mac_only_apple_system_dependencies_allowed(self):
        self.mac(['/usr/lib/libSystem.B.dylib', '/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation'])


if __name__ == '__main__':
    unittest.main()
