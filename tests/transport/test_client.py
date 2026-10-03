import asyncio, contextlib, hashlib, struct, sys, unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'protocol'))
import client

def package():
    p=bytearray(300);p[:8]=b'TRPKG001';struct.pack_into('<HH5I',p,8,1,256,len(p),256,1,16,0);struct.pack_into('<I',p,12,len(p));struct.pack_into('<I',p,32,2);p[56:63]=b'counter'
    return bytes(p)

class CodecTests(unittest.TestCase):
    def test_frames_match_literal_and_reject_stale_tail(self):
        frames=client.frames(0x10,0x1234,b'')
        self.assertEqual(frames,[bytes.fromhex('a7011000341200000000')])
        rx=client.Decoder(); payload=bytes(range(72))
        packets=client.frames(0x13,4,payload)
        self.assertTrue(all(len(p)<=20 for p in packets))
        result=None
        for p in packets: result=rx.feed(p)
        self.assertEqual(result,(0x13,4,False,payload))
        with self.assertRaises(ValueError):rx.feed(packets[-1])
    def test_package_identity_whole_hash_and_bad_header(self):
        data=package(); ident=client.package_info(data)
        self.assertEqual(ident[:32],b'counter'+bytes(25));self.assertEqual(ident[32:36],b'\x02\0\0\0')
        self.assertEqual(ident[36:68],hashlib.sha256(data).digest());self.assertEqual(ident[68:],b',\x01\0\0')
        for off in (0,12,56):
            bad=bytearray(data);bad[off]=0xff
            with self.assertRaises(ValueError):client.package_info(bytes(bad))

class FakePeer:
    def __init__(self,mode): self.mode=mode;self.installed=False;self.transfers=0;self.connections=0
    @contextlib.asynccontextmanager
    async def connect(self):
        self.connections+=1
        yield self
    async def management(self,op,payload=b''):
        if op==client.HELLO:return struct.pack('<HIBB',1,303104,2,71)
        if op==client.QUERY:
            if not self.installed:raise client.RemoteError(8)
            return client.package_info(package())
        if op==client.PREPARE:return b''
        raise AssertionError(op)
    async def transfer(self,data,progress=None):
        self.transfers+=1
        if self.mode=='finish_ack_lost':self.installed=True;raise TimeoutError('lost final acknowledgement')
        if self.mode=='disconnect_once' and self.transfers==1:raise ConnectionError('radio loss')
        if self.mode=='reject':raise client.RemoteError(9)
        self.installed=True

