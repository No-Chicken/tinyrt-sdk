"""Compile development AOT with the SDK's pinned, self-built toolchain."""
import hashlib
import json
from pathlib import Path
import platform
import struct
import subprocess
import tempfile
import urllib.request

import package
import tinyrt

PROFILE=Path(__file__).resolve().parent/'toolchains/esp32s3.json'
OPTIONS=['--target=xtensa','--cpu=esp32s3','--bounds-checks=1','--stack-bounds-checks=1',
         '--opt-level=3','--size-level=0','--disable-simd','--disable-ref-types','--enable-loop-poll']


def sha(raw):return hashlib.sha256(raw).hexdigest()


def compiler_path(pin, override=None):
    if override:
        compiler=Path(override).resolve()
    else:
        if platform.system()!='Windows' or platform.machine().lower() not in ('amd64','x86_64'):
            raise ValueError('automatic toolchain download currently supports Windows x64; pass --wamrc with the pinned compiler')
        compiler=tinyrt.ROOT/'bin/wamrc.exe'
        if not compiler.exists():
            compiler=Path.home()/'.cache/tinyrt'/pin['compiler']['sha256']/'wamrc.exe'
            if not compiler.exists():
                compiler.parent.mkdir(parents=True,exist_ok=True)
                with urllib.request.urlopen(pin['compiler']['url'],timeout=60) as response:
                    data=response.read(64*1024*1024+1)
                if sha(data)!=pin['compiler']['sha256']:
                    raise ValueError('downloaded compiler SHA-256 does not match SDK pin')
                tinyrt.write_atomic(compiler,data)
    if not compiler.is_file() or sha(compiler.read_bytes())!=pin['compiler']['sha256']:
        raise ValueError('compiler SHA-256 does not match SDK pin; use the matching SDK toolchain')
    return compiler


def pack_development(manifest, wasm, output, wamrc=None):
    manifest=tinyrt.manifest_path(manifest);app=tinyrt.load_manifest(manifest)
    if not app['app_id'].startswith('demo.') or len(app['app_id'])<=5:
        raise ValueError('development AOT app_id must be a demo. descendant')
    pin_bytes=PROFILE.read_bytes();pin=json.loads(pin_bytes)
    if pin['schema']!=1 or pin['options']!=OPTIONS or pin['options_sha256']!=sha(json.dumps(OPTIONS,separators=(',',':')).encode()):
        raise ValueError('SDK toolchain profile must enable bounds, stack and loop-poll checks')
    compiler=compiler_path(pin,wamrc)
    paths=[manifest,Path(wasm).resolve(),PROFILE,compiler]
    paths += [tinyrt.local_file(manifest.parent,name) for name in app['sources']]
    asset=tinyrt.local_file(manifest.parent,app['assets']) if app.get('assets') else None
    if asset:paths.append(asset)
    snapshots={p.resolve():p.read_bytes() for p in paths}
    source=snapshots[Path(wasm).resolve()];assets=snapshots[asset] if asset else b''
    metadata={name:app[name] for name in tinyrt.REQUIRED-{'sources'}}
    metadata.update(key_id=1,private_key=package.development_key())
    package.metadata_header(source,assets,**metadata)
    if len(source)>package.MAX_PACKAGE_SIZE or len(assets)>package.MAX_PACKAGE_SIZE:
        raise ValueError('Wasm or resources exceed 2 MiB')
    output=tinyrt.protect_output(output,paths,'.trpkg',b'TRPKG001')
    with tempfile.TemporaryDirectory(prefix='tinyrt-dev-aot-') as directory:
        staged=Path(directory);input_wasm=staged/'input.wasm';native=staged/'output.aot'
        input_wasm.write_bytes(source)
        subprocess.run([str(compiler),*OPTIONS,'-o',str(native),str(input_wasm)],
                       check=True,capture_output=True,timeout=300)
        if native.is_symlink() or not native.is_file():raise ValueError('compiler did not produce a regular AOT artifact')
        code=package.read_bounded(native)
        if input_wasm.read_bytes()!=source:raise ValueError('compiler modified its Wasm input')
        for path,raw in snapshots.items():
            if path.read_bytes()!=raw:raise ValueError('input changed during compilation: '+path.name)
        header=bytearray(256);struct.pack_into('<HHII',header,0,1,256,pin['aot_format'],7)
        header[16:32]=b'xtensa'.ljust(16,b'\0');header[32:48]=b'esp32s3'.ljust(16,b'\0')
        for start,name in ((48,'wamr_commit'),(68,'llvm_commit'),(88,'patch_sha256'),
                           (120,'compat_id'),(152,'options_sha256')):
            header[start:start+(20 if start<88 else 32)]=bytes.fromhex(pin[name])
        header[184:216]=bytes.fromhex(pin['compiler']['sha256']);header[216:248]=hashlib.sha256(source).digest()
        data=package._assemble(source,assets,native=code,native_metadata=header,**metadata)
        package.validate_envelope(data,package.development_key().public_key(),1)
        output=tinyrt.protect_output(output,paths,'.trpkg',b'TRPKG001');tinyrt.write_atomic(output,data)
    return dict(package=str(output),package_size=len(data),sha256=sha(data),development_key=True,
                compat_id=pin['compat_id'],compiler_sha256=pin['compiler']['sha256'],backend='aot')
