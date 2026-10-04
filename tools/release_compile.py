#!/usr/bin/env python3
"""Compile with an operator-pinned toolchain, reverify inputs, then sign format 1.

The release profile, source lock, compiler and signing key belong to the trusted
release operator. Never accept those paths or their content from an app uploader.
This local command provides a pipeline building block; it deploys no service.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
import package
import tinyrt

SAFETY_OPTIONS = ['--bounds-checks=1','--stack-bounds-checks=1','--opt-level=3',
                  '--size-level=0','--disable-simd','--disable-ref-types','--enable-loop-poll']
PROFILE_FIELDS = {'schema','provenance_sha256','compiler_patch_sha256','runtime_patch_sha256',
                  'runtime_abi_revision','signing_key_id','signing_key_sha256','development'}

def digest(raw): return hashlib.sha256(raw).hexdigest()
def canonical(value): return json.dumps(value,sort_keys=True,separators=(',',':')).encode('utf-8')

def hex_value(value, size, name):
    if not isinstance(value,str) or re.fullmatch('[0-9a-f]{'+str(size*2)+'}',value) is None or not any(bytes.fromhex(value)):
        raise ValueError('invalid '+name)
    return value

def patch_set_sha256(hashes):
    if not hashes: raise ValueError('patch set must not be empty')
    return digest(b''.join(bytes.fromhex(hex_value(value,32,'patch SHA256')) for value in hashes))

def compatibility_identity(lock, provenance, profile):
    """No host executable hash: equal target ABI can come from different hosts."""
    options=lock['target_options']
    description=dict(schema=1,wamr_commit=lock['wamr']['commit'],llvm_commit=lock['llvm']['commit'],
        aot_format_version=lock['aot_format_version'],target_arch=options[0].split('=',1)[1],
        target_cpu=options[1].split('=',1)[1],compiler_patch_sha256=profile['compiler_patch_sha256'],
        runtime_patch_sha256=profile['runtime_patch_sha256'],runtime_abi_revision=profile['runtime_abi_revision'],
        target_options_sha256=provenance['target_options_sha256'])
    return digest(canonical(description))

def compile_release(manifest, wasm, source_lock, provenance, release_profile, runtime_patches,
                    signing_key, output, *, include_wasm=True):
    watched={}
    def snapshot(path):
        path=Path(path).resolve()
        raw=path.read_bytes();watched[path]=digest(raw)
        return raw
    def document(path):
        value=json.loads(snapshot(path).decode('utf-8'),object_pairs_hook=tinyrt.unique_object)
        if not isinstance(value,dict): raise ValueError('configuration must be a JSON object')
        return value
    profile=document(release_profile);lock=document(source_lock);prov=document(provenance)
    if set(profile)!=PROFILE_FIELDS or profile['schema']!=1 or type(profile['development']) is not bool:
        raise ValueError('invalid release profile')
    for name in ('provenance_sha256','compiler_patch_sha256','runtime_patch_sha256','signing_key_sha256'):
        hex_value(profile[name],32,name)
    tinyrt.integer(profile['runtime_abi_revision'],1,0xffffffff,'runtime ABI revision')
    tinyrt.integer(profile['signing_key_id'],0,0xffffffff,'signing key ID')
    if watched[Path(provenance).resolve()]!=profile['provenance_sha256'] or prov.get('schema')!=1:
        raise ValueError('provenance is not pinned by the release profile')
    if lock.get('schema')!=1 or watched[Path(source_lock).resolve()]!=prov.get('source_lock_sha256'):
        raise ValueError('source lock is not pinned by compiler provenance')
    if prov.get('sources')!={'wamr':lock.get('wamr'),'llvm':lock.get('llvm')}:
        raise ValueError('compiler source identities differ from source lock')
    for name in ('wamr','llvm'):hex_value(lock[name]['commit'],20,name+' commit')
    tinyrt.integer(lock['aot_format_version'],1,0xffffffff,'AOT format')
    options=lock.get('target_options')
    if (not isinstance(options,list) or len(options)!=9 or not all(isinstance(item,str) for item in options) or
            re.fullmatch(r'--target=[a-z0-9_-]{1,15}',options[0]) is None or
            re.fullmatch(r'--cpu=[a-z0-9_-]{1,15}',options[1]) is None or options[2:]!=SAFETY_OPTIONS):
        raise ValueError('target options must use the fixed safe compilation profile')
    options_hash=digest(json.dumps(options,separators=(',',':')).encode('utf-8'))
    if prov.get('target_options')!=options or prov.get('target_options_sha256')!=options_hash:
        raise ValueError('compiler options differ from source lock')
    patches=prov.get('patches')
    if (not isinstance(patches,list) or not patches or
            [item.get('name') for item in patches if isinstance(item,dict)]!=lock.get('patches') or
            len({item['name'] for item in patches})!=len(patches)):
        raise ValueError('compiler patches differ from source lock')
    patch_hashes=[]
    for item in patches:
        expected=hex_value(item['sha256'],32,'compiler patch SHA256')
        if digest(snapshot(Path(source_lock).resolve().parent/item['name']))!=expected:
            raise ValueError('compiler source patch changed')
        patch_hashes.append(expected)
    if patch_set_sha256(patch_hashes)!=profile['compiler_patch_sha256']:
        raise ValueError('compiler patch set mismatch')
    if isinstance(runtime_patches,(str,Path)):runtime_patches=[runtime_patches]
    if len({Path(path).resolve() for path in runtime_patches})!=len(runtime_patches):
        raise ValueError('duplicate runtime patch')
    runtime_hash=patch_set_sha256([digest(snapshot(path)) for path in runtime_patches])
    if runtime_hash!=profile['runtime_patch_sha256']: raise ValueError('runtime patch set mismatch')
    compiler=Path(prov['compiler']['path']).resolve()
    if digest(snapshot(compiler))!=hex_value(prov['compiler']['sha256'],32,'compiler SHA256'):
        raise ValueError('compiler executable changed')
    manifest=tinyrt.manifest_path(manifest);snapshot(manifest)
    app=tinyrt.load_manifest(manifest)
    for source in app['sources']:snapshot(tinyrt.local_file(manifest.parent,source))
    wasm_bytes=snapshot(wasm)
    tinyrt.check_graphics_imports(wasm_bytes)
    cover=None;cover_path=None
    if app.get('cover') is not None:
        cover_path=tinyrt.local_file(manifest.parent,app['cover'])
        cover_source=package.read_cover_source(cover_path);watched[cover_path]=digest(cover_source)
        cover=package.encode_cover_png(cover_source)
    assets=snapshot(tinyrt.local_file(manifest.parent,app['assets'])) if app.get('assets') is not None else b''
    private=serialization.load_pem_private_key(snapshot(signing_key),password=None)
    if not isinstance(private,ec.EllipticCurvePrivateKey) or not isinstance(private.curve,ec.SECP256R1):
        raise ValueError('signing key must be P-256')
    public=private.public_key().public_bytes(serialization.Encoding.X962,serialization.PublicFormat.UncompressedPoint)
    if digest(public)!=profile['signing_key_sha256']:raise ValueError('signing key identity mismatch')
    if profile['development']:
        if not app['app_id'].startswith('demo.') or len(app['app_id'])<=5:
            raise ValueError('development AOT is limited to demo. descendants')
    elif private.private_numbers().private_value==1:
        raise ValueError('public development key cannot be a release authority')
    kwargs={name:app[name] for name in tinyrt.REQUIRED-{'sources'}}
    kwargs.update(key_id=profile['signing_key_id'],private_key=private)
    # Validate metadata, Wasm envelope and maximum input sizes before any process.
    package.build_package(wasm_bytes,b'',**kwargs)
    if len(assets)>package.MAX_PACKAGE_SIZE:raise ValueError('assets exceed 2 MiB')
    output=tinyrt.protect_output(output,list(watched),'.trpkg',b'TRPKG001')
    compat=compatibility_identity(lock,prov,profile)
    with tempfile.TemporaryDirectory(prefix='tinyrt-release-') as directory:
        staged=Path(directory);input_wasm=staged/'input.wasm';native=staged/'output.aot'
        input_wasm.write_bytes(wasm_bytes)
        subprocess.run([str(compiler),*options,'-o',str(native),str(input_wasm)],check=True,
                       capture_output=True,timeout=300)
        if native.is_symlink() or not native.is_file():raise ValueError('compiler did not produce a regular AOT artifact')
        native_bytes=package.read_bounded(native)
        if input_wasm.read_bytes()!=wasm_bytes:raise ValueError('compiler input changed during compilation')
        for path,expected in watched.items():
            if digest(package.read_cover_source(path) if path==cover_path else path.read_bytes())!=expected:raise ValueError('input artifact changed during compilation: '+path.name)
        metadata=bytearray(256)
        struct.pack_into('<HHII',metadata,0,1,256,lock['aot_format_version'],7)
        for start,option in ((16,options[0]),(32,options[1])):
            value=option.split('=',1)[1].encode('ascii');metadata[start:start+len(value)]=value
        metadata[48:68]=bytes.fromhex(lock['wamr']['commit']);metadata[68:88]=bytes.fromhex(lock['llvm']['commit'])
        metadata[88:120]=hashlib.sha256(bytes.fromhex(profile['compiler_patch_sha256'])+bytes.fromhex(runtime_hash)).digest()
        metadata[120:152]=bytes.fromhex(compat);metadata[152:184]=bytes.fromhex(options_hash)
        metadata[184:216]=bytes.fromhex(prov['compiler']['sha256']);metadata[216:248]=hashlib.sha256(wasm_bytes).digest()
        result=package._assemble(wasm_bytes,assets,native=native_bytes,native_metadata=metadata,cover=cover,
                                   include_wasm=include_wasm,**kwargs)
        package.validate_envelope(result,private.public_key(),profile['signing_key_id'])
        # Recheck replacement rules after the compiler process as well.
        output=tinyrt.protect_output(output,list(watched),'.trpkg',b'TRPKG001')
        tinyrt.write_atomic(output,result)
    return dict(package=str(output),package_size=len(result),sha256=digest(result),compat_id=compat,
                source_wasm_sha256=digest(wasm_bytes),compiler_sha256=prov['compiler']['sha256'],
                development=profile['development'])

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('manifest','wasm','source-lock','provenance','release-profile','key','output'):
        parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--runtime-patch',required=True,type=Path,action='append',help='ordered runtime patch set; repeat for each patch')
    parser.add_argument('--without-wasm',action='store_true',help='omit interpreter fallback')
    args=parser.parse_args(argv)
    try:
        result=compile_release(args.manifest,args.wasm,args.source_lock,args.provenance,args.release_profile,
            args.runtime_patch,args.key,args.output,include_wasm=not args.without_wasm)
        print(json.dumps(result,sort_keys=True));return 0
    except (OSError,ValueError,TypeError,KeyError,UnicodeError,subprocess.SubprocessError) as error:
        parser.exit(2,'release_compile: '+str(error)+'\n')

if __name__=='__main__':raise SystemExit(main())
