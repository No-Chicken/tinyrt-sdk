import asyncio
import argparse
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'protocol'))
import client
spec=importlib.util.spec_from_file_location('query_cli',ROOT/'tools/ble_install.py')
cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)

def identity():
    return b'demo.game'.ljust(32,b'\0')+struct.pack('<I',7)+bytes(range(32))+struct.pack('<I',334)

def storage():
    raw=bytearray(92);struct.pack_into('<HHQ',raw,0,1,1,0xfedcba9876543210)
    struct.pack_into('<7I4H11I',raw,12,0x4e0000,0x4de000,300,4096,0x4dd000,0x4dd000,0x200000,
        16,1,0,0,65536,300000,200000,100000,8000000,6000000,5000000,2097152,123456,234567,5242880)
    return bytes(raw)

def details():
    raw=bytearray(248);struct.pack_into('<HHBBH',raw,0,1,1,1,0,5)
    raw[8:80]=identity();title='迷宫 🐍'.encode();raw[80:80+len(title)]=title
    struct.pack_into('<10I',raw,144,42,1,15,16,100000,30,14,0,0,0)
    return bytes(raw)

class Peer:
    def __init__(self):self.raw=storage();self.info=details();self.flags=127;self.calls=[]
    async def management(self,op,payload=b''):
        self.calls.append((op,payload))
        if op==16:return struct.pack('<HIBB',1,0x200000,16,self.flags)
        if op==25:return self.raw
        if op==26:
            self.asserted=payload
            return self.info
        if op==27:return struct.pack('<HHI',1,1,1)+bytes(128)
        if op==24:return struct.pack('<QBBB',1,1,0,1)+identity()
        raise AssertionError(op)

