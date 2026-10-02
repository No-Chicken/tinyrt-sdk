import asyncio
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'protocol'))
import client as p
ID=b'demo.cover'.ljust(32,b'\0')+struct.pack('<I',1)+bytes(32)
COVER=struct.pack('<8sHHIHHHHII',b'TRCOV001',1,1,32,210,210,150,150,88200,45000)+bytes(133200)
class Mobile(unittest.IsolatedAsyncioTestCase):
    async def test_legacy_and_confirmed_operation(self):
        class Link:
            calls=[]
            async def management(self,op,payload=b''):
                self.calls.append(op)
                if op==p.PREPARE:return struct.pack('<HBBI',1,2,0,7)
                return struct.pack('<HBBIHH',1,2,1 if len(self.calls)==2 else 3,7,6 if len(self.calls)==2 else 0,0)
        link=Link();await p.confirmed_operation(link,p.PREPARE,b'',poll_interval=0)
        self.assertEqual(link.calls,[p.PREPARE,p.MOBILE_STATUS,p.MOBILE_STATUS])
        class Legacy:
            async def management(self,*args):return b''
        self.assertEqual(await p.confirmed_operation(Legacy(),p.UNINSTALL,ID),b'')
    async def test_cancelled_timeout_wrong_token_and_failed_result(self):
        for state,result,token in ((4,6,7),(5,6,7),(3,5,7),(3,0,8),(1,0,7)):
            class Link:
                async def management(self,op,payload=b''):
                    return struct.pack('<HBBI',1,1,0,7) if op==p.UNINSTALL else struct.pack('<HBBIHH',1,1,state,token,result,0)
            with self.subTest(state=state,result=result,token=token),self.assertRaises((p.RemoteError,ValueError)):
                await p.confirmed_operation(Link(),p.UNINSTALL,ID,poll_interval=0)
    async def test_cover_integrity_and_bounds(self):
        class Link:
            mutate=None
            async def management(self,op,payload=b''):
                if op==p.HELLO:return struct.pack('<HIBB',1,0x200000,16,128)
                offset,count=struct.unpack_from('<IH',payload,68);chunk=COVER[offset:offset+count]
                raw=bytearray(struct.pack('<HHIIHH',1,1,len(COVER),offset,len(chunk),0)+hashlib.sha256(COVER).digest()+chunk)
                if self.mutate:self.mutate(raw,offset)
                return bytes(raw)
        cover=await p.app_cover(Link(),ID)
        self.assertEqual(cover['data'],COVER);self.assertEqual(len(cover['side_pixels']),45000)
        def bad_hash(raw,offset):raw[16]^=1
        def zero_progress(raw,offset):struct.pack_into('<H',raw,12,0)
        def oversized(raw,offset):struct.pack_into('<I',raw,4,0xffffffff)
        def bad_cursor(raw,offset):struct.pack_into('<I',raw,8,offset+1)
        for mutate in (bad_hash,zero_progress,oversized,bad_cursor):
            link=Link();link.mutate=mutate
            with self.assertRaises(ValueError):await p.app_cover(link,ID)

    async def test_install_capacity_blocks_prepare_and_transfer_for_full_new_extent(self):
        from unittest.mock import AsyncMock,patch
        from test_client import FakePeer,package
        class CapacityPeer(FakePeer):
            prepares=0
            async def management(self,op,payload=b''):
                if op==p.HELLO:return struct.pack('<HIBB',1,0x200000,16,103)
                if op==p.PREPARE:self.prepares+=1
                return await super().management(op,payload)
        data=bytearray(package()+bytes(3797));struct.pack_into('<I',data,12,len(data))
        peer=CapacityPeer('ok')
        with patch.object(p,'storage_info',new=AsyncMock(return_value={'free_bytes':8192,'largest_free_bytes':4096})):
            with self.assertRaises(p.RemoteError) as caught:await p.install(peer.connect,bytes(data))
        self.assertEqual(caught.exception.status,5);self.assertEqual(peer.prepares,0);self.assertEqual(peer.transfers,0)
