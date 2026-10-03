#!/usr/bin/env python3
"""TinyRT PC client: real BLE, exact persistent result, authenticated pairing.

Examples:
 python tinyrt_push.py --address AA:BB:CC:DD:EE:FF --pair install counter.trpkg
 python tinyrt_push.py --address AA:BB:CC:DD:EE:FF list
 python tinyrt_push.py --address AA:BB:CC:DD:EE:FF launch counter.trpkg
No serial access, firmware flashing, storage formatting, or automatic unpairing.
"""
import argparse
import asyncio
import contextlib
import json
import re
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'protocol'))
import client as protocol

async def read_windows_pin():
    """Read only after WinRT requests a PIN; polling remains task-cancellable.

    msvcrt reads the Windows console without a blocking input worker. Redirected
    stdin cannot use this path: automation must supply --pin or an async reader.
    """
    if not sys.stdin.isatty():
        raise ValueError('interactive PIN entry requires a Windows console; use --pin or an async pin_reader')
    import msvcrt
    digits=[];extended=False
    sys.stdout.write('Enter the six digits displayed on the badge: ');sys.stdout.flush()
    try:
        while True:
            if not msvcrt.kbhit():
                await asyncio.sleep(0.05)
                continue
            char=msvcrt.getwch()
            if extended:
                extended=False
            elif char in ('\x00','\xe0'):
                extended=True
            elif char in ('\r','\n'):
                return ''.join(digits)
            elif char=='\x03':
                raise KeyboardInterrupt('PIN input cancelled')
            elif char=='\x1a':
                raise EOFError('PIN input stream closed')
            elif char=='\b':
                if digits:
                    digits.pop();sys.stdout.write('\b \b');sys.stdout.flush()
            elif '0'<=char<='9' and len(digits)<6:
                digits.append(char);sys.stdout.write('*');sys.stdout.flush()
            # Remain cancellable even while console keys arrive continuously.
            await asyncio.sleep(0)
    finally:
        sys.stdout.write('\n');sys.stdout.flush()

async def windows_pair(address, pin=None, *, pin_reader=None):
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.enumeration import (DevicePairingKinds,
        DevicePairingProtectionLevel, DevicePairingResultStatus)
    from bleak.backends.winrt.util import assert_mta
    await assert_mta()
    device=await BluetoothLEDevice.from_bluetooth_address_async(int(address.replace(':',''),16))
    if device is None:
        raise RuntimeError('Windows could not locate the badge; keep it advertising, '
                           'run tinyrt_push.py --scan once, then retry this address. '
                           'No pairing or reset was attempted.')
    try:
        pairing=device.device_information.pairing
        if pairing.is_paired:return
        custom=pairing.custom;loop=asyncio.get_running_loop();tasks=set()
        # Store the exception as a result: a cancelled pairing cannot leave an
        # unobserved exception on this reporting future itself.
        handler_failure=loop.create_future();stopping=False;pair_task=None
        def report_failure(error):
            if not handler_failure.done():handler_failure.set_result(error)
        def complete_deferral(deferral):
            try:deferral.complete()
            except Exception as exc:report_failure(exc)
        async def enter_pin(args, deferral):
            try:
                value=pin if pin is not None else await (pin_reader or read_windows_pin)()
                if not re.fullmatch(r'[0-9]{6}',value):
                    raise ValueError('PIN input must contain exactly six digits')
                args.accept_with_pin(value)
            except (Exception, KeyboardInterrupt) as exc:
                report_failure(exc)
            finally:
                complete_deferral(deferral)
        def task_done(task):
            tasks.discard(task)
            if not task.cancelled():
                error=task.exception()  # Retrieve even unexpected task failures.
                if error is not None:report_failure(error)
        def requested(_sender,args):
            try:
                if args.pairing_kind!=DevicePairingKinds.PROVIDE_PIN:return
                deferral=args.get_deferral()
            except Exception as exc:
                loop.call_soon_threadsafe(report_failure,exc)
                return
            def schedule():
                if stopping:
                    complete_deferral(deferral)
                    return
                task=asyncio.create_task(enter_pin(args,deferral))
                tasks.add(task);task.add_done_callback(task_done)
            loop.call_soon_threadsafe(schedule)
        token=custom.add_pairing_requested(requested)
        try:
            pair_task=asyncio.ensure_future(custom.pair_with_protection_level_async(
                DevicePairingKinds.PROVIDE_PIN,DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION))
            await asyncio.wait((pair_task,handler_failure),return_when=asyncio.FIRST_COMPLETED)
            if handler_failure.done():
                error=handler_failure.result()
                if isinstance(error,KeyboardInterrupt): raise error
                raise RuntimeError(f'Windows PIN handler failed ({type(error).__name__}): {error}') from error
            result=await pair_task
            if result.status not in (DevicePairingResultStatus.PAIRED,DevicePairingResultStatus.ALREADY_PAIRED):
                raise RuntimeError(f'authenticated pairing failed: {result.status.name}')
        finally:
            stopping=True
            try:custom.remove_pairing_requested(token)
            finally:
                pending=list(tasks)
                if pair_task is not None:pending.append(pair_task)
                for task in pending:
                    if not task.done():task.cancel()
                if pending:await asyncio.gather(*pending,return_exceptions=True)
                handler_failure.cancel()
    finally:device.close()

