"""Independent wire fixtures: schema-1 section metadata and strict bounds."""
import contextlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'protocol'))
import client


def info(size):
    return b'demo.native'.ljust(32,b'\0')+struct.pack('<I',2)+bytes(range(32))+struct.pack('<I',size)


def details(fmt=1,wasm=11,aot=29,assets=3,backend=2,fallback=0,cover=False):
    sections=int(bool(wasm)) | (2 if aot else 0) | (4 if assets else 0) | (8 if cover else 0)
    size=256+16*sections.bit_count()
    for length in (wasm,256+aot if aot else 0,assets,133232 if cover else 0):
        if length:size=(size+3)//4*4+length
    raw=bytearray(248)
    struct.pack_into('<HHBBH',raw,0,1,fmt,backend,fallback,sections)
    raw[8:80]=info(size)
    raw[80:144]='原生'.encode().ljust(64,b'\0')
    struct.pack_into('<10I',raw,144,42,1,15,16,100000,wasm,assets,aot,5 if aot else 0,7 if aot else 0)
    if aot:
        raw[184:200]=b'xtensa'.ljust(16,b'\0');raw[200:216]=b'esp32s3'.ljust(16,b'\0');raw[216:248]=bytes(range(32))
    return bytes(raw)


def profile(aot=True,development=False):
    raw=bytearray(136);struct.pack_into('<HHI',raw,0,1,1,1|(2 if aot else 0)|(4 if development else 0))
    if aot:
        struct.pack_into('<II',raw,8,5,1)
        raw[16:32]=b'xtensa'.ljust(16,b'\0');raw[32:48]=b'esp32s3'.ljust(16,b'\0')
        raw[48:80]=bytes(range(32));raw[80:112]=bytes(range(1,33));raw[112:132]=bytes(range(20))
        struct.pack_into('<I',raw,132,7)
    return bytes(raw)


class Peer:
    def __init__(self):self.flags=127;self.details=details();self.profile=profile();self.calls=[]
    async def management(self,op,payload=b''):
        self.calls.append((op,payload))
        if op==16:return struct.pack('<HIBB',1,2097152,16,self.flags)
        if op==26:return self.details
        if op==27:return self.profile
        raise AssertionError(op)


