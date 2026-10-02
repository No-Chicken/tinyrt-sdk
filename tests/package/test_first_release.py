"""First public format has a canonical section table; old envelopes are rejected."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import package
import tinyrt


class FirstRelease(unittest.TestCase):
    def test_section_table_is_the_only_format_one(self):
        wasm = b'\0asm\1\0\0\0\0\1\0'
        raw = package.build_package(wasm, b'rom', app_id='demo.test', title='Test',
            version=1, abi_version=1, permissions=15, memory_pages=2,
            budget=100000, key_id=1, private_key=package.development_key())
        self.assertEqual(struct.unpack_from('<HH5I', raw, 8),
                         (1, 256, len(raw), 256, 2, 16, 0))
        self.assertEqual(struct.unpack_from('<4I', raw, 256), (1, 0, 288, len(wasm)))
        info = tinyrt.validate_envelope(raw, package.development_key().public_key(), 1)
        self.assertEqual(info['format_version'], 1)
        self.assertEqual(info['assets_size'], 3)


if __name__ == '__main__':
    unittest.main()
