import hashlib,struct,sys,unittest
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric import ec
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import package,resign

class ResignTests(unittest.TestCase):
    def setUp(self):
        self.signer=ec.derive_private_key(42,ec.SECP256R1())
        self.old=package.development_key();self.wasm=b'\0asm\1\0\0\0\0\5\4test'
        self.raw=package.build_package(self.wasm,b'resources',app_id='games.counter',title='Counter',version=3,
            abi_version=1,permissions=0,memory_pages=1,budget=1000,key_id=1,private_key=self.old)
    def test_release_key_100_preserves_sections_and_positive_version(self):
        result,audit=resign.resign_package(self.raw,self.old.public_key(),1,self.signer,100)
        info=package.validate_envelope(result,self.signer.public_key(),100)
        self.assertEqual(info['app_id'],'games.counter');self.assertEqual(info['version'],3)
        self.assertEqual(self.raw[256:],result[256:]);self.assertTrue(audit['sections_unchanged'])
        with self.assertRaises(ValueError):package.validate_envelope(result,self.old.public_key(),1)
    def test_untrusted_input_and_public_release_key_are_rejected(self):
        bad=bytearray(self.raw);bad[-1]^=1
        with self.assertRaises(ValueError):resign.resign_package(bytes(bad),self.old.public_key(),1,self.signer,100)
        with self.assertRaises(ValueError):resign.resign_package(self.raw,self.old.public_key(),1,self.old,100)
    def test_legacy_aot_needs_complete_hash_and_provenance_approval(self):
        metadata=bytearray(256);struct.pack_into('<HHII',metadata,0,1,256,5,7)
        metadata[16:22]=b'xtensa';metadata[32:39]=b'esp32s3'
        metadata[48:68]=b'a'*20;metadata[68:88]=b'b'*20
        for at in range(88,216,32):metadata[at:at+32]=bytes([at//32+1])*32
        metadata[216:248]=hashlib.sha256(self.wasm).digest()
        raw=package._assemble(self.wasm,b'',native=b'\0aot\5\0\0\0fixture',native_metadata=metadata,
            app_id='games.counter',title='Counter',version=3,abi_version=1,permissions=0,memory_pages=1,budget=1000,key_id=1,private_key=self.old)
        info=package.validate_envelope(raw,self.old.public_key(),1)
        with self.assertRaises(ValueError):resign.resign_package(raw,self.old.public_key(),1,self.signer,100)
        approval=dict(schema=1,package_sha256=hashlib.sha256(raw).hexdigest(),aot=info['aot'],review_id='test-review',method='reviewed-legacy-migration')
        result,_=resign.resign_package(raw,self.old.public_key(),1,self.signer,100,approval=approval)
        self.assertEqual(result[256:],raw[256:])
        for field,value in [('package_sha256','0'*64),('aot',{}),('review_id','')]:
            with self.subTest(field=field),self.assertRaises(ValueError):resign.resign_package(raw,self.old.public_key(),1,self.signer,100,approval={**approval,field:value})

if __name__=='__main__':unittest.main()