class SectionQueries(unittest.IsolatedAsyncioTestCase):
    async def test_cover_bit_accounts_for_signed_section_and_alignment(self):
        peer=Peer();peer.details=details(cover=True)
        result=await client.app_info(peer,peer.details[8:80])
        self.assertEqual(result['cover_size'],133232)
        self.assertEqual(result['section_bits'],15)

    async def test_full_details_response_uses_real_mtu23_fragment_decoder(self):
        class Gatt:
            def __init__(self):self.notifications={};self.decoder=client.Decoder();self.peer=Peer();self.fragments=0
            async def start_notify(self,uuid,callback):self.notifications[uuid]=callback
            async def write_gatt_char(self,uuid,packet,response):
                request=self.decoder.feed(packet)
                if request:
                    op,rid,_,payload=request
                    reply=await self.peer.management(op,payload)
                    for fragment in client.frames(op,rid,b'\0\0'+reply,True):
                        self.fragments+=1;self.notifications[client.MGMT](None,fragment)
        gatt=Gatt();link=client.Link(gatt,timeout=1);await link.start()
        value=await client.app_info(link,details()[8:80])
        self.assertEqual(value['aot_size'],29)
        self.assertEqual(gatt.fragments,26) # HELLO status+8, then APP_INFO status+248.

    async def test_all_package_shapes_and_fallbacks(self):
        for fmt,wasm,aot,assets,backend,fallback in ((1,11,0,3,1,0),(1,11,0,0,1,0),(1,11,0,3,1,0),
                (1,0,29,0,2,0),(1,11,29,3,2,0),*( (1,11,29,0,1,f) for f in (1,2,3) )):
            with self.subTest(fmt=fmt,wasm=wasm,aot=aot,fallback=fallback):
                peer=Peer();peer.details=details(fmt,wasm,aot,assets,backend,fallback)
                identity=peer.details[8:80];value=await client.app_info(peer,identity)
                self.assertEqual(peer.calls,[(16,b''),(26,b'\x01\0'+identity[:68])])
                self.assertEqual((value['package_format'],value['selected_backend'],value['fallback_reason']),(fmt,backend,fallback))
                self.assertEqual((value['wasm_size'],value['aot_size'],value['assets_size']),(wasm,aot,assets))
                self.assertEqual(value['title'],'原生')

    async def test_unknown_invalid_inconsistent_details_rejected(self):
        mutations=[(0,'H',3),(2,'H',3),(4,'B',0),(4,'B',3),(5,'B',4),(5,'B',1),(6,'H',15),
            (40,'I',3),(76,'I',1000),(148,'I',2),(152,'I',16),(156,'I',0),(160,'I',100001),
            (164,'I',8),(164,'I',0xffffffff),(168,'I',0),(172,'I',0),(176,'I',0),(180,'I',3),
            (184,'B',65),(191,'B',65),(200,'B',255)]
        for offset,kind,value in mutations:
            peer=Peer();raw=bytearray(peer.details);struct.pack_into('<'+kind,raw,offset,value);peer.details=raw
            with self.subTest(offset=offset),self.assertRaises(ValueError):await client.app_info(peer,details()[8:80])
        for changed in (details()[:247],details()+b'\0',details()[:216]+bytes(32),details()[:80]+bytes(64)+details()[144:]):
            peer=Peer();peer.details=changed
            with self.assertRaises(ValueError):await client.app_info(peer,details()[8:80])
        for raw in (details(2,11,29),details(1,0,29,0,1,1),details(1,11,0,0,2,0),details(1,11,29,0,1,0)):
            peer=Peer();peer.details=raw
            with self.assertRaises(ValueError):await client.app_info(peer,raw[8:80])

    async def test_runtime_enabled_disabled_and_development(self):
        for aot,development in ((False,False),(True,False),(True,True)):
            peer=Peer();peer.profile=profile(aot,development);value=await client.runtime_info(peer)
            self.assertEqual(peer.calls,[(16,b''),(27,b'')]);self.assertTrue(value['package_sections'])
            self.assertEqual((value['aot_enabled'],value['development_aot']),(aot,development))
            self.assertEqual(value['compat_id'],bytes(range(32)).hex() if aot else bytes(32).hex())
        peer=Peer();peer.flags=63
        with self.assertRaises(ValueError):await client.runtime_info(peer)
        self.assertEqual(peer.calls,[(16,b'')])

    async def test_runtime_unknown_malformed_or_unadvertised_fields_rejected(self):
        mutations=[(0,'H',2),(1,'H',7),(1,'H',1),(4,'I',9),(4,'I',2),(8,'I',0),(12,'I',0),
                   (16,'B',65),(23,'B',65),(32,'B',255),(132,'I',3)]
        for offset,kind,value in mutations:
            peer=Peer();raw=bytearray(peer.profile);struct.pack_into('<'+kind,raw,offset,value);peer.profile=raw
            with self.subTest(offset=offset),self.assertRaises(ValueError):await client.runtime_info(peer)
        for raw in (profile()[:135],profile()+b'\0',profile(False,True),profile()[:48]+bytes(32)+profile()[80:],
                    profile()[:80]+bytes(32)+profile()[112:],profile()[:112]+bytes(20)+profile()[132:]):
            peer=Peer();peer.profile=raw
            with self.assertRaises(ValueError):await client.runtime_info(peer)
        peer=Peer();raw=bytearray(profile(False));raw[16]=1;peer.profile=raw
        with self.assertRaises(ValueError):await client.runtime_info(peer)

    async def test_section_install_requires_capability_before_any_probe_or_transfer(self):
        data=bytearray(300);data[:8]=b'TRPKG001';struct.pack_into('<HH5I',data,8,1,256,300,256,1,16,0)
        struct.pack_into('<I',data,32,2);data[56:67]=b'demo.native'
        expected=client.package_info(data)
        class InstallPeer(Peer):
            @contextlib.asynccontextmanager
            async def connect(self):yield self
            async def management(self,op,payload=b''):
                if op==18:self.calls.append((op,payload));return expected
                return await super().management(op,payload)
        peer=InstallPeer();peer.flags=63
        with self.assertRaises(ValueError):await client.install(peer.connect,data)
        self.assertEqual(peer.calls,[(16,b'')])
        peer=InstallPeer();self.assertEqual(await client.install(peer.connect,data),'already-installed')
        self.assertEqual(peer.calls[-1],(18,expected[:68]))


if __name__=='__main__':unittest.main()
