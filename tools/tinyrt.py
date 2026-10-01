#!/usr/bin/env python3
"""Standalone TinyRT SDK: create, build, sign and inspect ABI 1 applications.

validate checks only the authenticated package envelope. TinyRT core remains
responsible for full Wasm structure, imports/exports, limits and actual execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = {'app_id','title','version','abi_version','permissions','memory_pages','budget','sources'}
OPTIONAL = {'assets','defines'}


def integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{name} must be an integer in {low}..{high}')
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result: raise ValueError(f'duplicate manifest field: {key}')
        result[key] = value
    return result


def manifest_path(path):
    path = Path(path).resolve()
    return path / 'app.json' if path.is_dir() else path


def local_file(root, name):
    if not isinstance(name,str) or not name or Path(name).is_absolute():
        raise ValueError('manifest paths must be nonempty relative paths')
    target = (root / name).resolve()
    if not target.is_relative_to(root.resolve()) or not target.is_file():
        raise ValueError(f'manifest file must exist inside the app directory: {name}')
    return target


def load_manifest(path):
    path = manifest_path(path)
    if path.stat().st_size > 16384: raise ValueError('manifest exceeds 16 KiB')
    value = json.loads(path.read_text(encoding='utf-8-sig'),object_pairs_hook=unique_object)
    if not isinstance(value,dict): raise ValueError('manifest must be an object')
    missing, extra = REQUIRED - value.keys(), value.keys() - REQUIRED - OPTIONAL
    if missing or extra: raise ValueError(f'missing fields: {sorted(missing)}; unknown fields: {sorted(extra)}')
    if not isinstance(value['app_id'],str) or re.fullmatch(r'[a-z0-9._-]{1,31}',value['app_id']) is None:
        raise ValueError('app_id must be 1..31 ASCII [a-z0-9._-] bytes')
    title = value['title']
    if not isinstance(title,str) or not 1 <= len(title.encode('utf-8')) <= 63 or '\0' in title:
        raise ValueError('title must be 1..63 UTF-8 bytes without NUL')
    for key,low,high in [('version',1,0xffffffff),('abi_version',1,1),('permissions',0,15),
                         ('memory_pages',1,16),('budget',1,100000)]:
        integer(value[key],low,high,key)
    sources = value['sources']
    if not isinstance(sources,list) or not 1 <= len(sources) <= 64:
        raise ValueError('sources must contain 1..64 local C source paths')
    resolved = []
    for source in sources:
        item=local_file(path.parent,source)
        if item.suffix != '.c' or item in resolved: raise ValueError('sources must be unique C files')
        resolved.append(item)
    if value.get('assets') is not None: local_file(path.parent,value['assets'])
    defines=value.get('defines',{})
    if not isinstance(defines,dict) or len(defines)>32: raise ValueError('defines must be an object with at most 32 entries')
    for name,number in defines.items():
        if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',name) is None: raise ValueError('invalid define name')
        integer(number,-2147483648,4294967295,'define value')
    return value


def protect_output(output, inputs, suffix, magic):
    """Limit replacements to expected artifacts, including resolved link targets.

    Header dependencies need not appear in sources, so protecting only declared
    files is insufficient. This format guard protects other existing files while
    preserving ordinary rebuilds; it is not full Wasm/package validation.
    """
    requested=Path(output)
    output=requested.resolve()
    if requested.suffix.lower()!=suffix or output.suffix.lower()!=suffix:
        raise ValueError(f'output must use {suffix} extension (including resolved link target)')
    for source in inputs:
        if source is None: continue
        source=Path(source).resolve()
        if output==source or (output.exists() and source.exists() and output.samefile(source)):
            raise ValueError('output must not overwrite an input or signing key')
    if output.exists():
        if not output.is_file(): raise ValueError('output must be a regular artifact file')
        with output.open('rb') as stream:
            if stream.read(len(magic))!=magic:
                raise ValueError(f'existing output is not a {suffix} artifact; choose a new path')
    return output


def write_atomic(output,data):
    output.parent.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix=output.name+'.',dir=output.parent)
    try:
        with os.fdopen(fd,'wb') as stream: stream.write(data)
        os.replace(temp,output)
    finally:
        if os.path.exists(temp): os.unlink(temp)


def build(path,cc=None,output=None):
    path=manifest_path(path);m=load_manifest(path)
    cc=cc or os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
    if not cc: raise ValueError('pass --cc pointing to clang with Wasm support, or Zig 0.13.0')
    resolved_cc=shutil.which(cc)
    if resolved_cc: cc=resolved_cc
    cc=str(Path(cc).resolve())
    sources=[local_file(path.parent,name) for name in m['sources']]
    assets=local_file(path.parent,m['assets']) if m.get('assets') is not None else None
    output=protect_output(output or path.parent/'build'/(m['app_id']+'.wasm'),[path,*sources,assets],'.wasm',b'\0asm\x01\0\0\0')
    output.parent.mkdir(parents=True,exist_ok=True)
    command=([cc,'cc','-target','wasm32-freestanding'] if Path(cc).stem.lower()=='zig'
             else [cc,'--target=wasm32-unknown-unknown'])
    command+=['-std=c11','-O2','-nostdlib','-fno-builtin','-Wall','-Wextra','-Werror',
              '-Wno-unused-parameter','-I',str(ROOT/'include'),'-Wl,--no-entry',
              '-Wl,--export=tinyrt_init','-Wl,--export=tinyrt_event','-Wl,--export=tinyrt_render',
              '-Wl,-z,stack-size=16384',
              f'-Wl,--max-memory={m["memory_pages"]*65536:#x}','-Wl,--strip-all']
    command += [f'-D{name}={value}' for name,value in m.get('defines',{}).items()]
    env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(output.parent/'zig-cache'))
    with tempfile.TemporaryDirectory(prefix='tinyrt-build-',dir=output.parent) as temp:
        compiled=Path(temp)/'app.wasm'
        subprocess.run(command+list(map(str,sources))+['-o',str(compiled)],check=True,env=env)
        write_atomic(output,compiled.read_bytes())
    return {'app_id':m['app_id'],'wasm':str(output),'wasm_size':output.stat().st_size}


def validate_envelope(data,public_key,expected_key_id):
    """Authenticate canonical envelope only; does NOT load or execute Wasm."""
    import package
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec,utils
    integer(expected_key_id,0,0xffffffff,'key ID')
    if not isinstance(public_key,ec.EllipticCurvePublicKey) or not isinstance(public_key.curve,ec.SECP256R1):
        raise ValueError('verification key must be P-256')
    if not 265 <= len(data) <= package.MAX_PACKAGE_SIZE or data[:8]!=b'TRPKG001':
        raise ValueError('invalid package size or magic')
    fields=struct.unpack_from('<HH11I',data,8)
    fmt,header,total,wo,ws,ao,az,version,abi,permissions,pages,budget,key_id=fields
    if (fmt!=1 or header!=256 or total!=len(data) or wo!=256 or ws<=8 or
            ws>len(data)-256 or ao!=256+ws or az!=len(data)-ao):
        raise ValueError('invalid package layout')
    for value,low,high,name in [(version,1,0xffffffff,'version'),(abi,1,1,'ABI'),
        (permissions,0,15,'permissions'),(pages,1,16,'memory pages'),(budget,1,100000,'budget')]:
        integer(value,low,high,name)
    if key_id!=expected_key_id: raise ValueError('package key ID is not the explicitly trusted ID')
    def padded_text(blob,encoding):
        end=blob.find(b'\0')
        if end<1 or any(blob[end:]): raise ValueError('noncanonical text padding')
        try: return blob[:end].decode(encoding,errors='strict')
        except UnicodeError as error: raise ValueError('invalid package text') from error
    app_id=padded_text(data[56:88],'ascii');title=padded_text(data[88:152],'utf-8')
    if re.fullmatch(r'[a-z0-9._-]{1,31}',app_id) is None: raise ValueError('invalid app ID')
    if any(data[184:192]): raise ValueError('nonzero reserved bytes')
    if data[256:264]!=b'\0asm\x01\0\0\0': raise ValueError('invalid Wasm magic/version')
    if hashlib.sha256(data[256:]).digest()!=data[152:184]: raise ValueError('payload hash mismatch')
    r=int.from_bytes(data[192:224],'big');s=int.from_bytes(data[224:256],'big')
    if not 0<r<package.P256_ORDER or not 0<s<=package.P256_ORDER//2:
        raise ValueError('invalid or noncanonical signature')
    try: public_key.verify(utils.encode_dss_signature(r,s),package.DOMAIN+data[:192],ec.ECDSA(hashes.SHA256()))
    except InvalidSignature as error: raise ValueError('signature verification failed') from error
    return {'validation_level':'envelope','wasm_validation':'not_performed','app_id':app_id,
            'title':title,'version':version,'package_size':len(data),'key_id':key_id,
            'sha256':hashlib.sha256(data).hexdigest()}


def signing_options(parser,verify=False):
    parser.add_argument('--key-id',required=True,type=lambda s:int(s,0))
    group=parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--public-key' if verify else '--key',type=Path,
                       help='P-256 SEC1 or PEM public key' if verify else 'existing unencrypted P-256 PEM key')
    group.add_argument('--development-key',action='store_true',help='PUBLIC TEST key; explicitly opt in')


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    new=commands.add_parser('new',help='create a minimal app in a new directory')
    new.add_argument('directory',type=Path);new.add_argument('--app-id',required=True);new.add_argument('--title',default='TinyRT app')
    compile_cmd=commands.add_parser('build',help='compile freestanding C to Wasm without ESP-IDF')
    compile_cmd.add_argument('app',type=Path);compile_cmd.add_argument('--cc');compile_cmd.add_argument('--output',type=Path)
    pack=commands.add_parser('pack',help='sign package using app.json metadata')
    pack.add_argument('app',type=Path);pack.add_argument('--wasm',type=Path);pack.add_argument('--output',type=Path);signing_options(pack)
    validate=commands.add_parser('validate',help='check authenticated envelope ONLY; no Wasm loading or execution')
    validate.add_argument('package',type=Path);signing_options(validate,True)
    args=parser.parse_args(argv)
    try:
        if args.command=='new':
            if args.directory.exists(): raise ValueError('destination already exists')
            with tempfile.TemporaryDirectory(prefix='tinyrt-new-') as temp:
                staged=Path(temp)/'app';shutil.copytree(ROOT/'templates/minimal',staged)
                m=json.loads((staged/'app.json').read_text(encoding='utf-8-sig'))
                m.update(app_id=args.app_id,title=args.title)
                (staged/'app.json').write_text(json.dumps(m,indent=2)+'\n',encoding='utf-8')
                load_manifest(staged)
                shutil.copytree(staged,args.directory)
            result={'app':str(args.directory.resolve()),'app_id':args.app_id}
        elif args.command=='build':result=build(args.app,args.cc,args.output)
        else:
            import package
            from cryptography.hazmat.primitives import serialization
            from cryptography.hazmat.primitives.asymmetric import ec
            integer(args.key_id,0,0xffffffff,'key ID')
            if args.development_key: print('WARNING: PUBLIC development key for TESTS ONLY.',file=sys.stderr)
            if args.command=='pack':
                path=manifest_path(args.app);m=load_manifest(path)
                wasm=args.wasm or path.parent/'build'/(m['app_id']+'.wasm')
                assets=local_file(path.parent,m['assets']) if m.get('assets') is not None else None
                inputs=[path,wasm,assets,args.key,*[local_file(path.parent,s) for s in m['sources']]]
                output=protect_output(args.output or path.parent/'build'/(m['app_id']+'.trpkg'),inputs,'.trpkg',b'TRPKG001')
                key=package.development_key() if args.development_key else serialization.load_pem_private_key(args.key.read_bytes(),None)
                data=package.build_package(package.read_bounded(wasm),package.read_bounded(assets) if assets else b'',
                    **{k:m[k] for k in REQUIRED-{'sources'}},key_id=args.key_id,private_key=key)
                write_atomic(output,data)
                result={'package':str(output),'app_id':m['app_id'],'package_size':len(data),
                        'sha256':hashlib.sha256(data).hexdigest(),'development_key':args.development_key}
            else:
                if args.development_key: public=package.development_key().public_key()
                else:
                    raw=args.public_key.read_bytes()
                    public=serialization.load_pem_public_key(raw) if raw.startswith(b'-----BEGIN') else ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(),raw)
                result=validate_envelope(package.read_bounded(args.package),public,args.key_id)
        print(json.dumps(result,sort_keys=True));return 0
    except (OSError,ValueError,TypeError,UnicodeError,subprocess.CalledProcessError) as error:
        parser.exit(2,f'tinyrt: {error}\n')

if __name__=='__main__':raise SystemExit(main())
