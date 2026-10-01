"""Regression: output paths must not destroy included headers or unrelated files."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class OutputSafetyTests(unittest.TestCase):
    def setUp(self):
        self.cc=os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
        if not self.cc:self.skipTest('set TINYRT_CC for the real compiler regression')
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        self.app=Path(temp.name)
        self.header=self.app/'config.h'
        self.header.write_text('#define BACKGROUND 0x123456\n')
        (self.app/'main.c').write_text('#include "tinyrt.h"\n#include "config.h"\nint32_t tinyrt_init(int32_t w,int32_t h){return 0;}\nint32_t tinyrt_event(int32_t k,int32_t x,int32_t y,int32_t a){return 0;}\nint32_t tinyrt_render(void){return draw_clear(BACKGROUND);}\n')
        self.manifest=dict(app_id='demo.safety',title='Safety',version=1,abi_version=1,
                           permissions=1,memory_pages=2,budget=100000,sources=['main.c'])
        self.save_manifest()
        self.wasm=self.app/'build/demo.safety.wasm'
        self.package=self.app/'build/demo.safety.trpkg'

    def save_manifest(self):
        (self.app/'app.json').write_text(json.dumps(self.manifest))

    def cli(self,command,*args,success=True):
        options=['--cc',self.cc] if command=='build' else ['--development-key','--key-id','1']
        result=subprocess.run([sys.executable,str(ROOT/'tools/tinyrt.py'),command,str(self.app),
                               *options,*map(str,args)],capture_output=True,text=True)
        self.assertEqual(result.returncode==0,success,result.stdout+result.stderr)
        return result

    def test_build_preserves_actual_included_header(self):
        original=self.header.read_bytes()
        self.cli('build')  # Proves the include is valid and the real compiler succeeds.
        self.cli('build','--output',self.header,success=False)
        self.assertEqual(self.header.read_bytes(),original)

    def test_pack_preserves_actual_included_header(self):
        original=self.header.read_bytes();self.cli('build')
        self.cli('pack','--output',self.header,success=False)
        self.assertEqual(self.header.read_bytes(),original)

    def test_build_requires_wasm_extension_for_new_output(self):
        output=self.app/'new.bin'
        self.cli('build','--output',output,success=False)
        self.assertFalse(output.exists())

    def test_pack_requires_package_extension_for_new_output(self):
        self.cli('build');output=self.app/'new.bin'
        self.cli('pack','--output',output,success=False)
        self.assertFalse(output.exists())

    def test_build_preserves_existing_non_wasm_even_with_wasm_suffix(self):
        output=self.app/'config.wasm'
        for original in (b'#define CONFIG 123\n',b'',b'TRPKG001',b'\0asm\x02\0\0\0'):
            with self.subTest(original=original):
                output.write_bytes(original)
                self.cli('build','--output',output,success=False)
                self.assertEqual(output.read_bytes(),original)

    def test_pack_preserves_existing_non_package_even_with_package_suffix(self):
        self.cli('build');output=self.app/'config.trpkg'
        for original in (b'#define CONFIG 123\n',b'',self.wasm.read_bytes(),b'TRPKG002'):
            with self.subTest(original=original):
                output.write_bytes(original)
                self.cli('pack','--output',output,success=False)
                self.assertEqual(output.read_bytes(),original)

    def test_existing_generated_artifacts_can_be_updated(self):
        self.cli('build');old_wasm=self.wasm.read_bytes()
        self.cli('pack');old_package=self.package.read_bytes()
        self.header.write_text('#define BACKGROUND 0x654321\n')
        self.cli('build');self.assertNotEqual(self.wasm.read_bytes(),old_wasm)
        self.manifest['version']=2;self.save_manifest()
        self.cli('pack');self.assertNotEqual(self.package.read_bytes(),old_package)
        self.assertEqual(int.from_bytes(self.package.read_bytes()[32:36],'little'),2)

    def test_hardlink_to_declared_input_is_rejected_despite_artifact_magic(self):
        self.cli('build');asset=self.app/'resource.bin';asset.write_bytes(self.wasm.read_bytes())
        self.manifest['assets']='resource.bin';self.save_manifest()
        alias=self.app/'alias.wasm';alias.hardlink_to(asset)
        original=asset.read_bytes()
        self.cli('build','--output',alias,success=False)
        self.assertEqual(alias.read_bytes(),original);self.assertEqual(asset.read_bytes(),original)
        # Protect package resources too, even when they have the expected package magic.
        alias.unlink();asset.write_bytes(b'TRPKG001'+b'preserve resource')
        alias=self.app/'alias.trpkg';alias.hardlink_to(asset)
        original=asset.read_bytes()
        self.cli('pack','--output',alias,success=False)
        self.assertEqual(alias.read_bytes(),original);self.assertEqual(asset.read_bytes(),original)

    def test_symlink_to_included_header_is_rejected(self):
        alias=self.app/'alias.wasm'
        try:alias.symlink_to(self.header)
        except OSError as error:self.skipTest(f'symlink creation unavailable: {error}')
        original=self.header.read_bytes()
        self.cli('build','--output',alias,success=False)
        self.assertEqual(self.header.read_bytes(),original)
        alias.unlink();alias=self.app/'alias.trpkg';alias.symlink_to(self.header)
        self.cli('build')
        self.cli('pack','--output',alias,success=False)
        self.assertEqual(self.header.read_bytes(),original)

if __name__=='__main__':unittest.main()
