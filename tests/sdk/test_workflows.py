"""Behavior tests for SDK manifest, isolated build, pack and envelope validation."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
VALID = dict(app_id='demo.test', title='Test', version=1, abi_version=1,
             permissions=15, memory_pages=2, budget=100000, sources=['main.c'])


class SDKTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue((ROOT / 'tools/tinyrt.py').is_file(), 'unified SDK CLI is missing')
        import tinyrt
        self.sdk = tinyrt
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.project = Path(self.temp.name)
        (self.project / 'main.c').write_text('#include "tinyrt.h"\nint32_t tinyrt_init(int32_t w,int32_t h){return 0;}\nint32_t tinyrt_event(int32_t k,int32_t x,int32_t y,int32_t a){return 0;}\nint32_t tinyrt_render(void){return draw_clear(0);}\n')

    def manifest(self, values=None):
        path = self.project / 'app.json'
        path.write_text(json.dumps(VALID if values is None else values), encoding='utf-8')
        return path

    def cli(self, *args, success=True):
        result = subprocess.run([sys.executable, str(ROOT/'tools/tinyrt.py'), *map(str,args)],
                                capture_output=True, text=True, cwd=self.project)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def test_accepts_valid_manifest(self):
        self.assertEqual(self.sdk.load_manifest(self.manifest())['app_id'], 'demo.test')

    def test_rejects_policy_and_type_violations(self):
        cases = [('app_id',''),('app_id','A'),('app_id','x'*32),('title',''),('title','中'*22),
                 ('title','a\0b'),('version',0),('version',2**32),('version',True),('version',1.5),
                 ('abi_version',2),('permissions',16),('permissions',-1),('memory_pages',0),
                 ('memory_pages',17),('budget',0),('budget',100001),('sources',[]),
                 ('sources',['../outside.c']),('sources',['/absolute.c']),
                 ('sources',['main.c','main.c']),('assets','../outside.bin'),
                 ('defines',{'BAD-NAME':1}),('defines',{'GOOD':'-include secret'}),
                 ('unknown',1)]
        for key,value in cases:
            with self.subTest(key=key,value=value):
                values=copy.deepcopy(VALID); values[key]=value
                with self.assertRaises(ValueError): self.sdk.load_manifest(self.manifest(values))

    def test_rejects_missing_fields_and_duplicate_json_keys(self):
        for key in VALID:
            values=copy.deepcopy(VALID); del values[key]
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.sdk.load_manifest(self.manifest(values))
        p=self.manifest();p.write_text('{"version":1,"version":2}')
        with self.assertRaises(ValueError): self.sdk.load_manifest(p)

    def test_new_cannot_overwrite_existing_project(self):
        dest=self.project/'new-app'
        self.cli('new',dest,'--app-id','demo.new','--title','New')
        self.assertEqual(json.loads((dest/'app.json').read_text())['app_id'],'demo.new')
        self.assertTrue((dest/'main.c').is_file())
        self.cli('new',dest,'--app-id','demo.new',success=False)
        self.assertEqual(json.loads((dest/'app.json').read_text())['title'],'New')

    def test_build_pack_validate_from_standalone_copy(self):
        cc=os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
        if not cc: self.skipTest('set TINYRT_CC to run real freestanding build')
        isolated=self.project/'sdk'
        for part in ('tools','include','templates'):
            shutil.copytree(ROOT/part,isolated/part,ignore=shutil.ignore_patterns('__pycache__'))
        project=self.project/'app'
        command=[sys.executable,str(isolated/'tools/tinyrt.py')]
        def run(*args):
            p=subprocess.run(command+list(map(str,args)),capture_output=True,text=True,cwd=self.project)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)
            return json.loads(p.stdout)
        run('new',project,'--app-id','demo.standalone')
        built=run('build',project,'--cc',cc)
        self.assertTrue(Path(built['wasm']).read_bytes().startswith(b'\0asm\x01\0\0\0'))
        packed=run('pack',project,'--development-key','--key-id','1')
        checked=run('validate',packed['package'],'--development-key','--key-id','1')
        self.assertEqual(checked['validation_level'],'envelope')
        self.assertEqual(checked['wasm_validation'],'not_performed')
        self.assertEqual(checked['app_id'],'demo.standalone')

    def package(self, wasm=b'\0asm\x01\0\0\0\0\1\0', assets=b''):
        import package
        return package.build_package(wasm,assets,app_id='demo.test',title='Test',version=1,
            abi_version=1,permissions=15,memory_pages=2,budget=100000,key_id=1,
            private_key=package.development_key())

    def test_build_uses_declared_memory_for_larger_static_data(self):
        cc=os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
        if not cc:self.skipTest('set TINYRT_CC to run real freestanding build')
        (self.project/'main.c').write_text('#include "tinyrt.h"\nstatic volatile unsigned char buffer[70000];\nint32_t tinyrt_init(int32_t w,int32_t h){buffer[69999]=1;return 0;}\nint32_t tinyrt_event(int32_t k,int32_t x,int32_t y,int32_t a){return 0;}\nint32_t tinyrt_render(void){return draw_clear(buffer[69999]);}\n')
        self.manifest()
        self.cli('build',self.project,'--cc',cc)

    def test_build_cannot_overwrite_declared_assets(self):
        cc=os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
        if not cc:self.skipTest('set TINYRT_CC to run real freestanding build')
        asset=self.project/'resources.bin';asset.write_bytes(b'preserve resource')
        values=copy.deepcopy(VALID);values['assets']='resources.bin'
        path=self.manifest(values)
        with self.assertRaises(ValueError):self.sdk.build(path,cc,asset)
        self.assertEqual(asset.read_bytes(),b'preserve resource')
    def test_envelope_is_explicitly_not_wasm_validation(self):
        import package
        info=self.sdk.validate_envelope(self.package(),package.development_key().public_key(),1)
        self.assertEqual(info['validation_level'],'envelope')
        self.assertEqual(info['wasm_validation'],'not_performed')

    def test_envelope_rejects_corruption_untrusted_id_and_high_s(self):
        import package
        key=package.development_key().public_key()
        data=self.package()
        for offset in (0,8,10,12,16,20,24,28,32,36,40,44,48,52,56,88,152,184,192,256):
            changed=bytearray(data);changed[offset]^=0xff
            with self.subTest(offset=offset),self.assertRaises(ValueError):
                self.sdk.validate_envelope(bytes(changed),key,1)
        for malformed in (b'',data[:-1],data+b'\0'):
            with self.assertRaises(ValueError): self.sdk.validate_envelope(malformed,key,1)
        with self.assertRaises(ValueError):self.sdk.validate_envelope(data,key,2)
        high=bytearray(data);s=int.from_bytes(high[224:256],'big')
        high[224:256]=(package.P256_ORDER-s).to_bytes(32,'big')
        with self.assertRaises(ValueError):self.sdk.validate_envelope(bytes(high),key,1)

    def test_single_package_limit(self):
        import package
        wasm=b'\0asm\x01\0\0\0\0\1\0'
        data=self.package(wasm,b'a'*(303104-256-len(wasm)))
        self.assertEqual(len(data),303104)
        self.sdk.validate_envelope(data,package.development_key().public_key(),1)
        with self.assertRaises(ValueError):self.package(wasm,b'a'*(303105-256-len(wasm)))

    def test_pack_refuses_to_overwrite_manifest_or_wasm(self):
        p=self.manifest();wasm=self.project/'app.wasm';wasm.write_bytes(b'\0asm\x01\0\0\0\0\1\0')
        original=p.read_bytes()
        self.cli('pack',p,'--wasm',wasm,'--output',p,'--development-key','--key-id','1',success=False)
        self.assertEqual(p.read_bytes(),original)
        self.cli('pack',p,'--wasm',wasm,'--output',wasm,'--development-key','--key-id','1',success=False)
        self.assertEqual(wasm.read_bytes(),b'\0asm\x01\0\0\0\0\1\0')

    def test_pomodoro_source_and_manifests_exist(self):
        self.assertTrue((ROOT/'examples/pomodoro/main.c').is_file(),'Pomodoro implementation is missing')
        normal=self.sdk.load_manifest(ROOT/'examples/pomodoro/app.json')
        fast=self.sdk.load_manifest(ROOT/'examples/pomodoro/app-fast.json')
        self.assertEqual(normal['permissions'],15)
        self.assertNotEqual(normal['app_id'],fast['app_id'])
        self.assertIn('25s',fast['title'])

if __name__=='__main__':unittest.main()
