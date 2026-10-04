"""Graphics requirement failures happen before compilation/signing output."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import tinyrt

def import_wasm(field):
    module=b'tinyrt';field=field.encode()
    record=bytes([1,len(module)])+module+bytes([len(field)])+field+b'\x00\x00'
    return b'\0asm\x01\0\0\0'+bytes([2,len(record)])+record

class GraphicsCompatibilityTests(unittest.TestCase):
    def test_pending_contract_cannot_be_published(self):
        import release_validation
        with patch.object(Path,'read_bytes',return_value=b'{"core_revision":"pending"}'):
            with self.assertRaisesRegex(ValueError,'committed contract pin'):
                release_validation.core_contract_revision()
    def test_known_and_unknown_graphics_imports(self):
        tinyrt.check_graphics_imports(import_wasm('gfx_caps'))
        tinyrt.check_graphics_imports(import_wasm('gfx_submit'))
        with self.assertRaisesRegex(ValueError,'unsupported graphics import'):
            tinyrt.check_graphics_imports(import_wasm('gfx_future_required'))
        with self.assertRaisesRegex(ValueError,'truncated'):
            tinyrt.check_graphics_imports(import_wasm('gfx_caps')[:-1])
    def test_unknown_manifest_graphics_requirement(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/'main.c').write_text('')
            value=dict(app_id='demo.gfx',title='Graphics',version=1,abi_version=1,
                permissions=1,memory_pages=2,budget=100000,sources=['main.c'],graphics='future')
            path=root/'app.json';path.write_text(json.dumps(value))
            with self.assertRaisesRegex(ValueError,'unsupported graphics requirement'):tinyrt.load_manifest(path)
            for mode in ('immediate','raster','framebuffer'):
                value['graphics']=mode;path.write_text(json.dumps(value))
                self.assertEqual(tinyrt.load_manifest(path)['graphics'],mode)
if __name__=='__main__':unittest.main()
