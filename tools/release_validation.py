"""Offline v2 bundle checks shared by release creation and ZIP self-inspection."""
import hashlib
import io
import json
from pathlib import Path
import re
import stat
import struct
import zipfile

from PIL import Image
import package
import tinyrt

DOCS = ('README.md', 'CHANGELOG.md', 'LICENSES.md')
MAX_ZIP = 12 * 1024 * 1024
MAX_EXPANDED = 10 * 1024 * 1024


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8')


def text_bytes(raw, name, limit):
    if not 1 <= len(raw) <= limit:
        raise ValueError(f'{name}: empty or exceeds {limit} bytes')
    if raw.startswith(b'\xef\xbb\xbf') or b'\r' in raw:
        raise ValueError(f'{name}: use UTF-8 without BOM and LF line endings')
    raw.decode('utf-8', errors='strict')
    return raw


def read_json(raw, name, limit=32768):
    text_bytes(raw, name, limit)
    def invalid(value): raise ValueError(f'{name}: invalid JSON number {value}')
    return json.loads(raw, object_pairs_hook=tinyrt.unique_object, parse_constant=invalid)


def check_schema(value, name):
    try:
        from jsonschema import Draft202012Validator
    except ImportError as error:
        raise ValueError('release needs requirements.txt dependencies (jsonschema)') from error
    schema = json.loads((tinyrt.ROOT/'specs'/name).read_bytes())
    errors = list(Draft202012Validator(schema).iter_errors(value))
    if errors:
        error = errors[0]
        field = '/'.join(map(str, error.absolute_path))
        raise ValueError(f'{name}/{field}: {error.message}')