def info_json(raw):
    if len(raw)!=72:raise ValueError('invalid directory record')
    import struct
    return {'app_id':raw[:32].split(b'\0',1)[0].decode('ascii'),
        'version':struct.unpack_from('<I',raw,32)[0],'sha256':raw[36:68].hex(),
        'size':struct.unpack_from('<I',raw,68)[0]}

async def directory_records(link,opcode,*,hello=None):
    return await protocol.directory_records(link,opcode,hello=hello)

async def removal_identity(link,app_id):
    if not re.fullmatch(r'[a-z0-9._-]{1,31}',app_id):
        raise ValueError('invalid app ID')
    hello=await link.management(protocol.HELLO)
    if len(hello)!=8:raise ValueError('unsupported HELLO')
    records=await directory_records(link,protocol.LIST,hello=hello)
    if hello[7]&protocol.CAP_QUARANTINE:
        records+=await directory_records(link,protocol.LIST_QUARANTINED,hello=hello)
    requested=app_id.encode('ascii').ljust(32,b'\0')
    matches=[value for value in records if value[:32]==requested]
    if not matches:raise ValueError('app ID not found in device directory')
    if len(matches)!=1:raise ValueError('ambiguous app ID in device directory')
    return matches[0]

async def details_identity(link,app_id):
    if not re.fullmatch(r'[a-z0-9._-]{1,31}',app_id):
        raise ValueError('invalid app ID')
    requested=app_id.encode('ascii').ljust(32,b'\0')
    matches=[row for row in await directory_records(link,protocol.LIST) if row[:32]==requested]
    if len(matches)!=1:raise ValueError('healthy app ID not found or ambiguous')
    return matches[0]

