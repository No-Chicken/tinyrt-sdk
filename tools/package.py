#!/usr/bin/env python3
"""TinyRT section package format 1 packer. Requires cryptography >= 43.

Devices no longer check signatures. Packages stay signed with the public scalar
1 key, invisibly to developers, so they still install on older firmware.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

HEADER_SIZE = 256
# 格式健全性上限（16 MiB Flash）；实际单包上限是设备 HELLO 报告的包库数据区。
MAX_PACKAGE_SIZE = 0x1000000
P256_ORDER = 0xFFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551
DOMAIN = b"TinyRT-package-v1\0"

KEY_ID = 1

def legacy_signature(signed):
    """Old firmware requires a canonical public-key signature; it proves nothing."""
    der = ec.derive_private_key(1, ec.SECP256R1()).sign(DOMAIN+signed, ec.ECDSA(hashes.SHA256(),deterministic_signing=True))
    r, s = utils.decode_dss_signature(der)
    return r.to_bytes(32,'big')+min(s,P256_ORDER-s).to_bytes(32,'big')

def uint32(value):
    result = int(value, 0)
    if not 0 <= result <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError("expected uint32")
    return result

def metadata_header(wasm, assets, *, app_id, title, version, abi_version,
                  permissions, memory_pages, budget):
    """Encode validated metadata; runtime still validates Wasm/ABI."""
    if re.fullmatch(r"[a-z0-9._-]{1,31}", app_id) is None:
        raise ValueError("app_id must be 1..31 ASCII [a-z0-9._-] bytes")
    encoded_title = title.encode("utf-8", errors="strict")
    if not 1 <= len(encoded_title) <= 63 or b"\0" in encoded_title:
        raise ValueError("title must be 1..63 UTF-8 bytes without NUL")
    if not 1 <= version <= 0xFFFFFFFF or abi_version != 1:
        raise ValueError("version must be positive uint32 and ABI must be 1")
    if not 0 <= permissions <= 63:
        raise ValueError("unknown permission bits")
    if not 1 <= memory_pages <= 16 or not 1 <= budget <= 100000:
        raise ValueError("memory pages or instruction budget exceed v1 policy")
    if len(wasm) <= 8 or not wasm.startswith(b"\0asm\x01\0\0\0"):
        raise ValueError("input must begin with Wasm v1 magic and contain sections")
    header = bytearray(HEADER_SIZE)
    struct.pack_into('<6I', header, 32, version, abi_version, permissions,
                     memory_pages, budget, KEY_ID)
    header[56:56+len(app_id)] = app_id.encode('ascii')
    header[88:88+len(encoded_title)] = encoded_title
    return header

META_SIZE = 256
COVER_HEADER = struct.pack('<8sHHIHHHHII',b'TRCOV001',1,1,32,210,210,150,150,88200,45000)
COVER_SECTION_SIZE = 133232

def decode_cover(raw):
    if len(raw)!=COVER_SECTION_SIZE or raw[:32]!=COVER_HEADER:
        raise ValueError('invalid cover codec, dimensions, header or bounded size')
    return dict(codec=1,size=len(raw),sha256=hashlib.sha256(raw).hexdigest())

def encode_cover_png(raw):
    """Bound and inspect PNG before decode; precompute device-ready opaque pixels."""
    import io
    from PIL import Image
    if len(raw)>65536 or len(raw)<33 or raw[:8]!=b'\x89PNG\r\n\x1a\n' or raw[8:16]!=b'\0\0\0\rIHDR':
        raise ValueError('cover must be a PNG of at most 64 KiB')
    if struct.unpack_from('>II',raw,16)!=(210,210):
        raise ValueError('cover PNG must be exactly 210x210')
    with Image.open(io.BytesIO(raw)) as source:
        if source.format!='PNG' or source.size!=(210,210) or getattr(source,'n_frames',1)!=1:
            raise ValueError('cover must be a single-frame 210x210 PNG')
        source.load()
        background=Image.new('RGBA',(210,210),(16,20,24,255))
        background.alpha_composite(source.convert('RGBA'))
        image=background.convert('RGB')
        result=bytearray(COVER_HEADER)
        for pixels in (image,image.resize((150,150),Image.Resampling.LANCZOS)):
            for red,green,blue in struct.iter_unpack('BBB',pixels.tobytes()):
                result+=struct.pack('<H',((red>>3)<<11)|((green>>2)<<5)|(blue>>3))
    return bytes(result)

def read_cover_source(path):
    with Path(path).open('rb') as source:raw=source.read(65537)
    if len(raw)>65536:raise ValueError('cover PNG exceeds 64 KiB')
    return raw

def read_cover_png(path):
    return encode_cover_png(read_cover_source(path))


def _text(raw, encoding, pattern=None):
    end = raw.find(b'\0')
    if end < 1 or any(raw[end:]):
        raise ValueError('noncanonical text or padding')
    value = raw[:end].decode(encoding, errors='strict')
    if pattern and re.fullmatch(pattern, value) is None:
        raise ValueError('invalid text')
    return value

def decode_aot_metadata(raw):
    if len(raw) != 256 or struct.unpack_from('<HH', raw) != (1,256):
        raise ValueError('invalid AOT metadata version or size')
    version, flags = struct.unpack_from('<II', raw, 4)
    if not version or flags != 7 or any(raw[12:16]) or any(raw[248:256]):
        raise ValueError('invalid AOT safety flags or reserved bytes')
    result = dict(format_version=version, safety_flags=flags,
                  target_arch=_text(raw[16:32],'ascii',r'[a-z0-9_-]{1,15}'),
                  target_cpu=_text(raw[32:48],'ascii',r'[a-z0-9_-]{1,15}'))
    for name, start, size in [('wamr_commit',48,20),('llvm_commit',68,20),
        ('patch_sha256',88,32),('compat_id',120,32),('options_sha256',152,32),
        ('compiler_sha256',184,32),('source_wasm_sha256',216,32)]:
        if not any(raw[start:start+size]): raise ValueError('empty AOT source identity')
        result[name] = raw[start:start+size].hex()
    return result

def _assemble(wasm, assets, *, native=None, native_metadata=None, include_wasm=True, cover=None, **metadata):
    header = metadata_header(wasm,b'',**metadata); header[:8] = b'TRPKG001'
    sections = [(1,bytes(wasm))] if include_wasm else []
    if native is not None:
        aot = decode_aot_metadata(native_metadata)
        if aot['source_wasm_sha256'] != hashlib.sha256(wasm).hexdigest():
            raise ValueError('AOT source identity does not match compiled Wasm')
        if len(native) <= 8 or native[:4] != b'\0aot' or struct.unpack_from('<I',native,4)[0] != aot['format_version']:
            raise ValueError('AOT module header does not match metadata')
        sections.append((2,bytes(native_metadata)+bytes(native)))
    if assets: sections.append((3,bytes(assets)))
    if cover is not None:
        decode_cover(cover);sections.append((4,bytes(cover)))
    if not sections or sections[0][0] == 3: raise ValueError('package requires executable section')
    cursor = 256 + 16*len(sections)
    table, body = bytearray(), bytearray()
    for kind, content in sections:
        pad = (-cursor) % 4; body += bytes(pad); cursor += pad
        table += struct.pack('<4I',kind,0,cursor,len(content))
        body += content; cursor += len(content)
    if cursor > MAX_PACKAGE_SIZE: raise ValueError('complete package exceeds the 16 MiB format limit')
    struct.pack_into('<HH5I',header,8,1,256,cursor,256,len(sections),16,0)
    payload = table + body; header[152:184] = hashlib.sha256(payload).digest()
    header[192:256] = legacy_signature(bytes(header[:192]))
    return bytes(header+payload)

def build_wasm_package(wasm, assets, **metadata):
    """Developer packer: intentionally exposes no AOT input or safety assertions."""
    return _assemble(wasm,assets,**metadata)

def validate_envelope(data):
    """Check envelope structure and hashes only. Does not load or execute code."""
    if not 272 <= len(data) <= MAX_PACKAGE_SIZE or data[:8] != b'TRPKG001':
        raise ValueError('invalid format-1 size or magic')
    fmt,header,total,table,count,entry,flags = struct.unpack_from('<HH5I',data,8)
    if (fmt,header,total,table,entry,flags) != (1,256,len(data),256,16,0) or not 1 <= count <= 4:
        raise ValueError('invalid format-1 header')
    version,abi,permissions,pages,budget,key_id = struct.unpack_from('<6I',data,32)
    if not version or abi!=1 or permissions & ~63 or not 1<=pages<=16 or not 1<=budget<=100000 or any(data[184:192]):
        raise ValueError('invalid package policy')
    app_id = _text(data[56:88],'ascii',r'[a-z0-9._-]{1,31}')
    title = _text(data[88:152],'utf-8')
    if hashlib.sha256(data[256:]).digest()!=data[152:184]: raise ValueError('payload hash mismatch')
    cursor = 256+count*16; previous=0; sections={}
    if cursor>len(data): raise ValueError('truncated section table')
    for i in range(count):
        kind, flags, offset, size = struct.unpack_from('<4I',data,256+16*i)
        aligned=(cursor+3)&~3
        if not previous<kind<=4 or flags or offset!=aligned or any(data[cursor:aligned]) or size<1 or offset+size>len(data):
            raise ValueError('invalid section table, range or padding')
        sections[kind]=data[offset:offset+size];previous=kind;cursor=offset+size
    if cursor!=len(data) or not ({1,2}&sections.keys()): raise ValueError('missing executable section or trailing bytes')
    wasm=sections.get(1,b'');aot=None;native_size=0
    if 1 in sections and (len(wasm)<=8 or wasm[:8]!=b'\0asm\1\0\0\0'):
        raise ValueError('invalid Wasm magic/version')
    if 2 in sections:
        if len(sections[2])<=264: raise ValueError('truncated native section')
        aot=decode_aot_metadata(sections[2][:256]);native=sections[2][256:];native_size=len(native)
        if native[:4]!=b'\0aot' or struct.unpack_from('<I',native,4)[0]!=aot['format_version']:
            raise ValueError('native header does not match metadata')
        if wasm and hashlib.sha256(wasm).hexdigest()!=aot['source_wasm_sha256']:
            raise ValueError('AOT source identity mismatch')
    cover=decode_cover(sections[4]) if 4 in sections else None
    return dict(validation_level='envelope',wasm_validation='not_performed',aot_validation='not_performed',
                format_version=1,app_id=app_id,title=title,version=version,package_size=len(data),key_id=key_id,
                sha256=hashlib.sha256(data).hexdigest(),wasm_size=len(wasm),assets_size=len(sections.get(3,b'')),
                aot_size=native_size,aot=aot,cover=cover)


build_package = build_wasm_package

def read_bounded(path):
    with Path(path).open("rb") as source:
        data = source.read(MAX_PACKAGE_SIZE + 1)
    if len(data) > MAX_PACKAGE_SIZE:
        raise ValueError("input exceeds maximum package size")
    return data

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    pack = commands.add_parser("pack", help="build a TinyRT Wasm package")
    pack.add_argument("--wasm", required=True, type=Path)
    pack.add_argument("--assets", type=Path)
    pack.add_argument("--cover", type=Path, help="single-frame 210x210 PNG, at most 64 KiB")
    pack.add_argument("--output", required=True, type=Path)
    pack.add_argument("--app-id", required=True)
    pack.add_argument("--title", required=True)
    pack.add_argument("--version", required=True, type=uint32)
    pack.add_argument("--abi-version", type=uint32, default=1)
    pack.add_argument("--permissions", type=uint32, default=15)
    pack.add_argument("--memory-pages", type=uint32, default=4)
    pack.add_argument("--budget", type=uint32, default=10000)
    args = parser.parse_args(argv)
    try:
        for source in (args.wasm, args.assets, args.cover):
            if source is not None and (args.output.resolve() == source.resolve() or
                    (args.output.exists() and args.output.samefile(source))):
                raise ValueError("output must not overwrite an input")
        builder = build_package
        data = builder(read_bounded(args.wasm), read_bounded(args.assets) if args.assets else b"",
            cover=read_cover_png(args.cover) if args.cover else None,
            app_id=args.app_id, title=args.title, version=args.version, abi_version=args.abi_version,
            permissions=args.permissions, memory_pages=args.memory_pages, budget=args.budget)
        args.output.write_bytes(data)
        print(json.dumps({"app_id": args.app_id, "version": args.version,
            "package_size": len(data), "sha256": hashlib.sha256(data).hexdigest()}, sort_keys=True))
        return 0
    except (OSError, ValueError, TypeError, UnicodeError) as error:
        parser.exit(2, f"package: {error}\n")

if __name__ == "__main__":
    raise SystemExit(main())
