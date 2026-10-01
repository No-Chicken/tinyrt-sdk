"""Offline custom WinRT pairing regression. Every device API is a fake."""
import asyncio
from enum import IntEnum
import gc
import importlib.util
from pathlib import Path
import sys
from types import ModuleType, SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("tinyrt_push_pairing_test", ROOT / "tools/ble_install.py")
push = importlib.util.module_from_spec(spec)
spec.loader.exec_module(push)

class Kinds(IntEnum):
    PROVIDE_PIN = 4
class Protection(IntEnum):
    ENCRYPTION_AND_AUTHENTICATION = 3
class Status(IntEnum):
    PAIRED = 1
    ALREADY_PAIRED = 2
    REJECTED_BY_HANDLER = 3

class FakeArgs:
    pairing_kind = Kinds.PROVIDE_PIN
    def __init__(self, accept_error=None, complete_error=None):
        self.accept_error, self.complete_error = accept_error, complete_error
        self.done = asyncio.Event()
        self.completed = 0
        self.accepted = None
    def get_deferral(self):
        return self
    def accept_with_pin(self, value):
        if self.accept_error:
            raise self.accept_error
        self.accepted = value
    def complete(self):
        self.completed += 1
        self.done.set()
        if self.complete_error:
            raise self.complete_error

class FakeCustom:
    def __init__(self, args, hang=False):
        self.args, self.hang = args, hang
        self.handler = None
        self.removed = False
        self.cancelled = False
        self.request = None
    def add_pairing_requested(self, handler):
        self.handler = handler
        return 42
    def remove_pairing_requested(self, token):
        assert token == 42
        self.removed = True
    async def pair_with_protection_level_async(self, kind, protection):
        self.request = (kind, protection)
        try:
            self.handler(None, self.args)
            await self.args.done.wait()
            if self.hang:
                await asyncio.Future()
            return SimpleNamespace(status=Status.PAIRED if self.args.accepted else Status.REJECTED_BY_HANDLER)
        except asyncio.CancelledError:
            self.cancelled = True
            raise

class FakeDevice:
    def __init__(self, custom):
        self.closed = False
        self.device_information = SimpleNamespace(pairing=SimpleNamespace(is_paired=False, custom=custom))
    def close(self):
        self.closed = True

def fake_modules(device):
    bluetooth = ModuleType("winrt.windows.devices.bluetooth")
    async def locate(address):
        assert address == int("7c4fadbc257a", 16)
        return device
    bluetooth.BluetoothLEDevice = SimpleNamespace(from_bluetooth_address_async=locate)
    enumeration = ModuleType("winrt.windows.devices.enumeration")
    enumeration.DevicePairingKinds = Kinds
    enumeration.DevicePairingProtectionLevel = Protection
    enumeration.DevicePairingResultStatus = Status
    util = ModuleType("bleak.backends.winrt.util")
    async def assert_mta():
        pass
    util.assert_mta = assert_mta
    return {module.__name__: module for module in (bluetooth, enumeration, util)}

class PairingTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.warnings = []
        self.loop = asyncio.get_running_loop()
        self.previous_handler = self.loop.get_exception_handler()
        self.loop.set_exception_handler(lambda _loop, context: self.warnings.append(context))
    async def asyncTearDown(self):
        gc.collect()
        await asyncio.sleep(0)
        self.loop.set_exception_handler(self.previous_handler)
        self.assertEqual(self.warnings, [], "handler exceptions must be retrieved")
    async def invoke(self, args, pin=None, hang=False):
        custom = FakeCustom(args, hang)
        device = FakeDevice(custom)
        self.device, self.custom = device, custom
        with patch.dict(sys.modules, fake_modules(device)):
            await asyncio.wait_for(push.windows_pair("7c:4f:ad:bc:25:7a", pin), 0.3)
    async def test_success_keeps_authenticated_pin_policy(self):
        args = FakeArgs()
        await self.invoke(args, "123456")
        self.assertEqual(args.accepted, "123456")
        self.assertEqual(args.completed, 1)
        self.assertEqual(self.custom.request, (Kinds.PROVIDE_PIN, Protection.ENCRYPTION_AND_AUTHENTICATION))
        self.assertTrue(self.custom.removed and self.device.closed)
    async def test_eof_input_error_reaches_caller(self):
        args = FakeArgs()
        with patch.object(push, "read_windows_pin", side_effect=EOFError("PIN input stream closed")):
            with self.assertRaisesRegex(RuntimeError, "PIN input stream closed") as caught:
                await self.invoke(args)
        self.assertIsInstance(caught.exception.__cause__, EOFError)
        self.assertEqual(args.completed, 1)
        self.assertTrue(self.custom.removed and self.device.closed)
    async def test_accept_failure_cancels_pending_pair_and_preserves_error(self):
        args = FakeArgs(accept_error=OSError("accept_with_pin failed"))
        with self.assertRaisesRegex(RuntimeError, "accept_with_pin failed") as caught:
            await self.invoke(args, "123456", hang=True)
        self.assertIsInstance(caught.exception.__cause__, OSError)
        self.assertEqual(args.completed, 1)
        self.assertTrue(self.custom.cancelled and self.custom.removed and self.device.closed)
    async def test_deferral_failure_reaches_caller(self):
        args = FakeArgs(complete_error=RuntimeError("deferral completion failed"))
        with self.assertRaisesRegex(RuntimeError, "deferral completion failed"):
            await self.invoke(args, "123456")
        self.assertTrue(self.custom.removed and self.device.closed)
    async def test_unknown_windows_device_has_discovery_guidance(self):
        with patch.dict(sys.modules, fake_modules(None)):
            with self.assertRaisesRegex(RuntimeError, "--scan"):
                await push.windows_pair("7c:4f:ad:bc:25:7a", "123456")

if __name__ == "__main__":
    unittest.main()
