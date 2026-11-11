import asyncio
import contextlib
import json
import struct
import sys
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

SDK = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(SDK/'protocol'), str(SDK/'tools')]
import client
import device
import eeot
from test_client import FakePeer, package


class FakeSerial:
    """Short writes, split replies, serial console text and unrelated sequences."""
    def __init__(self):
        self.reader = eeot.FrameDecoder()
        self.inbound = bytearray()
        self.calls = []

    def write(self, raw):
        part = raw[:7]
        for kind, seq, payload in self.reader.feed(part):
            self.calls.append((kind, payload))
            self.inbound += b'console\n' + eeot.encode_frame(kind|128, seq-1, b'stale')
            info = json.dumps(dict(schema=1, firmware='test', perm=1)).encode()
            self.inbound += eeot.encode_frame(kind|128, seq, struct.pack('<I',0)+info)
        return len(part)

    def read(self, size):
        assert size <= max(1, self.in_waiting)
        data = bytes(self.inbound[:size])
        del self.inbound[:size]
        return data

    @property
    def in_waiting(self):
        return min(5, len(self.inbound))


class TransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_split_info_and_short_write(self):
        link = eeot.USBApplicationLink('COM8')
        link.link = object.__new__(eeot.USBTransport)
        link.link.serial = FakeSerial()
        link.link.sequence = 20
        link.link.decoder = eeot.FrameDecoder()
        info = json.loads(await link.management(client.GET_DEVICE_INFO))
        self.assertEqual(info['perm'], 1)
        self.assertEqual(link.link.serial.calls, [(7,b'')])

    async def test_usb_package_multiblock_offsets_and_cancellation(self):
        image = bytes(range(256)) * 15 + b'last block'
        class Receiver:
            def __init__(self, reject):
                self.reject = reject
                self.received = bytearray()
                self.ops = []
            def request(self, kind, payload):
                self.ops.append(kind)
                if kind == eeot.START:
                    target, size, nonce = struct.unpack('<BII',payload)
                    assert target == 2 and size == len(image) and nonce
                    return 0,7,0,1
                if kind == eeot.DATA:
                    session,offset,crc = struct.unpack_from('<III',payload)
                    block = payload[12:]
                    assert session == 7 and offset == len(self.received) and crc == zlib.crc32(block)
                    assert len(eeot.encode_frame(kind, 1, payload)) <= 4096
                    self.received.extend(block)
                    return int(self.reject),7,len(self.received),1
                assert struct.unpack('<I',payload)[0] == 7
                return 0,7,len(self.received),2
        for reject in (False, True):
            receiver = Receiver(reject)
            link = eeot.USBApplicationLink('COM8')
            link.link = receiver
            if reject:
                with self.assertRaises(RuntimeError): await link.transfer(image)
                self.assertEqual(receiver.ops,[eeot.START,eeot.DATA,eeot.CANCEL])
            else:
                self.assertEqual(await link.transfer(image),len(image))
                self.assertEqual(receiver.received,image)
                self.assertEqual(receiver.ops,[eeot.START,eeot.DATA,eeot.DATA,eeot.FINISH])

    async def test_prepare_permissions_and_same_connection_launch(self):
        class Peer(FakePeer):
            prepared = False
            launched = False
            async def management(self, op, payload=b''):
                if op == client.GET_DEVICE_INFO: return b'{"schema":1,"perm":1}'
                if op == client.PREPARE_PERMISSIONS:
                    self.prepared = payload == client.package_info(package())+package()[40:44]
                    return b''
                if op == client.LAUNCH:
                    self.launched = self.installed and self.prepared
                    return b''
                return await super().management(op,payload)
        peer = Peer('ok')
        self.assertEqual(await client.install(peer.connect,package(),launch=True),'installed')
        self.assertTrue(peer.launched)
        self.assertEqual(peer.connections,1)

    async def test_watched_log_reopens_same_port(self):
        with patch.object(device,'print_logs',side_effect=[OSError('busy'), None]) as logs, patch.object(asyncio,'sleep'):
            await device.follow_logs('COM8',lambda:False)
        self.assertEqual([args.args[0] for args in logs.call_args_list],['COM8','COM8'])


class DebugTests(unittest.TestCase):
    def test_single_log_line_is_immediate_and_split_magic_is_retained(self):
        reader = device.debug.FrameReader()
        reader.feed(b'I app: final line\nEE')
        self.assertEqual(reader.log,b'I app: final line\n')
        frame = device.debug.encode(device.debug.STATUS,7,b'{}')
        self.assertEqual(reader.feed(frame[2:]),[(device.debug.STATUS,7,b'{}')])

    def test_auto_checks_all_debug_candidates(self):
        with patch.object(eeot,'usb_ports',return_value=['COM7','COM8']), patch.object(eeot,'USBTransport',side_effect=OSError):
            self.assertEqual(eeot.resolve_transport('auto',debug_probe=lambda p:p=='COM8'),('usb','COM8'))
            self.assertEqual(eeot.resolve_transport('auto',debug_probe=lambda p:False),('ble',None))
            with self.assertRaisesRegex(RuntimeError,'多台'):
                eeot.resolve_transport('auto',debug_probe=lambda p:True)

    def test_visible_session_after_install_and_lost_launch_reply(self):
        class Bridge:
            reader = device.debug.FrameReader()
            launched = 0
            back = False
            def json(self, op):
                if not self.back: return dict(session=1,page='app_install',app_state=1)
                if not self.launched: return dict(session=1,page='apps',app_state=1)
                return dict(session=2,page='app',app_state=3)
            def key(self,key): self.back = key=='short'
            def call(self,op,payload):
                self.launched += 1
                raise TimeoutError('launch ACK lost')
        bridge = Bridge()
        with patch.object(device,'open_debug',side_effect=lambda port:contextlib.nullcontext(bridge)), patch.object(device.time,'sleep'):
            device.launch_debug('COM8','demo','already-installed')
        self.assertEqual(bridge.launched,1)
        self.assertTrue(bridge.back)
