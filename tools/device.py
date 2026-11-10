"""Install and launch S31 packages through the shared USB/BLE client."""
import contextlib
import time
import threading
import json
import struct
import codecs
import re
import os
import argparse
import asyncio
from pathlib import Path

import ble_install
from eeot import USBApplicationLink, resolve_transport, usb_ports
import debug_bridge as debug
import package
import tinyrt


@contextlib.contextmanager
def open_debug(port):
    import serial
    if port is None:
        ports=usb_ports()
        if len(ports)!=1:
            raise ValueError('请用 --port 指定开发板')
        port=ports[0]
    stream=serial.Serial(port=None,baudrate=115200,timeout=.05,write_timeout=2)
    stream.dtr=False
    stream.rts=False
    stream.port=port
    stream.open()
    try:
        yield debug.Bridge(stream)
    finally:
        stream.close()


def is_debug_badge(port):
    try:
        with open_debug(port) as bridge:
            hello = json.loads(bridge.call(debug.HELLO, timeout=0.7))
        return (isinstance(hello, dict) and hello.get('proto') == 1
                and isinstance(hello.get('firmware'), str)
                and hello.get('data_max') == debug.DATA_MAX)
    except (OSError, ValueError, debug.EedbError):
        return False


def open_install_page(port):
    with open_debug(port) as bridge:
        hello=bridge.json(debug.HELLO)
        if hello.get('proto') != 1 or hello.get('data_max') != debug.DATA_MAX:
            raise ValueError('USB dev 需要 EEDB 开发固件')
        for _ in range(30):
            tree=bridge.json(debug.UI_TREE,bytes([64]))
            page=tree['page']
            nodes=tree['nodes']
            names={n['text'] for n in nodes}
            if page=='app_install':
                return
            if page=='app':
                if bridge.json(debug.STATUS)['app_state']==3:
                    bridge.call(debug.APP_STOP)
                else:
                    bridge.key('short')
            elif '' in names:
                bridge.key('long')
            elif any('添加' in n or 'Add' in n or '追加' in n or '추가' in n for n in names):
                bridge.tap(233,221)
            elif any(n['type']=='image' and n['w']==210 for n in nodes):
                bridge.swipe(330,233,136,233,300)
            elif any(n['text']=='' and n['w']==150 for n in nodes):
                card=next(n for n in nodes if n['text']=='' and n['w']==150)
                if card['x']==158: bridge.tap(233,233)
                elif card['x']<158: bridge.swipe(130,233,330,233,300)
                else: bridge.swipe(330,233,130,233,300)
            elif page=='home':
                bridge.swipe(233,385,233,70,300)
            else:
                raise ValueError('请先在设备上打开应用大厅的添加页')
            time.sleep(.55)
        raise TimeoutError('等待设备应用接收页超时')


class AppLogs:
    def __init__(self):
        self.decoder = codecs.getincrementaldecoder('utf-8')(errors='replace')
        self.pending = ''

    def feed(self, raw):
        self.pending += self.decoder.decode(raw)
        lines = self.pending.split('\n')
        self.pending = lines.pop()[-8192:]
        for line in lines:
            if re.search(r'\bapp: ', line):
                print(line.rstrip(), flush=True)


def launch_debug(port, app_id, installed):
    # The install page owns the display. Its ordinary short-key action closes
    # the transfer session, including an idle page after an exact QUERY hit.
    deadline = time.monotonic() + 18
    allow_back = time.monotonic() + (0 if installed == 'already-installed' else 3)
    sent_back = False
    logs = AppLogs()
    session = None
    previous = None
    uncertain = False
    last_error = None
    while time.monotonic() < deadline:
        try:
            with open_debug(port) as bridge:
                while time.monotonic() < deadline:
                    state = bridge.json(debug.STATUS)
                    logs.feed(bytes(bridge.reader.log))
                    bridge.reader.log.clear()
                    if session is not None:
                        if state['session'] == session and state['app_state'] == 5:
                            raise RuntimeError('设备启动应用失败')
                        if state['session'] == session and state['app_state'] == 3 and state['page'] == 'app':
                            return
                    elif uncertain and state['session'] != previous:
                        # A launch whose reply was lost may already be running.
                        # Do not submit a second launch against that generation.
                        session = state['session']
                    elif state['page'] == 'app_install':
                        if not sent_back and time.monotonic() >= allow_back:
                            bridge.key('short')
                            sent_back = True
                    else:
                        previous = state['session']
                        uncertain = True
                        reply = bridge.call(debug.APP_LAUNCH, debug.app_id_field(app_id))
                        if len(reply) != 4 or not (session := struct.unpack('<I', reply)[0]):
                            raise ValueError('设备返回无效的启动会话')
                    time.sleep(0.15)
        except debug.EedbError as error:
            if error.status not in (3, 6, 8): raise RuntimeError(str(error)) from error
            last_error = error
        except OSError as error:
            last_error = error
        time.sleep(0.3)
    raise TimeoutError('未在时限内看到设备进入目标应用页面') from last_error


