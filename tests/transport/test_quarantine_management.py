import importlib.util
from pathlib import Path
import struct
import unittest

source=Path(__file__).resolve().parents[2]/'tools/ble_install.py'
spec=importlib.util.spec_from_file_location('quarantine_cli',source)
cli=importlib.util.module_from_spec(spec);spec.loader.exec_module(cli)

def record(name):
    return name.encode().ljust(32,b'\0')+struct.pack('<I',1)+b'x'*32+struct.pack('<I',300)

class Peer:
    def __init__(self):self.flags=15;self.good=[record('demo.good')];self.bad=[record('demo.bad')]
    async def management(self,op,payload=b''):
        if op==cli.protocol.HELLO:return struct.pack('<HIBB',1,303104,2,self.flags)
        rows=self.good if op==cli.protocol.LIST else self.bad
        return bytes([len(rows)])+b''.join(rows)

class QuarantineTests(unittest.IsolatedAsyncioTestCase):
    async def test_resolve_exact_identity_without_original_package(self):
        p=Peer()
        self.assertEqual(await cli.removal_identity(p,'demo.good'),record('demo.good'))
        self.assertEqual(await cli.removal_identity(p,'demo.bad'),record('demo.bad'))
        with self.assertRaisesRegex(ValueError,'not found'):await cli.removal_identity(p,'demo')

    async def test_old_host_lists_only_healthy_and_bad_response_is_rejected(self):
        p=Peer();p.flags=7
        self.assertEqual(await cli.removal_identity(p,'demo.good'),record('demo.good'))
        with self.assertRaisesRegex(ValueError,'not found'):await cli.removal_identity(p,'demo.bad')
        p.flags=15;p.bad=[record('demo.good')]
        with self.assertRaisesRegex(ValueError,'ambiguous'):await cli.removal_identity(p,'demo.good')
        p.good=[b'broken']
        with self.assertRaisesRegex(ValueError,'directory'):await cli.directory_records(p,cli.protocol.LIST)

if __name__=='__main__':unittest.main()
