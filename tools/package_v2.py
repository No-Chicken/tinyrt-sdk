"""Canonical v2 envelope. A signature authenticates claims, not native-code safety."""
import hashlib
import re
import struct
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils
import package

DOMAIN = b'TinyRT-package-v2\0'
META_SIZE = 256

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

def _assemble(wasm, assets, *, native=None, native_metadata=None, include_wasm=True, **metadata):
    # Reuse the mature v1 metadata/key validator, without changing v1 output bytes.
    base = package.build_package(wasm,b'',**metadata)
    header = bytearray(base[:256]); header[:8] = b'TRPKG002'
    sections = [(1,bytes(wasm))] if include_wasm else []
    if native is not None:
        aot = decode_aot_metadata(native_metadata)
        if aot['source_wasm_sha256'] != hashlib.sha256(wasm).hexdigest():
            raise ValueError('AOT source identity does not match compiled Wasm')
        if len(native) <= 8 or native[:4] != b'\0aot' or struct.unpack_from('<I',native,4)[0] != aot['format_version']:
            raise ValueError('AOT module header does not match metadata')
        sections.append((2,bytes(native_metadata)+bytes(native)))
    if assets: sections.append((3,bytes(assets)))
    if not sections or sections[0][0] == 3: raise ValueError('package requires executable section')
    cursor = 256 + 16*len(sections)
    table, body = bytearray(), bytearray()
    for kind, content in sections:
        pad = (-cursor) % 4; body += bytes(pad); cursor += pad
        table += struct.pack('<4I',kind,0,cursor,len(content))
        body += content; cursor += len(content)
    if cursor > package.MAX_PACKAGE_SIZE: raise ValueError('complete package exceeds 2 MiB')
    struct.pack_into('<HH5I',header,8,2,256,cursor,256,len(sections),16,0)
    payload = table + body; header[152:184] = hashlib.sha256(payload).digest()
    signature = metadata['private_key'].sign(DOMAIN+header[:192],ec.ECDSA(hashes.SHA256(),deterministic_signing=True))
    r,s = utils.decode_dss_signature(signature)
    header[192:256] = r.to_bytes(32,'big')+min(s,package.P256_ORDER-s).to_bytes(32,'big')
    return bytes(header+payload)

def build_wasm_package(wasm, assets, **metadata):
    """Developer packer: intentionally exposes no AOT input or safety assertions."""
    return _assemble(wasm,assets,**metadata)

def validate_envelope(data, public_key, expected_key_id):
    """Authenticate envelope only. Does not authorize a host, load, or execute code."""
    if not isinstance(public_key,ec.EllipticCurvePublicKey) or not isinstance(public_key.curve,ec.SECP256R1):
        raise ValueError('verification key must be P-256')
    if type(expected_key_id) is not int or not 0 <= expected_key_id <= 0xffffffff:
        raise ValueError('invalid expected key ID')
    if not 272 <= len(data) <= package.MAX_PACKAGE_SIZE or data[:8] != b'TRPKG002':
        raise ValueError('invalid v2 size or magic')
    fmt,header,total,table,count,entry,flags = struct.unpack_from('<HH5I',data,8)
    if (fmt,header,total,table,entry,flags) != (2,256,len(data),256,16,0) or not 1 <= count <= 3:
        raise ValueError('invalid v2 header')
    version,abi,permissions,pages,budget,key_id = struct.unpack_from('<6I',data,32)
    if not version or abi!=1 or permissions & ~15 or not 1<=pages<=16 or not 1<=budget<=100000 or any(data[184:192]):
        raise ValueError('invalid package policy')
    if key_id != expected_key_id: raise ValueError('package key ID is not the explicitly trusted ID')
    app_id = _text(data[56:88],'ascii',r'[a-z0-9._-]{1,31}')
    title = _text(data[88:152],'utf-8')
    r,s = int.from_bytes(data[192:224],'big'),int.from_bytes(data[224:256],'big')
    if not 0<r<package.P256_ORDER or not 0<s<=package.P256_ORDER//2: raise ValueError('noncanonical signature')
    from cryptography.exceptions import InvalidSignature
    try: public_key.verify(utils.encode_dss_signature(r,s),DOMAIN+data[:192],ec.ECDSA(hashes.SHA256()))
    except InvalidSignature as error: raise ValueError('signature verification failed') from error
    if hashlib.sha256(data[256:]).digest()!=data[152:184]: raise ValueError('payload hash mismatch')
    cursor = 256+count*16; previous=0; sections={}
    if cursor>len(data): raise ValueError('truncated section table')
    for i in range(count):
        kind, flags, offset, size = struct.unpack_from('<4I',data,256+16*i)
        aligned=(cursor+3)&~3
        if not previous<kind<=3 or flags or offset!=aligned or any(data[cursor:aligned]) or size<1 or offset+size>len(data):
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
    return dict(validation_level='envelope',wasm_validation='not_performed',aot_validation='not_performed',
                format_version=2,app_id=app_id,title=title,version=version,package_size=len(data),key_id=key_id,
                sha256=hashlib.sha256(data).hexdigest(),wasm_size=len(wasm),assets_size=len(sections.get(3,b'')),
                aot_size=native_size,aot=aot)
