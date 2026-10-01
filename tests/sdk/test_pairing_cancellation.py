"""No hardware: emulate WinRT lifecycle and a cancellable Windows console."""
import asyncio
from enum import IntEnum
import importlib.util
import inspect
import io
from pathlib import Path
import subprocess
import sys
import threading
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('pairing_cancel_cli',ROOT/'tools/ble_install.py')
push=importlib.util.module_from_spec(spec);spec.loader.exec_module(push)

class Kinds(IntEnum):PROVIDE_PIN=4
class Protection(IntEnum):ENCRYPTION_AND_AUTHENTICATION=3
class Status(IntEnum):PAIRED=1;ALREADY_PAIRED=2;REJECTED_BY_HANDLER=3

class Request:
    pairing_kind=Kinds.PROVIDE_PIN
    def __init__(self):self.accepted=None;self.completed=0;self.done=asyncio.Event()
    def get_deferral(self):return self
    def accept_with_pin(self,value):self.accepted=value
    def complete(self):self.completed+=1;self.done.set()

class Custom:
    def __init__(self,started=None):self.request=Request();self.started=started;self.removed=False;self.cancelled=False
    def add_pairing_requested(self,handler):self.handler=handler;return 1
    def remove_pairing_requested(self,token):assert token==1;self.removed=True
    async def pair_with_protection_level_async(self,kind,protection):
        assert (kind,protection)==(Kinds.PROVIDE_PIN,Protection.ENCRYPTION_AND_AUTHENTICATION)
        try:
            self.handler(None,self.request)
            if self.started is not None:
                while not self.started.is_set():await asyncio.sleep(0.001)
                return SimpleNamespace(status=Status.REJECTED_BY_HANDLER)
            await self.request.done.wait()
            return SimpleNamespace(status=Status.PAIRED if self.request.accepted else Status.REJECTED_BY_HANDLER)
        except asyncio.CancelledError:self.cancelled=True;raise

class Device:
    def __init__(self,custom):self.closed=False;self.device_information=SimpleNamespace(pairing=SimpleNamespace(is_paired=False,custom=custom))
    def close(self):self.closed=True

def modules(device):
    bluetooth=ModuleType('winrt.windows.devices.bluetooth')
    async def locate(address):return device
    bluetooth.BluetoothLEDevice=SimpleNamespace(from_bluetooth_address_async=locate)
    enumeration=ModuleType('winrt.windows.devices.enumeration')
    enumeration.DevicePairingKinds=Kinds;enumeration.DevicePairingProtectionLevel=Protection;enumeration.DevicePairingResultStatus=Status
    util=ModuleType('bleak.backends.winrt.util')
    async def assert_mta():pass
    util.assert_mta=assert_mta
    return {m.__name__:m for m in (bluetooth,enumeration,util)}

def rejection_probe():
    started=threading.Event();custom=Custom(started);device=Device(custom)
    console=ModuleType('msvcrt')
    def pending_key():started.set();return False
    console.kbhit=pending_key
    console.getwch=lambda: (_ for _ in ()).throw(AssertionError('no console key available'))
    def blocking_input(*args):started.set();threading.Event().wait()
    async def scenario():
        try:await push.windows_pair('00:11:22:33:44:55')
        except RuntimeError as error:assert 'authenticated pairing failed' in str(error)
        else:raise AssertionError('OS rejection should fail')
        assert custom.removed and device.closed and custom.request.completed==1
    with patch.dict(sys.modules,{**modules(device),'msvcrt':console}),patch.object(sys,'stdin',SimpleNamespace(isatty=lambda:True)),patch('builtins.input',blocking_input):
        asyncio.run(scenario())
    print('asyncio.run returned after OS rejection')