class Queries(unittest.IsolatedAsyncioTestCase):
    async def test_empty_full_fragmented_and_sixteen_app_space(self):
        for count,used,allocated,largest,quarantined in ((0,0,0,0x4de000,0),(5,0x4de000,0x4de000,0,1),
                (3,0x2de000,0x2de000,0x100000,1),(16,4800,65536,0x4ce000,2)):
            peer=Peer();raw=bytearray(peer.raw)
            struct.pack_into('<4I',raw,20,used,allocated,0x4de000-allocated,largest)
            struct.pack_into('<2H',raw,42,count,quarantined);peer.raw=raw
            value=await client.storage_info(peer);self.assertEqual(value['installed_count'],count)
            self.assertEqual(value['largest_free_bytes'],largest)

    async def test_remote_busy_and_quarantine_are_not_retried_or_metadata(self):
        class Reject(Peer):
            async def management(self,op,payload=b''):
                if op==16:return await super().management(op,payload)
                self.calls.append((op,payload));raise client.RemoteError(6 if op==25 else 9)
        for query in (lambda p:client.storage_info(p),lambda p:client.app_info(p,identity())):
            peer=Reject()
            with self.assertRaises(client.RemoteError):await query(peer)
            self.assertEqual(len(peer.calls),2)

    async def test_storage_exact_fields_and_lossless_generation(self):
        peer=Peer();value=await client.storage_info(peer)
        self.assertEqual(value['generation'],'fedcba9876543210')
        self.assertEqual((value['package_bytes'],value['allocated_bytes'],value['free_bytes']),(300,4096,0x4dd000))
        self.assertEqual(value['runtime_heap_peak'],234567)
        self.assertEqual(peer.calls,[(16,b''),(25,b'')])

    async def test_storage_rejects_bad_schema_lengths_reserved_and_arithmetic(self):
        cases=[(0,'H',2),(2,'H',2),(46,'H',1),(12,'I',8191),(16,'I',0x4de001),
            (20,'I',4097),(24,'I',4095),(28,'I',0),(32,'I',0x4de000),(36,'I',300),
            (40,'H',17),(42,'H',17),(44,'H',2),(56,'I',300001),(60,'I',200001),
            (68,'I',8000001),(72,'I',6000001),(80,'I',234568),(84,'I',2097153),
            (20,'I',0xffffffff),(24,'I',0xffffffff),(28,'I',0xffffffff)]
        for offset,kind,value in cases:
            with self.subTest(offset=offset):
                peer=Peer();raw=bytearray(peer.raw);struct.pack_into('<'+kind,raw,offset,value);peer.raw=raw
                with self.assertRaises(ValueError):await client.storage_info(peer)
        for length in (0,91,93):
            peer=Peer();peer.raw=(peer.raw+b'\0')[:length]
            with self.assertRaises(ValueError):await client.storage_info(peer)

    async def test_invalid_ram_flag_hides_unavailable_counters_and_old_host_rejects(self):
        peer=Peer();raw=bytearray(peer.raw);struct.pack_into('<H',raw,2,0);raw[52:76]=b'\xff'*24;peer.raw=raw
        value=await client.storage_info(peer);self.assertFalse(value['ram_valid']);self.assertIsNone(value['internal_ram_free_bytes'])
        self.assertEqual(value['runtime_heap_used'],123456)
        struct.pack_into('<I',raw,80,234568)
        with self.assertRaises(ValueError):await client.storage_info(peer)
        peer=Peer();peer.flags=31
        with self.assertRaises(ValueError):await client.storage_info(peer)
        self.assertEqual(len(peer.calls),1)

    async def test_info_checks_exact_identity_and_policy(self):
        peer=Peer();value=await client.app_info(peer,identity())
        self.assertEqual(value['title'],'迷宫 🐍');self.assertEqual(value['key_id'],42)
        self.assertEqual(value['sha256'],bytes(range(32)).hex());self.assertEqual(peer.asserted,b'\x01\0'+identity()[:68])
        self.assertEqual(value['wasm_size']+value['assets_size']+290,value['size'])
        for offset,kind,value in ((40,'I',8),(44,'B',255),(76,'I',301),(148,'I',2),(152,'I',16),(156,'I',0),(156,'I',17),(160,'I',0),(160,'I',100001),(164,'I',8),(168,'I',15),(164,'I',0xffffffff),(168,'I',0xffffffff)):
            with self.subTest(offset=offset):
                peer=Peer();raw=bytearray(peer.info);struct.pack_into('<'+kind,raw,offset,value);peer.info=raw
                with self.assertRaises(ValueError):await client.app_info(peer,identity())

    async def test_cli_healthy_id_resolution_and_new_commands(self):
        self.assertEqual(await cli.details_identity(Peer(),'demo.game'),identity())
        with self.assertRaises(ValueError):await cli.details_identity(Peer(),'demo.missing')
        seen=[]
        async def run(args):seen.append((args.command,args.package,args.app_id))
        with patch.object(cli,'run',run):
            self.assertEqual(await asyncio.to_thread(cli.main,['storage','--address','test']),0)
            self.assertEqual(await asyncio.to_thread(cli.main,['runtime','--address','test']),0)
            self.assertEqual(await asyncio.to_thread(cli.main,['info','--app-id','demo.game','--address','test']),0)
            self.assertEqual(await asyncio.to_thread(cli.main,['info','game.trpkg','--address','test']),0)
        self.assertEqual(seen,[('storage',None,None),('runtime',None,None),('info',None,'demo.game'),('info','game.trpkg',None)])

    async def test_cli_runs_storage_and_both_details_routes_over_mock_transport(self):
        class Device:
            def __init__(self,*args,**kwargs):pass
            async def __aenter__(self):return self
            async def __aexit__(self,*args):pass
        class LinkPeer(Peer):
            async def start(self):pass
        bleak=types.SimpleNamespace(BleakClient=Device,BleakScanner=object,BleakError=type('BleakError',(Exception,),{}))
        with tempfile.TemporaryDirectory() as folder:
            package=bytearray(334);package[:8]=b'TRPKG001';struct.pack_into('<HH5I',package,8,1,256,334,256,1,16,0)
            struct.pack_into('<I',package,32,7);package[56:65]=b'demo.game'
            path=Path(folder)/'game.trpkg';path.write_bytes(package)
            for command,filename,app_id in (('storage',None,None),('runtime',None,None),('info',None,'demo.game'),('info',str(path),None)):
                peer=LinkPeer()
                if command=='runtime':peer.flags=127
                if filename:peer.info=peer.info[:8]+client.package_info(package)+peer.info[80:]
                args=argparse.Namespace(command=command,package=filename,app_id=app_id,address='test',scan=False,pair=False,pin=None,timeout=1,fragment=20)
                output=io.StringIO()
                with patch.dict(sys.modules,{'bleak':bleak}),patch.object(cli.protocol,'Link',lambda *a,**k:peer),contextlib.redirect_stdout(output):
                    await cli.run(args)
                value=json.loads(output.getvalue())
                self.assertEqual(value['schema'] if command in ('storage','runtime') else value['title'],1 if command in ('storage','runtime') else '迷宫 🐍')

if __name__=='__main__':unittest.main()
