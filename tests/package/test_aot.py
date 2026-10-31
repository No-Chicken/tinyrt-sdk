"""AOT path keeps compiler provenance checks mandatory; any app ID may use it."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2]/'tools'))
import package
import tinyrt


class AOT(unittest.TestCase):
    def test_s31_options_preserve_safety_and_soft_float_calling_convention(self):
        import aot as tool
        options=tool.target_options('esp32s31')
        self.assertEqual(options[:2],['--target=riscv32','--cpu=generic-rv32'])
        expected=list(tool.OPTIONS[2:]);expected[3]='--size-level=3'
        self.assertEqual(options[2:9],expected)
        self.assertEqual(options[9:],['--target-abi=ilp32f','--cpu-features=+m,+a,+c,+f,-d'])
        self.assertEqual(tool.target_options('esp32s3'),tool.OPTIONS)
        with self.assertRaises(ValueError):tool.target_options('unknown')
    def test_fixed_profile_own_compilation_and_tamper_rejection(self):
        import aot as tool
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);compiler=root/'wamrc.exe';compiler.write_bytes(b'test compiler')
            wasm=root/'app.wasm';wasm.write_bytes(b'\0asm\1\0\0\0\0\1\0')
            (root/'main.c').write_text('/* fixture */',encoding='utf-8')
            manifest=root/'app.json';metadata=dict(app_id='demo.game',title='Game',version=1,
                abi_version=1,permissions=15,memory_pages=4,budget=100000,sources=['main.c'])
            from PIL import Image
            Image.new('RGB',(210,210),'red').save(root/'cover.png')
            metadata['cover']='cover.png'
            manifest.write_text(json.dumps(metadata),encoding='utf-8')
            profile=json.loads(tool.profile_path('esp32s3').read_text(encoding='utf-8'))
            profile['compiler']['sha256']=hashlib.sha256(compiler.read_bytes()).hexdigest()
            pin=root/'pin.json';pin.write_text(json.dumps(profile),encoding='utf-8')
            output=root/'game.trpkg';calls=[]
            def compile(command,**kwargs):
                calls.append(command)
                self.assertEqual(command[1:10],tool.OPTIONS)
                self.assertEqual(Path(command[-1]).read_bytes(),wasm.read_bytes())
                Path(command[command.index('-o')+1]).write_bytes(b'\0aot'+struct.pack('<I',5)+b'native-test-output')
            with patch.object(tool,'profile_path',return_value=pin),patch.object(tool.subprocess,'run',side_effect=compile):
                tool.pack_aot(manifest,wasm,output,compiler,target='esp32s3')
                checked=package.validate_envelope(output.read_bytes())
                self.assertEqual(checked['aot']['compat_id'],profile['compat_id'])
                self.assertEqual(checked['cover']['size'],133232)
                self.assertEqual(len(calls),1)
                saved=output.read_bytes();compiler.write_bytes(b'tampered')
                with self.assertRaises(ValueError):tool.pack_aot(manifest,wasm,output,compiler,target='esp32s3')
                self.assertEqual(len(calls),1);self.assertEqual(output.read_bytes(),saved)
                compiler.write_bytes(b'test compiler')
                metadata['app_id']='my-game';manifest.write_text(json.dumps(metadata),encoding='utf-8')
                tool.pack_aot(manifest,wasm,output,compiler,target='esp32s3')
                self.assertEqual(package.validate_envelope(output.read_bytes())['app_id'],'my-game')
    def test_rebuild_detects_aot_that_is_not_compiled_from_the_packaged_wasm(self):
        import aot as tool
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);compiler=root/'wamrc.exe';compiler.write_bytes(b'test compiler')
            wasm=root/'app.wasm';wasm.write_bytes(b'\0asm\1\0\0\0\0\1\0')
            (root/'main.c').write_text('/* fixture */',encoding='utf-8')
            manifest=root/'app.json';manifest.write_text(json.dumps(dict(app_id='my-game',title='Game',version=1,
                abi_version=1,permissions=15,memory_pages=4,budget=100000,sources=['main.c'])),encoding='utf-8')
            profile=json.loads(tool.profile_path('esp32s31').read_text(encoding='utf-8'))
            profile['compiler']['sha256']=hashlib.sha256(compiler.read_bytes()).hexdigest()
            pin=root/'pin.json';pin.write_text(json.dumps(profile),encoding='utf-8')
            output=root/'game.trpkg';native=b'\0aot'+struct.pack('<I',5)+b'honest-native-output'
            def compile(command,**kwargs):Path(command[command.index('-o')+1]).write_bytes(native)
            with patch.object(tool,'profile_path',return_value=pin),patch.object(tool.subprocess,'run',side_effect=compile):
                tool.pack_aot(manifest,wasm,output,compiler,target='esp32s31')
                raw=output.read_bytes()
                self.assertEqual(tool.check_rebuild(raw,compiler)['aot_rebuild'],'identical')
                swapped=bytearray(raw);at=raw.index(b'honest');swapped[at:at+6]=b'hostil'
                swapped[152:184]=hashlib.sha256(swapped[256:]).digest()
                package.validate_envelope(bytes(swapped))
                with self.assertRaisesRegex(ValueError,'does not match'):tool.check_rebuild(bytes(swapped),compiler)


if __name__=='__main__':unittest.main()
