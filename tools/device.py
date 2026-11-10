"""Install and launch S31 packages through the shared USB/BLE client."""
import argparse
import asyncio
from pathlib import Path

import ble_install
from eeot import USBApplicationLink, resolve_transport
import package
import tinyrt


async def deploy(args, path, launch):
    raw = package.read_bounded(path)
    package.validate_envelope(raw)
    mode, port = await asyncio.to_thread(resolve_transport, args.transport, args.port, args.address)
    if mode == 'usb':
        def connect():
            return USBApplicationLink(port)
        state = await ble_install.protocol.install(connect, raw)
        if launch:
            async with connect() as link:
                identity = ble_install.protocol.package_info(raw)
                await link.management(ble_install.protocol.LAUNCH, identity[:68])
        return dict(result=state, transport=mode, launched=launch)
    address = args.address
    if not address:
        from bleak import BleakScanner
        found = await BleakScanner.discover(timeout=5, return_adv=True)
        matches = [device.address for device, advert in found.values()
                   if ble_install.protocol.SVC in [u.lower() for u in advert.service_uuids or []]]
        if len(matches) != 1:
            raise ValueError('请用 --address 指定目标设备')
        address = matches[0]
    command = argparse.Namespace(command='install', package=path, scan=False, pair=True,
                                 address=address, timeout=30, fragment=20, pin=None)
    await ble_install.run(command)
    if launch:
        command.command = 'launch'
        command.pair = False
        await ble_install.run(command)
    return dict(transport=mode, launched=launch)


def source_state(directory):
    manifest = tinyrt.manifest_path(directory)
    root = manifest.parent
    app = tinyrt.load_manifest(manifest)
    files = {manifest, *(root / name for name in app['sources'])}
    files.update(root / app[key] for key in ('assets', 'cover') if app.get(key))
    files.update(p for p in root.rglob('*.h') if p.relative_to(root).parts[0] not in ('build','release'))
    return {p: (p.stat().st_mtime_ns, p.stat().st_size) for p in files}


async def develop(args):
    import aot
    while True:
        before = source_state(args.app)
        built = tinyrt.build(args.app, args.cc)
        wasm = Path(built['wasm'])
        output = wasm.with_suffix('.trpkg')
        aot.pack_aot(args.app, wasm, output)
        result = await deploy(args, output, True)
        if not args.watch:
            return result
        print(f"已启动 {built['app_id']}，等待源文件变化", flush=True)
        while source_state(args.app) == before:
            await asyncio.sleep(0.5)