async def ble_address(address):
    if not address:
        from bleak import BleakScanner
        found = await BleakScanner.discover(timeout=5, return_adv=True)
        matches = [device.address for device, advert in found.values()
                   if ble_install.protocol.SVC in [u.lower() for u in advert.service_uuids or []]]
        if len(matches) != 1:
            raise ValueError('请用 --address 指定目标设备')
        address = matches[0]
    return address


def describe_crash(report):
    if report is None:
        return '设备尚无应用崩溃记录'
    function = report['function_index']
    pc = report['pc']
    return (f"{report['app_id']} v{report['version']} ({report['sha256']})\n"
            f"回调阶段 {report['phase']}，原因 {report['reason']}\n"
            f"函数索引 {function if function is not None else '未知'}，PC {hex(pc) if pc is not None else '未知'}")


async def crash_info(args):
    mode, port = await asyncio.to_thread(resolve_transport, args.transport, args.port, args.address)
    if mode == 'usb':
        async with USBApplicationLink(port) as link:
            return describe_crash(await ble_install.protocol.last_crash(link))
    command = argparse.Namespace(command='crash', package=None, scan=False, pair=False,
                                 address=await ble_address(args.address), timeout=30, fragment=20)
    return describe_crash(await ble_install.run(command))


async def deploy(args, path, launch):
    raw = package.read_bounded(path)
    metadata = package.validate_envelope(raw)
    mode, port = await asyncio.to_thread(resolve_transport, args.transport, args.port, args.address,
                                         debug_probe=is_debug_badge if launch else None)
    if mode == 'usb':
        if not port:
            ports = usb_ports()
            if len(ports) != 1: raise ValueError('请用 --port 指定目标开发板')
            port = ports[0]
        if launch:
            await asyncio.to_thread(open_install_page,port)
        def connect():
            return USBApplicationLink(port)
        state = await ble_install.protocol.install(connect, raw)
        if launch:
            await asyncio.to_thread(launch_debug,port,metadata['app_id'],state)
        return dict(result=state, transport=mode, port=port, launched=launch)
    address = await ble_address(args.address)
    command = argparse.Namespace(command='install', package=path, scan=False, pair=True,
                                 address=address, timeout=30, fragment=20, pin=None, launch_after_install=launch)
    await ble_install.run(command)
    return dict(transport=mode, address=address, launched=launch)


def print_logs(port, done=None, seconds=None):
    logs = AppLogs()
    end = None if seconds is None else time.monotonic() + seconds
    next_check = 0
    with open_debug(port) as bridge:
        while end is None or time.monotonic() < end:
            now = time.monotonic()
            if done and now >= next_check:
                if done(): break
                next_check = now + 0.4
            bridge.reader.feed(bridge.port.read(bridge.port.in_waiting or 1))
            logs.feed(bytes(bridge.reader.log))
            bridge.reader.log.clear()


def watched_files(directory):
    # Walk only source directories. A build may contain thousands of Zig
    # cache files; comparing timestamps must also work during manifest edits.
    root = tinyrt.manifest_path(directory).parent
    files = {}
    for folder, dirs, names in os.walk(root):
        dirs[:] = [name for name in dirs if name not in ('build','release','.git')]
        for name in names:
            path = Path(folder) / name
            try:
                stat = path.stat()
            except FileNotFoundError:
                continue
            files[path] = stat.st_mtime_ns, stat.st_size
    return files


async def follow_logs(port, changed):
    stop = threading.Event()
    deadline = None

    def done():
        return stop.is_set() or (changed is not None and changed())

    try:
        while not done():
            try:
                await asyncio.to_thread(print_logs, port, done)
                return
            except OSError as error:
                if deadline is None: deadline = time.monotonic() + 12
                if time.monotonic() >= deadline:
                    raise ConnectionError(f'无法重新连接日志串口 {port}') from error
                await asyncio.sleep(0.4)
    finally:
        stop.set()


async def develop(args):
    import aot
    import subprocess
    while True:
        before = watched_files(args.app)
        try:
            built = tinyrt.build(args.app, args.cc)
            wasm = Path(built['wasm'])
            output = wasm.with_suffix('.trpkg')
            aot.pack_aot(args.app, wasm, output)
            result = await deploy(args, output, True)
            # Subsequent watch cycles reconnect to this same board.
            args.transport = result['transport']
            if result['transport'] == 'usb':
                args.port = result['port']
                print(f"已启动 {built['app_id']}，读取 {args.port} 日志", flush=True)
                changed = (lambda: watched_files(args.app) != before) if args.watch else None
                await follow_logs(args.port, changed)
            else:
                args.address = result['address']
                print(f"已启动 {built['app_id']}", flush=True)
            if not args.watch: return result
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            if not args.watch: raise
            print(f"开发循环失败：{error}。修改文件后重试。", flush=True)
        while watched_files(args.app) == before:
            await asyncio.sleep(0.5)