def png(raw, name, size, limit):
    if not 1 <= len(raw) <= limit or raw[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError(f'{name}: invalid PNG or exceeds size limit')
    if len(raw) < 33 or struct.unpack_from('>II', raw, 16) != size:
        raise ValueError(f'{name}: PNG must be {size[0]}x{size[1]}')
    with Image.open(io.BytesIO(raw)) as image:
        if image.format != 'PNG' or image.size != size or getattr(image, 'n_frames', 1) != 1 or getattr(image, 'is_animated', False):
            raise ValueError(f'{name}: requires a single-frame PNG')
        image.verify()
    with Image.open(io.BytesIO(raw)) as image:
        image.load()
        if size == (210, 210) and image.mode not in ('RGB', 'RGBA'):
            raise ValueError(f'{name}: cover must be RGB or RGBA')
        return image.convert('RGBA')


def sections(raw):
    result = {}
    for i in range(struct.unpack_from('<I', raw, 20)[0]):
        kind, _, offset, size = struct.unpack_from('<4I', raw, 256 + i*16)
        result[kind] = raw[offset:offset+size]
    return result


class WasmReader:
    def __init__(self, raw): self.raw, self.pos = raw, 0
    def byte(self):
        if self.pos >= len(self.raw): raise ValueError('truncated Wasm import table')
        value = self.raw[self.pos]; self.pos += 1
        return value
    def uint(self):
        value = 0
        for shift in range(0, 35, 7):
            byte = self.byte()
            if shift == 28 and byte > 15: raise ValueError('invalid Wasm uint32')
            value |= (byte & 127) << shift
            if not byte & 128: return value
        raise ValueError('invalid Wasm uint32')
    def take(self, size):
        end = self.pos + size
        if end > len(self.raw): raise ValueError('truncated Wasm section')
        raw = self.raw[self.pos:end]; self.pos = end
        return raw
    def name(self): return self.take(self.uint()).decode('utf-8')


def wasm_imports(raw, permissions):
    if raw[:8] != b'\0asm\1\0\0\0': raise ValueError('invalid Wasm header')
    abi = json.loads((tinyrt.ROOT/'contracts/abi-v1.json').read_bytes())
    allowed = {item['name']: item['permission'] for item in abi['imports']}
    source = WasmReader(raw[8:]); names = []; seen = False
    while source.pos < len(source.raw):
        kind = source.byte(); content = WasmReader(source.take(source.uint()))
        if kind != 2: continue
        if seen: raise ValueError('duplicate Wasm import section')
        seen = True
        count = content.uint()
        if count > 64: raise ValueError('too many Wasm imports')
        for _ in range(count):
            module, name, kind = content.name(), content.name(), content.byte()
            if module != 'tinyrt' or name not in allowed or kind != 0:
                raise ValueError('unsupported Wasm import: '+module+'.'+name)
            content.uint()  # function type index; runtime checks the signature.
            if allowed[name] & permissions != allowed[name]:
                raise ValueError('missing permission for Wasm import: '+name)
            names.append(name)
        if content.pos != len(content.raw): raise ValueError('trailing Wasm import bytes')
    return sorted(set(names))


def file_record(path, raw, **extra):
    return dict(path=path, size=len(raw), sha256=digest(raw), **extra)


def inspect_package(raw, public_key, key_id):
    info = package.validate_envelope(raw, public_key, key_id)
    parts = sections(raw)
    if 1 not in parts: raise ValueError('website bundle requires Wasm fallback')
    version, abi, permissions, pages, budget, actual_key = struct.unpack_from('<6I', raw, 32)
    metadata = file_record('package.trpkg', raw, format_version=info['format_version'],
                           abi_version=abi, permissions=permissions, memory_pages=pages,
                           budget=budget, key_id=actual_key)
    aot = info['aot']
    requirements = dict(core_contract_revision=json.loads((tinyrt.ROOT/'contracts/source.json').read_bytes())['core_revision'],
                        imports=wasm_imports(parts[1], permissions),
                        aot={name:aot[name] for name in ('target_arch','target_cpu','compat_id')} if aot else None)
    return info, parts, metadata, requirements


def check_report(raw):
    report = read_json(raw, 'build/report.json', 262144)
    if not isinstance(report, dict) or set(report) != {'schema', 'steps', 'warnings'} or report['schema'] != 'tinyrt.build-report.v1':
        raise ValueError('invalid build report')
    if not isinstance(report['steps'], list) or not isinstance(report['warnings'], list):
        raise ValueError('invalid build report steps/warnings')
    for step in report['steps']:
        if (not isinstance(step, dict) or not {'name','result'} <= step.keys()
                or step.keys()-{'name','result','detail'} or not isinstance(step['name'], str)
                or step['result'] not in ('passed','not-run','failed')
                or ('detail' in step and not isinstance(step['detail'],str))):
            raise ValueError('invalid build report step')
    if any(not isinstance(warning,str) for warning in report['warnings']):
        raise ValueError('invalid build report warning')
    return report


def validate_bundle(raw, public_key, key_id):
    """Local content validation; server account/key authorization is separate."""
    if len(raw) > MAX_ZIP: raise ValueError('ZIP exceeds 12 MiB')
    try:
        with zipfile.ZipFile(io.BytesIO(raw)) as archive:
            entries = archive.infolist()
            if not 7 <= len(entries) <= 13: raise ValueError('invalid ZIP entry count')
            names = [entry.filename for entry in entries]
            if len(set(name.casefold() for name in names)) != len(names):
                raise ValueError('duplicate ZIP paths')
            files = {}; total = 0
            for entry in entries:
                name = entry.filename
                if not re.fullmatch(r'(release\.json|package\.trpkg|cover\.png|README\.md|CHANGELOG\.md|LICENSES\.md|screenshots/0[1-6]\.png|build/report\.json)', name):
                    raise ValueError('ZIP path not allowed: '+name)
                mode = entry.external_attr >> 16
                if (entry.flag_bits & 1 or entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED)
                        or entry.is_dir() or (stat.S_IFMT(mode) not in (0, stat.S_IFREG))
                        or entry.create_version >= 45 or entry.extract_version >= 45):
                    raise ValueError('unsupported ZIP entry: '+name)
                limit = 2097152 if name == 'package.trpkg' else 1048576 if name.startswith('screenshots/') else 262144 if name == 'build/report.json' else 32768 if name == 'release.json' else 65536
                with archive.open(entry) as stream:
                    data = stream.read(limit+1)
                    if len(data) > limit or stream.read(1): raise ValueError('ZIP file exceeds limit: '+name)
                total += len(data)
                if total > MAX_EXPANDED: raise ValueError('ZIP exceeds 10 MiB expanded')
                files[name] = data
    except (zipfile.BadZipFile, RuntimeError, NotImplementedError) as error:
        raise ValueError('invalid ZIP: '+str(error)) from error
    if 'release.json' not in files: raise ValueError('missing release.json')
    manifest = read_json(files['release.json'], 'release.json')
    check_schema(manifest, 'release-v2.schema.json')
    shots = manifest['screenshots']
    expected = ['release.json','package.trpkg','cover.png',*DOCS] + [f'screenshots/{i:02d}.png' for i in range(1,len(shots)+1)]
    if manifest['build']['report'] is not None: expected.append('build/report.json')
    if names != expected: raise ValueError('ZIP files missing, unregistered or out of order')
    records = [manifest[key] for key in ('package','cover','readme','changelog','licenses')] + shots
    if manifest['build']['report'] is not None: records.append(manifest['build']['report'])
    for record in records:
        data = files.get(record['path'])
        if data is None or file_record(record['path'],data) != {key:record[key] for key in ('path','size','sha256')}:
            raise ValueError('file size/hash mismatch: '+record['path'])
    for name in DOCS: text_bytes(files[name],name,65536)
    png(files['cover.png'],'cover.png',(210,210),65536)
    for i, shot in enumerate(shots, 1):
        if shot['path'] != f'screenshots/{i:02d}.png': raise ValueError('screenshot numbering must be continuous')
        png(files[shot['path']],shot['path'],(466,466),1048576)
    info, parts, metadata, requirements = inspect_package(files['package.trpkg'],public_key,key_id)
    for key in ('app_id','title','version'):
        if manifest[key] != info[key]: raise ValueError('package metadata mismatch: '+key)
    if manifest['package'] != metadata or manifest['requirements'] != requirements:
        raise ValueError('package metadata/imports/AOT/core revision mismatch')
    if manifest['variant'] != ('wasm-aot' if 2 in parts else 'wasm'):
        raise ValueError('package variant mismatch')
    if parts.get(4) != package.encode_cover_png(files['cover.png']):
        raise ValueError('cover does not match signed package')
    development = public_key.public_numbers() == package.development_key().public_key().public_numbers()
    if manifest['signing'] != ('development' if development else 'release'):
        raise ValueError('signing classification mismatch')
    if development and (key_id != 1 or manifest['requested_channel'] != 'development' or not re.fullmatch(r'demo\.[a-z0-9._-]+',info['app_id'])):
        raise ValueError('development signing requires demo. app, key ID 1 and development channel')
    if 'build/report.json' in files:
        report = check_report(files['build/report.json'])
        steps = {step['name']:step['result'] for step in report['steps']}
        for name, field in (('native-tests','native'),('wamr-preview','wamr')):
            if steps.get(name) != manifest['verification'][field]:
                raise ValueError('verification disagrees with build report')
    for name in ('release.json','build/report.json'):
        if name in files and re.search(r'[A-Za-z]:[\\/]|/Users/|/home/|\\Users\\', files[name].decode('utf-8')):
            raise ValueError('local absolute path in '+name)
    return manifest
