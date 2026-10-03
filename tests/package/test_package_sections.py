"""Signed malformed inputs distinguish structural rejection from signature failure."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec,utils

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import package
import package

WASM=b'\0asm\x01\0\0\0\0\x01\0'
KEY=ec.derive_private_key(42,ec.SECP256R1())
KW=dict(app_id='demo.fixture',title='Fixture',version=1,abi_version=1,permissions=15,
        memory_pages=2,budget=100000,key_id=42,private_key=KEY)

def signed(raw):
    raw=bytearray(raw);raw[152:184]=hashlib.sha256(raw[256:]).digest()
    r,s=utils.decode_dss_signature(KEY.sign(package.DOMAIN+raw[:192],ec.ECDSA(hashes.SHA256())))
    raw[192:256]=r.to_bytes(32,'big')+min(s,package.P256_ORDER-s).to_bytes(32,'big')
    return bytes(raw)

def meta(wasm):
    raw=bytearray(256);struct.pack_into('<HHII',raw,0,1,256,5,7)
    raw[16:32]=b'xtensa'.ljust(16,b'\0');raw[32:48]=b'esp32s3'.ljust(16,b'\0')
    raw[48:216]=bytes(range(1,169));raw[216:248]=hashlib.sha256(wasm).digest()
    return bytes(raw)

class SectionEnvelope(unittest.TestCase):
    def verify(self,raw):return package.validate_envelope(raw,KEY.public_key(),42)

    def test_signed_table_header_padding_and_tail_rejections(self):
        good=package.build_wasm_package(WASM,b'asset',**KW)
        self.verify(good)
        changes=[(8,'H',3),(10,'H',255),(12,'I',len(good)+1),(16,'I',260),(20,'I',0),(20,'I',4),
            (24,'I',20),(28,'I',1),(36,'I',2),(40,'I',32),(44,'I',17),(48,'I',0),
            (256,'I',0),(256,'I',4),(260,'I',1),(264,'I',256),(264,'I',289),(268,'I',0),(268,'I',0xffffffff),
            (272,'I',1),(272,'I',4),(276,'I',1),(280,'I',296),(280,'I',304),(284,'I',0),(284,'I',0xffffffff),
            (299,'B',1)]
        for offset,kind,value in changes:
            raw=bytearray(good);struct.pack_into('<'+kind,raw,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):self.verify(signed(raw))
        raw=bytearray(good+b'\0');struct.pack_into('<I',raw,12,len(raw))
        with self.assertRaises(ValueError):self.verify(signed(raw))
        for offset in (56,88,152,192,288,300):
            raw=bytearray(good);raw[offset]^=255
            with self.assertRaises(ValueError):self.verify(raw)

    def test_aot_claims_source_identity_and_native_header(self):
        native=b'\0aot\x05\0\0\0'+b'x'*21
        good=package._assemble(WASM,b'',native=native,native_metadata=meta(WASM),**KW)
        checked=self.verify(good);self.assertEqual(checked['aot_validation'],'not_performed')
        offset=struct.unpack_from('<I',good,280)[0]
        for position,kind,value in ((0,'H',2),(2,'H',255),(4,'I',0),(8,'I',3),(12,'B',1),(16,'B',65),
                                    (23,'B',1),(216,'B',255),(248,'B',1),(256,'B',1),(260,'I',6)):
            raw=bytearray(good);struct.pack_into('<'+kind,raw,offset+position,value)
            with self.subTest(position=position),self.assertRaises(ValueError):self.verify(signed(raw))
        for begin,end in ((48,68),(68,88),(88,120),(120,152),(152,184),(184,216),(216,248)):
            raw=bytearray(good);raw[offset+begin:offset+end]=bytes(end-begin)
            with self.assertRaises(ValueError):self.verify(signed(raw))

    def test_total_cap_counts_only_included_sections(self):
        native=b'\0aot\x05\0\0\0'+b'x'*21
        wasm=WASM+bytes(1048576)
        # The omitted compiler input does not consume package storage.
        assets=bytes(package.MAX_PACKAGE_SIZE-(256+32+256+len(native)+3))
        raw=package._assemble(wasm,assets,native=native,native_metadata=meta(wasm),include_wasm=False,**KW)
        self.assertEqual(len(raw),package.MAX_PACKAGE_SIZE);self.verify(raw)
        with self.assertRaises(ValueError):
            package._assemble(wasm,assets+b'x',native=native,native_metadata=meta(wasm),include_wasm=False,**KW)

if __name__=='__main__':unittest.main()