class PairingCancellationTests(unittest.TestCase):
    def test_os_rejection_during_default_pin_wait_does_not_hang_asyncio_run(self):
        try:
            p=subprocess.run([sys.executable,__file__,'--rejection-probe'],capture_output=True,text=True,timeout=4)
        except subprocess.TimeoutExpired:self.fail('asyncio.run hung after OS rejection while waiting for PIN')
        self.assertEqual(p.returncode,0,p.stdout+p.stderr)
        self.assertIn('asyncio.run returned after OS rejection',p.stdout)

    def test_rejection_cancels_injected_reader_and_completes_deferral(self):
        self.assertIn('pin_reader',inspect.signature(push.windows_pair).parameters,'awaitable PIN reader API is missing')
        async def scenario():
            started=asyncio.Event();cleaned=asyncio.Event();custom=Custom(started);device=Device(custom)
            async def reader():
                try:started.set();await asyncio.Future()
                finally:cleaned.set()
            with patch.dict(sys.modules,modules(device)):
                with self.assertRaisesRegex(RuntimeError,'authenticated pairing failed'):
                    await asyncio.wait_for(push.windows_pair('00:11:22:33:44:55',pin_reader=reader),0.5)
            self.assertTrue(cleaned.is_set());self.assertEqual(custom.request.completed,1)
            self.assertTrue(custom.removed and device.closed)
        asyncio.run(scenario())

    def test_explicit_pin_keeps_existing_api_and_does_not_read_console(self):
        self.assertIn('pin_reader',inspect.signature(push.windows_pair).parameters,'awaitable PIN reader API is missing')
        async def scenario():
            custom=Custom();device=Device(custom)
            async def reader():self.fail('explicit --pin must not read console')
            with patch.dict(sys.modules,modules(device)):
                await push.windows_pair('00:11:22:33:44:55','123456',pin_reader=reader)
            self.assertEqual(custom.request.accepted,'123456');self.assertEqual(custom.request.completed,1)
            self.assertTrue(custom.removed and device.closed)
        asyncio.run(scenario())

    def test_reader_eof_reaches_caller_with_cleanup(self):
        self.assertIn('pin_reader',inspect.signature(push.windows_pair).parameters,'awaitable PIN reader API is missing')
        async def scenario():
            custom=Custom();device=Device(custom)
            async def reader():raise EOFError('PIN input stream closed')
            with patch.dict(sys.modules,modules(device)):
                with self.assertRaisesRegex(RuntimeError,'PIN input stream closed') as caught:
                    await push.windows_pair('00:11:22:33:44:55',pin_reader=reader)
            self.assertIsInstance(caught.exception.__cause__,EOFError)
            self.assertEqual(custom.request.completed,1);self.assertTrue(custom.removed and device.closed)
        asyncio.run(scenario())

    def console(self,keys,tty=True):
        self.assertTrue(hasattr(push,'read_windows_pin'),'cancellable console reader is missing')
        console=ModuleType('msvcrt');pending=list(keys)
        console.kbhit=lambda:bool(pending);console.getwch=lambda:pending.pop(0)
        with patch.dict(sys.modules,{'msvcrt':console}),patch.object(sys,'stdin',SimpleNamespace(isatty=lambda:tty)),patch.object(sys,'stdout',io.StringIO()):
            return asyncio.run(push.read_windows_pin())

    def test_console_supports_backspace_enter_and_ignores_extended_keys(self):
        self.assertEqual(self.console(['1','2','\b','2','3','\x00','H','4','5','6','\r']),'123456')

    def test_non_tty_requires_explicit_pin(self):
        with self.assertRaisesRegex(ValueError,'--pin'):self.console([],tty=False)

    def test_console_ctrl_c_can_interrupt(self):
        with self.assertRaises(KeyboardInterrupt):self.console(['\x03'])

    def test_console_ctrl_z_reports_closed_input(self):
        with self.assertRaises(EOFError):self.console(['\x1a'])

if __name__=='__main__':
    if sys.argv[-1]=='--rejection-probe':rejection_probe()
    else:unittest.main()
