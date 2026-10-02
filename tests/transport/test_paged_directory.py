"""Real directory consumer against literal v1 wire responses, without BLE."""
import contextlib
import importlib.util
from pathlib import Path
import struct
import unittest

source = Path(__file__).resolve().parents[2] / 'tools/ble_install.py'
spec = importlib.util.spec_from_file_location('paged_cli', source)
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


def record(index):
    return (f'demo.app{index:02}'.encode().ljust(32, b'\0') + struct.pack('<I', 1)
            + bytes([index + 1]) * 32 + struct.pack('<I', 300))


class Peer:
    def __init__(self, total=16, flags=31):
        self.rows = [record(i) for i in range(total)]
        self.flags = flags
        self.calls = []
        self.generation = 0xfedcba9876543210
        self.failures = 0
        self.corrupt = None

    async def management(self, opcode, payload=b''):
        self.calls.append((opcode, payload))
        if opcode == 0x10:
            return struct.pack('<HIBB', 1, 2097152, 16 if self.flags & 16 else 2, self.flags)
        if opcode in (0x11, 0x17):
            if self.flags & 16:
                raise AssertionError('new host requires paged inventory')
            return bytes([len(self.rows)]) + b''.join(self.rows)
        if opcode != 0x18 or len(payload) != 10:
            raise AssertionError('wrong paged request')
        kind, offset, generation = struct.unpack('<BBQ', payload)
        if kind not in (0, 1) or (offset == 0 and generation) or (offset and generation != self.generation):
            raise AssertionError('wrong cursor or generation')
        if offset and self.failures:
            self.failures -= 1
            self.generation += 1
            raise cli.protocol.RemoteError(7)
        rows = self.rows[offset:offset + 3]
        result = struct.pack('<QBBB', self.generation, len(self.rows), offset, len(rows)) + b''.join(rows)
        return self.corrupt(result, offset) if self.corrupt else result


class DirectoryTests(unittest.IsolatedAsyncioTestCase):
    async def test_all_six_pages_keep_exact_identities_and_u64_generation(self):
        peer = Peer()
        self.assertEqual(await cli.directory_records(peer, 0x11), peer.rows)
        requests = [p for op, p in peer.calls if op == 0x18]
        self.assertEqual([p[1] for p in requests], [0, 3, 6, 9, 12, 15])
        self.assertEqual(requests[0], bytes(10))
        self.assertEqual(requests[1], b'\0\3' + bytes.fromhex('1032547698badcfe'))

    async def test_quarantine_inventory_and_removal_use_paging(self):
        peer = Peer()
        self.assertEqual(await cli.directory_records(peer, 0x17), peer.rows)
        self.assertTrue(all(payload[0] == 1 for op, payload in peer.calls if op == 0x18))

    async def test_generation_conflict_restarts_and_discards_previous_pages(self):
        peer = Peer(); peer.failures = 1
        self.assertEqual(await cli.directory_records(peer, 0x11), peer.rows)
        self.assertEqual([p[1] for op, p in peer.calls if op == 0x18], [0, 3, 0, 3, 6, 9, 12, 15])

    async def test_repeated_conflicts_stop_after_three_attempts(self):
        peer = Peer(); peer.failures = 99
        with self.assertRaises(cli.protocol.RemoteError):
            await cli.directory_records(peer, 0x11)
        self.assertEqual(len([op for op, _ in peer.calls if op == 0x18]), 6)

    async def test_legacy_host_and_empty_inventory(self):
        peer = Peer(2, 15)
        self.assertEqual(await cli.directory_records(peer, 0x11), peer.rows)
        self.assertFalse(any(op == 0x18 for op, _ in peer.calls))
        empty = Peer(0); empty.generation = 0
        self.assertEqual(await cli.directory_records(empty, 0x11), [])

    async def test_malformed_page_never_retries(self):
        def replace(raw, at, value):
            return raw[:at] + bytes([value]) + raw[at + 1:]
        cases = {
            'truncated': lambda raw, off: raw[:-1],
            'too many apps': lambda raw, off: replace(raw, 8, 17),
            'wrong cursor': lambda raw, off: replace(raw, 9, 1),
            'empty nonterminal': lambda raw, off: raw[:10] + b'\0',
            'too many records': lambda raw, off: raw[:10] + b'\4' + b''.join(record(i) for i in range(4)),
            'generation changed': lambda raw, off: (bytes.fromhex('1132547698badcfe') + raw[8:]) if off else raw,
            'total changed': lambda raw, off: replace(raw, 8, 15) if off else raw,
            'duplicate across pages': lambda raw, off: raw[:11] + record(2) + raw[83:] if off else raw,
            'out of order': lambda raw, off: raw[:11] + record(1) + record(0) + record(2),
            'unterminated id': lambda raw, off: raw[:11] + b'x' * 32 + raw[43:],
            'noncanonical padding': lambda raw, off: replace(raw, 42, 120),
            'zero version': lambda raw, off: raw[:43] + bytes(4) + raw[47:],
            'oversized package': lambda raw, off: raw[:79] + struct.pack('<I', 2097153) + raw[83:],
        }
        for name, corrupt in cases.items():
            with self.subTest(name=name):
                peer = Peer(); peer.corrupt = corrupt
                with self.assertRaises(ValueError):
                    await cli.directory_records(peer, 0x11)
                self.assertLessEqual(len([op for op, _ in peer.calls if op == 0x18]), 2)


class PackageTests(unittest.IsolatedAsyncioTestCase):
    def data(self, size):
        raw = bytearray(size); raw[:8] = b'TRPKG001'; struct.pack_into('<HH5I',raw,8,1,256,len(raw),256,1,16,0)
        struct.pack_into('<I', raw, 12, size); struct.pack_into('<I', raw, 32, 1)
        raw[56:60] = b'demo'
        return bytes(raw)

    async def test_two_mib_identity_and_old_device_rejection_before_prepare(self):
        data = self.data(2097152)
        self.assertEqual(cli.protocol.package_info(data)[68:], bytes.fromhex('00002000'))
        with self.assertRaises(ValueError): cli.protocol.package_info(self.data(2097153))
        class OldPeer:
            calls = []
            async def management(self, opcode, payload=b''):
                self.calls.append(opcode)
                return struct.pack('<HIBB', 1, 303104, 2, 7)
        peer = OldPeer()
        @contextlib.asynccontextmanager
        async def connect(): yield peer
        with self.assertRaisesRegex(ValueError, 'support'):
            await cli.protocol.install(connect, data)
        self.assertEqual(peer.calls, [0x10])


if __name__ == '__main__': unittest.main()
