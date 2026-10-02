"""Exercise signing, immutable compiler inputs and artifact revalidation.

Only the expensive compiler process is replaced; hashing, packaging, P-256,
filesystem output protection and all policy checks are real.
"""
import hashlib
import importlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

TOOLS=Path(__file__).resolve().parents[2]/'tools'
sys.path.insert(0,str(TOOLS))
WASM=b'\0asm\1\0\0\0\0\5\4test'
AOT=b'\0aot\5\0\0\0trusted-test-output'
def sha(data): return hashlib.sha256(data).hexdigest()
def encoded(data): return (json.dumps(data,sort_keys=True,indent=2)+'\n').encode()

class ReleaseCompile(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        try: self.tool=importlib.import_module('release_compile')
        except ModuleNotFoundError: self.fail('trusted compile-and-sign pipeline is not implemented')
        self.wasm=self.root/'app.wasm';self.wasm.write_bytes(WASM)
        self.compiler=self.root/'wamrc.exe';self.compiler.write_bytes(b'test compiler identity')
        self.runtime=self.root/'runtime.patch';self.runtime.write_bytes(b'test runtime patch identity')
        self.compiler_patch=self.root/'loop-poll.patch';self.compiler_patch.write_bytes(b'test compiler patch identity')
        (self.root/'main.c').write_text('/* fixture source */',encoding='utf-8')
        self.manifest=self.root/'app.json'
        self.manifest.write_bytes(encoded(dict(app_id='demo.test',title='Test',version=1,abi_version=1,
            permissions=15,memory_pages=8,budget=10000,sources=['main.c'])))
        self.key=self.root/'test-only.pem';self.signer=ec.derive_private_key(42,ec.SECP256R1())
        self.key.write_bytes(self.signer.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
        self.options=['--target=xtensa','--cpu=esp32s3','--bounds-checks=1','--stack-bounds-checks=1',
            '--opt-level=3','--size-level=0','--disable-simd','--disable-ref-types','--enable-loop-poll']
        self.lock=dict(schema=1,wamr=dict(url='https://github.com/bytecodealliance/wasm-micro-runtime.git',commit='a'*40),
            llvm=dict(url='https://github.com/espressif/llvm-project.git',commit='b'*40),
            patches=['loop-poll.patch'],aot_format_version=5,target_options=self.options)
        self.source_lock=self.root/'source-lock.json';self.source_lock.write_bytes(encoded(self.lock))
        self.prov=dict(schema=1,source_lock_sha256=sha(self.source_lock.read_bytes()),
            sources=dict(wamr=self.lock['wamr'],llvm=self.lock['llvm']),
            patches=[dict(name='loop-poll.patch',sha256=sha(self.compiler_patch.read_bytes()))],
            compiler=dict(path=str(self.compiler),sha256=sha(self.compiler.read_bytes()),version='test-only'),
            target_options=self.options,target_options_sha256=sha(json.dumps(self.options,separators=(',',':')).encode()))
        self.provenance=self.root/'provenance.json';self.provenance.write_bytes(encoded(self.prov))
        public=self.signer.public_key().public_bytes(serialization.Encoding.X962,serialization.PublicFormat.UncompressedPoint)
        self.profile=dict(schema=1,provenance_sha256=sha(self.provenance.read_bytes()),
            compiler_patch_sha256=sha(bytes.fromhex(sha(self.compiler_patch.read_bytes()))),
            runtime_patch_sha256=sha(bytes.fromhex(sha(self.runtime.read_bytes()))),runtime_abi_revision=1,
            signing_key_id=7,signing_key_sha256=sha(public),development=False)
        self.release_profile=self.root/'release-profile.json';self.release_profile.write_bytes(encoded(self.profile))
        self.output=self.root/'app.trpkg';self.compiles=0;self.after_compile=None

    def run_compiler(self,command,**kwargs):
        self.compiles+=1
        self.assertEqual(command[0],str(self.compiler))
        self.assertEqual(command[1:10],self.options)
        self.assertEqual(Path(command[-1]).read_bytes(),WASM)
        Path(command[command.index('-o')+1]).write_bytes(AOT)
        if self.after_compile:self.after_compile()
        return None

    def compile(self,**kwargs):
        with patch.object(self.tool.subprocess,'run',side_effect=self.run_compiler):
            return self.tool.compile_release(self.manifest,self.wasm,self.source_lock,self.provenance,
                self.release_profile,self.runtime,self.key,self.output,**kwargs)

    def test_compiles_own_output_and_signs_bound_source(self):
        result=self.compile()
        import package
        verified=package.validate_envelope(self.output.read_bytes(),self.signer.public_key(),7)
        self.assertEqual(self.compiles,1)
        self.assertEqual(verified['aot_size'],len(AOT));self.assertEqual(verified['wasm_size'],len(WASM))
        self.assertEqual(verified['aot']['source_wasm_sha256'],sha(WASM))
        self.assertEqual(verified['aot']['compiler_sha256'],sha(self.compiler.read_bytes()))
        self.assertEqual(verified['aot']['compat_id'],result['compat_id'])
        self.assertEqual(verified['aot_validation'],'not_performed')
        original=self.output.read_bytes();self.compile();self.assertEqual(self.output.read_bytes(),original)

    def test_cover_producer_and_mutation_checked_before_signing(self):
        from PIL import Image
        import package
        Image.new('RGB',(210,210),'blue').save(self.root/'cover.png')
        app=json.loads(self.manifest.read_text(encoding='utf-8'));app['cover']='cover.png'
        self.manifest.write_bytes(encoded(app))
        self.compile();saved=self.output.read_bytes()
        verified=package.validate_envelope(saved,self.signer.public_key(),7)
        self.assertEqual(verified['cover']['size'],133232)
        self.after_compile=lambda:(self.root/'cover.png').write_bytes(b'changed')
        with self.assertRaises(ValueError):self.compile()
        self.assertEqual(self.output.read_bytes(),saved)

    def test_native_only_package_keeps_source_identity(self):
        self.compile(include_wasm=False)
        import package
        verified=package.validate_envelope(self.output.read_bytes(),self.signer.public_key(),7)
        self.assertEqual(verified['wasm_size'],0)
        self.assertEqual(verified['aot']['source_wasm_sha256'],sha(WASM))

    def test_unpinned_compiler_provenance_and_runtime_are_rejected_before_execution(self):
        for target in (self.compiler,self.provenance,self.runtime,self.compiler_patch):
            original=target.read_bytes();target.write_bytes(original+b'changed')
            with self.subTest(target=target.name),self.assertRaises(ValueError):self.compile()
            target.write_bytes(original)
        self.assertEqual(self.compiles,0);self.assertFalse(self.output.exists())

    def test_inputs_rechecked_before_signature_and_existing_output_preserved(self):
        self.compile();original=self.output.read_bytes()
        for target in (self.compiler,self.wasm,self.runtime,self.compiler_patch,self.source_lock,self.provenance,self.release_profile,self.manifest,self.key):
            before=target.read_bytes()
            self.after_compile=lambda target=target:target.write_bytes(target.read_bytes()+b'changed')
            with self.subTest(target=target.name),self.assertRaises(ValueError):self.compile()
            self.assertEqual(self.output.read_bytes(),original);target.write_bytes(before)

    def test_public_development_key_is_not_release_authority(self):
        signer=ec.derive_private_key(1,ec.SECP256R1())
        self.key.write_bytes(signer.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
        public=signer.public_key().public_bytes(serialization.Encoding.X962,serialization.PublicFormat.UncompressedPoint)
        self.profile['signing_key_sha256']=sha(public);self.release_profile.write_bytes(encoded(self.profile))
        with self.assertRaises(ValueError):self.compile()
        self.assertEqual(self.compiles,0)
        self.profile['development']=True;self.release_profile.write_bytes(encoded(self.profile));self.compile()

    def test_cannot_override_safety_options_even_with_revised_local_manifest(self):
        self.options.remove('--enable-loop-poll')
        self.source_lock.write_bytes(encoded(self.lock));self.prov['source_lock_sha256']=sha(self.source_lock.read_bytes())
        self.prov['target_options_sha256']=sha(json.dumps(self.options,separators=(',',':')).encode())
        self.provenance.write_bytes(encoded(self.prov));self.profile['provenance_sha256']=sha(self.provenance.read_bytes())
        self.release_profile.write_bytes(encoded(self.profile))
        with self.assertRaises(ValueError):self.compile()
        self.assertEqual(self.compiles,0)

    def test_output_cannot_replace_input_or_other_files(self):
        self.output=self.wasm
        with self.assertRaises(ValueError):self.compile()
        self.assertEqual(self.wasm.read_bytes(),WASM)
        self.output=self.root/'unknown.trpkg';self.output.write_bytes(b'preserve')
        with self.assertRaises(ValueError):self.compile()
        self.assertEqual(self.output.read_bytes(),b'preserve')

if __name__=='__main__':unittest.main()