async def run(args):
    from bleak import BleakClient, BleakScanner, BleakError
    if args.scan:
        found=await BleakScanner.discover(timeout=5,return_adv=True)
        print(json.dumps([{'address':d.address,'name':d.name} for d,a in found.values()
            if protocol.SVC in [u.lower() for u in (a.service_uuids or [])]],ensure_ascii=False))
        return
    if not args.address:raise ValueError('--address is required; use --scan first')
    if args.pair or args.command=='pair':
        if sys.platform=='win32':await windows_pair(args.address,args.pin)
        else:
            async with BleakClient(args.address,pair=True,timeout=60):pass
        if args.command=='pair':print('paired');return
    @contextlib.asynccontextmanager
    async def connect():
        try:
            async with BleakClient(args.address,timeout=30) as device:
                link=protocol.Link(device,timeout=args.timeout,fragment=args.fragment)
                await link.start();yield link
        except BleakError as exc:raise ConnectionError(str(exc)) from exc
    data=None
    if args.package:
        data=Path(args.package).read_bytes();info=protocol.package_info(data)
    app_id=getattr(args,'app_id',None)
    if app_id and (args.command not in ('uninstall','info') or data is not None):
        raise ValueError('--app-id is only for uninstall/info without a package file')
    if args.command in ('install','query','launch','uninstall','info') and data is None and not app_id:
        raise ValueError('this command requires a TinyRT package file')
    if args.command=='install':
        def progress(done,total):print(f'PROGRESS {done}/{total}',flush=True)
        result=await protocol.install(connect,data,progress)
        print(json.dumps({'result':result,**info_json(info)}));return
    async with connect() as link:
        if args.command=='device':
            if data is not None:raise ValueError('device does not take a package file')
            print(json.dumps(await protocol.device_info(link)));return
        if args.command=='runtime':
            if data is not None:raise ValueError('runtime does not take a package file')
            print(json.dumps(await protocol.runtime_info(link)));return
        if args.command=='storage':
            if data is not None:raise ValueError('storage does not take a package file')
            print(json.dumps(await protocol.storage_info(link)));return
        if args.command=='info':
            if app_id:info=await details_identity(link,app_id)
            print(json.dumps(await protocol.app_info(link,info),ensure_ascii=False));return
        if args.command in ('list','quarantine'):
            opcode=protocol.LIST
            if args.command=='quarantine':
                hello=await link.management(protocol.HELLO)
                if len(hello)!=8 or not hello[7]&protocol.CAP_QUARANTINE:
                    raise ValueError('host does not support quarantine diagnostics')
                opcode=protocol.LIST_QUARANTINED
            records=await directory_records(link,opcode)
            print(json.dumps([info_json(raw) for raw in records]));return
        if args.command=='hello':
            print((await link.management(protocol.HELLO)).hex());return
        if args.command=='stop':
            await link.management(protocol.STOP);print('stopped');return
        if app_id:info=await removal_identity(link,app_id)
        opcode={'query':protocol.QUERY,'launch':protocol.LAUNCH,'uninstall':protocol.UNINSTALL}[args.command]
        raw=await protocol.confirmed_operation(link,opcode,info[:68]) if opcode==protocol.UNINSTALL else await link.management(opcode,info[:68])
        if args.command=='query':
            if raw!=info:raise ValueError('query identity mismatch')
            print(json.dumps(info_json(raw)))
        elif args.command=='uninstall':
            if await protocol.installed(link,info):raise RuntimeError('uninstall not durable')
            print('uninstalled')
        else:print('launched')

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',nargs='?',default='list',choices=['pair','hello','list','quarantine','install','query','launch','uninstall','stop','storage','info','runtime','device'])
    parser.add_argument('package',nargs='?')
    parser.add_argument('--address');parser.add_argument('--scan',action='store_true')
    parser.add_argument('--app-id',help='uninstall or inspect the exact current directory identity without the original package')
    parser.add_argument('--pair',action='store_true',help='physically open pairing on the badge first')
    parser.add_argument('--pin',help='six-digit display PIN; omitted requires an interactive Windows console')
    parser.add_argument('--timeout',type=float,default=30)
    parser.add_argument('--fragment',type=int,default=20,help='20 is the MTU23-compatible default')
    args=parser.parse_args(argv)
    if args.pin is not None and not re.fullmatch(r'[0-9]{6}',args.pin):parser.error('--pin must contain six digits')
    try:asyncio.run(run(args));return 0
    except (Exception,KeyboardInterrupt) as exc:
        print(f'FAILED: {exc}',file=sys.stderr);return 130 if isinstance(exc,KeyboardInterrupt) else 1
if __name__=='__main__':raise SystemExit(main())