class InstallTests(unittest.IsolatedAsyncioTestCase):
    async def test_windows_cancel_after_commit_only_reconciles_identity(self):
        class WindowsCancel(FakePeer):
            async def transfer(self,data,progress=None):
                self.transfers+=1;self.installed=True
                error=OSError('operation cancelled');error.winerror=-2147023673;raise error
        p=WindowsCancel('ok')
        self.assertEqual(await client.install(p.connect,package()),'installed')
        self.assertEqual(p.transfers,1);self.assertEqual(p.connections,2)

    async def test_windows_cancel_without_commit_does_not_retransmit(self):
        class WindowsCancel(FakePeer):
            async def transfer(self,data,progress=None):
                self.transfers+=1
                error=OSError('operation cancelled');error.winerror=-2147023673;raise error
        p=WindowsCancel('ok')
        with self.assertRaises(OSError):await client.install(p.connect,package())
        self.assertEqual(p.transfers,1)

    async def test_windows_cancel_during_disconnect_preserves_confirmed_success(self):
        class WindowsCancel(FakePeer):
            @contextlib.asynccontextmanager
            async def connect(self):
                self.connections+=1;yield self
                error=OSError('operation cancelled');error.winerror=-2147023673;raise error
        p=WindowsCancel('ok')
        self.assertEqual(await client.install(p.connect,package()),'installed')
        self.assertEqual(p.transfers,1);self.assertEqual(p.connections,1)

    async def test_quarantine_can_repair_once_but_final_query_is_strict(self):
        class Quarantined(FakePeer):
            async def management(self,op,payload=b''):
                if op==client.QUERY and not self.installed:raise client.RemoteError(9)
                return await super().management(op,payload)
        p=Quarantined('ok')
        self.assertEqual(await client.install(p.connect,package()),'installed')
        self.assertEqual(p.transfers,1)
        p=Quarantined('reject')
        with self.assertRaises(client.RemoteError):await client.install(p.connect,package())
        self.assertEqual(p.transfers,1)
        with self.assertRaises(client.RemoteError):await client.installed(p,client.package_info(package()))

    async def test_storage_error_never_starts_repair(self):
        class Unreadable(FakePeer):
            async def management(self,op,payload=b''):
                if op==client.QUERY:raise client.RemoteError(3)
                return await super().management(op,payload)
        p=Unreadable('ok')
        with self.assertRaises(client.RemoteError):await client.install(p.connect,package())
        self.assertEqual(p.transfers,0)

    async def test_finish_ack_loss_queries_persistent_identity(self):
        p=FakePeer('finish_ack_lost')
        self.assertEqual(await client.install(p.connect,package()),'installed')
        self.assertEqual(p.transfers,1);self.assertEqual(p.connections,2)
    async def test_disconnect_restarts_uncommitted_transfer_once(self):
        p=FakePeer('disconnect_once')
        self.assertEqual(await client.install(p.connect,package()),'installed')
        self.assertEqual(p.transfers,2)
    async def test_rejected_signature_not_retried_or_success(self):
        p=FakePeer('reject')
        with self.assertRaises(client.RemoteError):await client.install(p.connect,package())
        self.assertEqual(p.transfers,1)
    async def test_already_installed_never_transmits(self):
        p=FakePeer('ok');p.installed=True
        self.assertEqual(await client.install(p.connect,package()),'already-installed');self.assertEqual(p.transfers,0)


class Gatt:
    def __init__(self,drop=False):self.notifications={};self.rx=client.Decoder();self.ops=[];self.data=bytearray();self.drop=drop
    async def start_notify(self,uuid,callback):self.notifications[uuid]=callback
    async def write_gatt_char(self,uuid,data,response):
        if uuid==client.MGMT:
            message=self.rx.feed(data)
            if message:
                op,rid,_,_=message
                for packet in client.frames(op,rid,b'\0\0*',True):self.notifications[client.MGMT](None,packet)
            return
        if uuid==client.DATA:
            if response:raise AssertionError('DATA must be write command')
            self.data.extend(data);return
        self.ops.append(data[0]);op=data[0]
        if op==1:
            if data!=bytes.fromhex('010900000002'):raise AssertionError('wrong target or length')
            ack=bytes.fromhex('0101000000000000')
        elif op==4:
            if data!=bytes.fromhex('04090000002639f4cb'):raise AssertionError('CRC golden mismatch')
            ack=bytes.fromhex('0401000009000000')
        else:
            if self.drop:return
            ack=bytes.fromhex('0201000009000000')
        self.notifications[client.CTRL](None,ack)

class LinkTests(unittest.IsolatedAsyncioTestCase):
    async def test_actual_link_frames_management(self):
        g=Gatt();link=client.Link(g,timeout=0.1);await link.start()
        self.assertEqual(await link.management(client.PREPARE,bytes(72)),b'*')
    async def test_actual_link_targets_package_and_checks_crc(self):
        g=Gatt();link=client.Link(g,timeout=0.1);await link.start()
        await link.transfer(b'123456789')
        self.assertEqual(g.ops,[1,4,2]);self.assertEqual(g.data,b'123456789')
    async def test_actual_link_lost_finish_has_bounded_timeout(self):
        g=Gatt(drop=True);link=client.Link(g,timeout=0.005);await link.start()
        with self.assertRaises(TimeoutError):await link.transfer(b'123456789')
        self.assertEqual(g.ops,[1,4,2])
        with self.assertRaises(RuntimeError):await link.management(client.HELLO)

if __name__=='__main__':unittest.main()
