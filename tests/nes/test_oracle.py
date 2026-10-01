"""Independent fixture/pixel oracle and pinned-source checks; no emulator needed."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

SDK = Path(__file__).resolve().parents[2]

def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded

rom_source = module('n1_rom_source', SDK / 'examples/nes/make_rom.py')
adapter = module('n1_adapter', SDK / 'third_party/nes/adapt_cpu.py')

def pixel_crc(color):
    # NES output contract: 256x240 pixels, a 64x64 square at (64,64), black surround.
    data = b''.join(struct.pack('<H', color if 64 <= x < 128 and 64 <= y < 128 else 0)
                    for y in range(240) for x in range(256))
    return zlib.crc32(data)

class FixtureTests(unittest.TestCase):
    def test_source_provenance(self):
        source = adapter.verify_source()
        self.assertEqual(source['revision'], '638096ae00d258700779be2af06478d1be5bf8a1')
        self.assertEqual(len(source['files']), 16)

    def test_original_nrom_layout_and_vectors(self):
        rom, labels = rom_source.make_rom()
        self.assertEqual(len(rom), 16 + 16384 + 8192)
        self.assertEqual(rom[:16], b'NES\x1a' + bytes((1, 1)) + bytes(10))
        for offset, label in ((0x3ffa, 'nmi'), (0x3ffc, 'reset'), (0x3ffe, 'irq')):
            self.assertEqual(struct.unpack_from('<H', rom, 16 + offset)[0], labels[label])
        self.assertEqual(rom[16:21], bytes.fromhex('78 d8 a2 ff 9a'))  # SEI/CLD/LDX/TXS
        self.assertIn(bytes.fromhex('a9 10 18 69 23 85 02'), rom[16:512])  # ADC signature
        self.assertEqual(rom[16 + 16384:16 + 16384 + 32], bytes(16) + b'\xff' * 8 + bytes(8))
        self.assertEqual(rom[16 + 16384 + 32:], bytes(8192 - 32))

    def test_known_rgb565_frame_checksums(self):
        # Colors specified by the pinned NES palette, not sampled from emulator output.
        self.assertEqual(pixel_crc(0x20d1), 0x7fdd3027)  # NES color $01
        self.assertEqual(pixel_crc(0x3dff), 0xee67c189)  # NES color $21
        self.assertEqual(pixel_crc(0xffff), 0x7ac3eb7f)  # NES color $30, held A

if __name__ == '__main__':
    unittest.main()
