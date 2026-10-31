"""Behavioral CLI tests; the legacy signature is checked independently."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
TOOL=Path(__file__).resolve().parents[2]/"tools"/"package.py"
sys.path.insert(0,str(TOOL.parent))
import package
ORDER=0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
class PackageCLI(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.root=Path(self.tmp.name)
        self.wasm=self.root/"app.wasm"
        self.wasm.write_bytes(b"\0asm\1\0\0\0\0\5\4test")
        self.output=self.root/"app.trpkg"
        self.base=["pack","--wasm",str(self.wasm),"--output",str(self.output),
                   "--app-id","demo.app","--title","\u8ba1\u6570\u5668","--version","3",
                   "--permissions","15","--memory-pages","8","--budget","12345"]
    def tearDown(self): self.tmp.cleanup()
    def invoke(self,*args):
        return subprocess.run([sys.executable,str(TOOL),*args],capture_output=True,text=True,encoding="utf-8")
    def test_legacy_signature_hash_and_metadata(self):
        result=self.invoke(*self.base)
        self.assertEqual(result.returncode,0,result.stderr)
        data=self.output.read_bytes()
        self.assertEqual(data[:8],b"TRPKG001")
        self.assertEqual(struct.unpack_from("<HH",data,8),(1,256))
        self.assertEqual(struct.unpack_from("<I",data,12)[0],len(data))
        self.assertEqual(data[152:184],hashlib.sha256(data[256:]).digest())
        self.assertEqual(data[184:192],bytes(8))
        r=int.from_bytes(data[192:224],"big"); s=int.from_bytes(data[224:256],"big")
        self.assertTrue(0<r<ORDER and 0<s<=ORDER//2)
        ec.derive_private_key(1,ec.SECP256R1()).public_key().verify(
            utils.encode_dss_signature(r,s),b"TinyRT-package-v1\0"+data[:192],ec.ECDSA(hashes.SHA256()))
        meta=json.loads(result.stdout)
        self.assertEqual(meta["sha256"],hashlib.sha256(data).hexdigest())
        self.assertEqual(meta["package_size"],len(data))
        again=self.invoke(*self.base)
        self.assertEqual(again.returncode,0,again.stderr)
        self.assertEqual(self.output.read_bytes(),data)
    def test_section_wasm_only_layout_and_signature(self):
        assets=self.root/'assets.bin';assets.write_bytes(b'resources')
        result=self.invoke(*self.base,'--assets',str(assets))
        self.assertEqual(result.returncode,0,result.stderr)
        data=self.output.read_bytes()
        self.assertEqual(data[:8],b'TRPKG001')
        self.assertEqual(struct.unpack_from('<HH5I',data,8),(1,256,313,256,2,16,0))
        self.assertEqual(struct.unpack_from('<8I',data,256),(1,0,288,15,3,0,304,9))
        self.assertEqual(data[303:304],b'\0')
        self.assertEqual(data[288:303],self.wasm.read_bytes())
        self.assertEqual(data[152:184],hashlib.sha256(data[256:]).digest())
        r=int.from_bytes(data[192:224],'big');s=int.from_bytes(data[224:256],'big')
        ec.derive_private_key(1,ec.SECP256R1()).public_key().verify(
            utils.encode_dss_signature(r,s),b'TinyRT-package-v1\0'+data[:192],ec.ECDSA(hashes.SHA256()))
        again=self.invoke(*self.base,'--assets',str(assets))
        self.assertEqual(again.returncode,0,again.stderr)
        self.assertEqual(self.output.read_bytes(),data)
    def test_generic_packer_does_not_accept_native_artifacts(self):
        native=self.root/'app.aot';native.write_bytes(b'\0aot\5\0\0\0native')
        result=self.invoke(*self.base,'--aot',str(native))
        self.assertNotEqual(result.returncode,0)
        self.assertFalse(self.output.exists())
    def test_reject_invalid_metadata_without_overwriting(self):
        self.output.write_bytes(b"preserve")
        for flag,value in [("--app-id","BAD"),("--title",""),("--title","x"*64),("--version","0"),
                           ("--abi-version","2"),("--permissions","64"),("--memory-pages","17"),
                           ("--budget","100001")]:
            with self.subTest(flag=flag):
                result=self.invoke(*self.base,flag,value)
                self.assertNotEqual(result.returncode,0)
                self.assertEqual(self.output.read_bytes(),b"preserve")
    def test_bad_wasm_and_oversize(self):
        self.wasm.write_bytes(b"bad-wasm"*4)
        self.assertNotEqual(self.invoke(*self.base).returncode,0)
        self.wasm.write_bytes(b"\0asm\1\0\0\0"+bytes(package.MAX_PACKAGE_SIZE))
        self.assertNotEqual(self.invoke(*self.base).returncode,0)
        self.assertFalse(self.output.exists())
    def test_output_cannot_overwrite_wasm(self):
        original=self.wasm.read_bytes()
        result=self.invoke(*self.base,"--output",str(self.wasm))
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(self.wasm.read_bytes(),original)
    def test_signature_area_is_never_checked_but_payload_is(self):
        self.assertEqual(self.invoke(*self.base).returncode,0)
        data=bytearray(self.output.read_bytes());self.assertEqual(struct.unpack_from('<I',data,52)[0],1)
        data[192:256]=bytes(64);self.assertEqual(package.validate_envelope(bytes(data))['app_id'],'demo.app')
        data[-1]^=1
        with self.assertRaisesRegex(ValueError,'payload hash'):package.validate_envelope(bytes(data))
if __name__=="__main__": unittest.main()
