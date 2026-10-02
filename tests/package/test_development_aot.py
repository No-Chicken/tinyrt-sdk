"""AOT developer path keeps compiler provenance and namespace checks mandatory."""
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


class DevelopmentAOT(unittest.TestCase):
    def test_fixed_profile_own_compilation_and_tamper_rejection(self):
        import development_aot as tool
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
            profile=json.loads(tool.PROFILE.read_text(encoding='utf-8'))
            profile['compiler']['sha256']=hashlib.sha256(compiler.read_bytes()).hexdigest()
            pin=root/'pin.json';pin.write_text(json.dumps(profile),encoding='utf-8')
            output=root/'game.trpkg';calls=[]
            def compile(command,**kwargs):
                calls.append(command)
                self.assertEqual(command[1:10],tool.OPTIONS)
                self.assertEqual(Path(command[-1]).read_bytes(),wasm.read_bytes())
                Path(command[command.index('-o')+1]).write_bytes(b'\0aot'+struct.pack('<I',5)+b'native-test-output')
            with patch.object(tool,'PROFILE',pin),patch.object(tool.subprocess,'run',side_effect=compile):
                tool.pack_development(manifest,wasm,output,compiler)
                checked=package.validate_envelope(output.read_bytes(),package.development_key().public_key(),1)
                self.assertEqual(checked['aot']['compat_id'],profile['compat_id'])
                self.assertEqual(checked['cover']['size'],133232)
                self.assertEqual(len(calls),1)
                saved=output.read_bytes();compiler.write_bytes(b'tampered')
                with self.assertRaises(ValueError):tool.pack_development(manifest,wasm,output,compiler)
                self.assertEqual(len(calls),1);self.assertEqual(output.read_bytes(),saved)
                metadata['app_id']='other.game';manifest.write_text(json.dumps(metadata),encoding='utf-8')
                with self.assertRaises(ValueError):tool.pack_development(manifest,wasm,output,compiler)


if __name__=='__main__':unittest.main()
