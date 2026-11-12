"""Exercise Puff's production input, physics and frame scheduling natively."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get('TINYRT_CC'), 'set TINYRT_CC to a native-capable compiler')
class PuffTests(unittest.TestCase):
    def test_controls_hover_and_render_schedule(self):
        cc = Path(os.environ['TINYRT_CC']).resolve()
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'puff-tests.exe'
            command = [str(cc)] + (['cc'] if cc.stem == 'zig' else [])
            subprocess.run(command + ['-std=c11', '-O2', '-UNDEBUG', '-I', str(ROOT / 'include'),
                str(ROOT / 'tests/sdk/test_puff.c'), '-o', str(output)], check=True)
            subprocess.run([str(output), str(ROOT / 'examples/puff/resources.bin')], check=True)
