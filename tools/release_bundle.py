"""Build one offline, reproducible website ZIP from an application directory."""
from datetime import datetime, timezone
from contextlib import contextmanager
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
import zipfile

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
import package
import preview
import tinyrt
from release_validation import (DOCS, check_schema, digest, file_record, inspect_package,
                                check_report, json_bytes, png, read_json, text_bytes, validate_bundle)


def bounded(path, name, limit):
    if not path.is_file(): raise ValueError('missing file: '+name)
    with path.open('rb') as source: raw = source.read(limit+1)
    if len(raw) > limit: raise ValueError(name+': exceeds size limit')
    return raw


def read_events(raw):
    # Preserve run's BOM/CRLF support; event scripts are inputs, never ZIP members.
    def invalid(value): raise ValueError('invalid events JSON number: '+value)
    return json.loads(raw.decode('utf-8-sig'),object_pairs_hook=tinyrt.unique_object,parse_constant=invalid)


def load_inputs(app, preview_ms):
    manifest_path = tinyrt.manifest_path(app)
    manifest = tinyrt.load_manifest(manifest_path)
    root = manifest_path.parent
    listing_path = tinyrt.local_file(root, 'listing.json')
    listing_raw = bounded(listing_path, 'listing.json', 32768)
    listing = read_json(listing_raw, 'listing.json')
    check_schema(listing, 'listing.schema.json')
    tinyrt.integer(preview_ms, 0, 60000, 'preview duration')
    if manifest.get('cover') != 'cover.png': raise ValueError('app.json cover must be cover.png')
    files = {'cover.png': bounded(tinyrt.local_file(root,'cover.png'),'cover.png',65536)}
    png(files['cover.png'],'cover.png',(210,210),65536)
    for name in DOCS:
        files[name] = text_bytes(bounded(tinyrt.local_file(root,name),name,65536),name,65536)
    snapshots = {manifest_path:manifest_path.read_bytes(),listing_path:listing_raw}
    for name, raw in files.items(): snapshots[tinyrt.local_file(root,name)] = raw
    for shot in listing['screenshots']:
        if 'at_ms' in shot:
            if shot['at_ms'] > preview_ms: raise ValueError('screenshot time exceeds preview duration')
        else:
            name = shot['file']
            if '\\' in name or ':' in name or '..' in Path(name).parts:
                raise ValueError('device screenshot must be an app-relative path')
            path = tinyrt.local_file(root,name)
            raw = bounded(path,name,1048576)
            png(raw,name,(466,466),1048576)
            snapshots[path] = raw
    events = root/'events.json'
    if events.exists():
        events = tinyrt.local_file(root,'events.json')
        raw = bounded(events,'events.json',262144)
        value = read_events(raw)
        if not isinstance(value,list) or len(value)>2000: raise ValueError('events must be a list with at most 2000 entries')
        for event in value:
            if not isinstance(event,dict) or 'ms' not in event: raise ValueError('events require ms')
            tinyrt.integer(event['ms'],0,preview_ms,'event time')
        snapshots[events] = raw
    else: events = None
    for name in manifest['sources']:
        path = tinyrt.local_file(root,name); snapshots[path] = path.read_bytes()
    if manifest.get('assets'):
        path = tinyrt.local_file(root,manifest['assets']); snapshots[path] = package.read_bounded(path)
    return root, manifest, listing, files, snapshots, events


def compiler_info(override):
    cc = override or os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
    if not cc: raise ValueError('release needs a local compiler; pass --cc')
    path = Path(shutil.which(str(cc)) or cc).resolve()
    if not path.is_file(): raise ValueError('local compiler not found; pass --cc')
    version_arg = 'version' if path.stem.lower() == 'zig' else '--version'
    result = subprocess.run([str(path),version_arg],check=True,capture_output=True,text=True,encoding='utf-8',timeout=30)
    version = re.search(r'\d+\.\d+(?:\.\d+)?',result.stdout)
    if not version: raise ValueError('cannot determine compiler version')
    return str(path), path.stem.lower()+' '+version.group()


def local_runner(override):
    path = override or os.environ.get('TINYRT_RUNNER') or tinyrt.ROOT/'bin/tinyrt-run.exe'
    path = Path(path).resolve()
    if not path.is_file(): raise ValueError('release needs a local WAMR runner; pass --runner')
    return path


def build_identity(args):
    def git(*arguments):
        result = subprocess.run(['git','-C',str(tinyrt.ROOT),*arguments],check=True,capture_output=True,text=True,encoding='utf-8',timeout=15)
        return result.stdout.strip()
    revision = args.sdk_revision
    if revision is None:
        try:
            if Path(git('rev-parse','--show-toplevel')).resolve() != tinyrt.ROOT.resolve():
                raise ValueError('SDK is not its own Git repository')
            revision = git('rev-parse','HEAD')
        except (OSError,ValueError,subprocess.SubprocessError) as error:
            raise ValueError('SDK revision unavailable; pass --sdk-revision for a source archive') from error
    if re.fullmatch(r'[0-9a-f]{40}',revision) is None: raise ValueError('sdk_revision must be a 40-character Git SHA')
    if args.built_at:
        timestamp = datetime.strptime(args.built_at,'%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=timezone.utc)
    else:
        epoch = os.environ.get('SOURCE_DATE_EPOCH')
        if epoch is None:
            try: epoch = git('show','-s','--format=%ct',revision)
            except (OSError,subprocess.SubprocessError) as error:
                raise ValueError('build timestamp unavailable; set SOURCE_DATE_EPOCH or --built-at') from error
        try: timestamp = datetime.fromtimestamp(int(epoch),tz=timezone.utc)
        except (ValueError,OverflowError,OSError) as error: raise ValueError('invalid SOURCE_DATE_EPOCH') from error
    return revision, timestamp.strftime('%Y-%m-%dT%H:%M:%SZ')


def archive_bytes(files, built_at):
    timestamp = datetime.strptime(built_at,'%Y-%m-%dT%H:%M:%SZ')
    # ZIP's DOS timestamp has a narrower range and two-second resolution.
    year = min(2107,max(1980,timestamp.year))
    date = (year,timestamp.month,timestamp.day,timestamp.hour,timestamp.minute,timestamp.second//2*2)
    output = io.BytesIO()
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_STORED,allowZip64=False) as archive:
        for name,raw in files.items():
            entry = zipfile.ZipInfo(name,date)
            entry.create_system = 3
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry,raw)
    return output.getvalue()


def protect_delivery(folder, archive, inputs, public_key, key_id):
    for target in (folder,archive):
        if target.is_symlink() or target.resolve() != target.absolute():
            raise ValueError('release output must not follow symbolic links')
        for source in inputs:
            if target.resolve() == source.resolve() or (source.exists() and target.exists() and target.is_file() and target.samefile(source)):
                raise ValueError('release output would overwrite an input')
    if archive.exists():
        if not archive.is_file(): raise ValueError('existing ZIP output is not a file')
        validate_bundle(bounded(archive,'existing release ZIP',12*1024*1024),public_key,key_id)
    if folder.exists():
        if not folder.is_dir() or not archive.exists(): raise ValueError('existing output directory is not an owned release')
        with zipfile.ZipFile(archive) as previous:
            actual = [path for path in folder.rglob('*') if not path.is_dir() or path.is_symlink()]
            if any(path.is_symlink() for path in folder.rglob('*')):
                raise ValueError('existing release directory contains links')
            if {path.relative_to(folder).as_posix() for path in actual} != set(previous.namelist()):
                raise ValueError('existing release directory contains unknown files')
            for path in actual:
                if path.read_bytes() != previous.read(path.relative_to(folder).as_posix()):
                    raise ValueError('existing release directory was modified')


def replace_artifact(source, target):
    # Windows virus/index scanners can briefly hold a newly-created ZIP.
    for attempt in range(5):
        try:
            os.replace(source,target)
            return
        except PermissionError:
            if attempt == 4: raise
            time.sleep(0.1 * 2**attempt)


@contextmanager
def delivery_stage(parent):
    # Final public assets inherit the output directory ACL, not tempfile's
    # private Windows ACL (which would keep sandbox/browser readers out).
    stage = parent/('.tinyrt-delivery-'+uuid.uuid4().hex)
    stage.mkdir()
    try: yield stage
    finally:
        if stage.resolve().parent != parent.resolve() or stage.is_symlink():
            raise ValueError('unsafe delivery cleanup target')
        # A failed rollback may leave the only copy of the previous directory.
        # Never clean its backup just because the context is unwinding.
        if not (stage/'previous').exists(): shutil.rmtree(stage)


def deliver(files, raw_zip, folder, archive, inputs, public_key, key_id):
    archive.parent.mkdir(parents=True,exist_ok=True)
    protect_delivery(folder,archive,inputs,public_key,key_id)
    with delivery_stage(archive.parent) as stage:
        new_folder = stage/'bundle'; new_folder.mkdir()
        for name,raw in files.items():
            path = new_folder/name; path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(raw)
        new_zip = stage/'bundle.zip'; new_zip.write_bytes(raw_zip)
        backup = stage/'previous'; moved = False
        if folder.exists(): replace_artifact(folder,backup)
        try:
            replace_artifact(new_folder,folder); moved = True
            replace_artifact(new_zip,archive)  # Commit the ZIP last; no partial ZIP becomes visible.
        except OSError:
            try:
                if moved:
                    if folder.resolve().parent != archive.parent.resolve() or folder.is_symlink():
                        raise ValueError('unsafe rollback cleanup target')
                    shutil.rmtree(folder)
                if backup.exists(): replace_artifact(backup,folder)
            except OSError as error:
                raise ValueError('delivery rollback blocked; previous files preserved in '+stage.name+'/previous') from error
            raise
        if backup.exists():
            # Output is already committed. A backup cleanup lock must not turn
            # a successful release into a failure or discard recovery data.
            try: shutil.rmtree(backup)
            except OSError: pass


def make_release(args):
    root = tinyrt.manifest_path(args.app).parent
    report = dict(schema='tinyrt.build-report.v1',steps=[],warnings=[])
    stage = 'inputs'
    snapshots = {}
    def passed(name, result='passed', detail=None):
        entry = dict(name=name,result=result)
        if detail is not None: entry['detail'] = detail
        report['steps'].append(entry)
    try:
        root, app, listing, assets, snapshots, events = load_inputs(args.app,args.preview_ms)
        tinyrt.integer(args.key_id,0,0xffffffff,'key ID')
        if args.development_key:
            key = package.development_key()
        else:
            key_path = Path(args.key).resolve()
            key_raw = key_path.read_bytes(); snapshots[key_path] = key_raw
            key = serialization.load_pem_private_key(key_raw,None)
        if not isinstance(key,ec.EllipticCurvePrivateKey) or not isinstance(key.curve,ec.SECP256R1):
            raise ValueError('signing key must be an ECDSA P-256 private key')
        development = key.public_key().public_numbers() == package.development_key().public_key().public_numbers()
        if development and (args.key_id != 1 or args.channel != 'development' or not re.fullmatch(r'demo\.[a-z0-9._-]+',app['app_id'])):
            raise ValueError('development key requires demo. app ID, key ID 1 and development channel')
        if args.variant == 'wasm-aot' and (not args.development_key or args.key_id != 1):
            raise ValueError('production AOT must use the controlled release_compile.py workflow')
        if args.wamrc and args.variant != 'wasm-aot': raise ValueError('--wamrc requires --variant wasm-aot')
        cc, toolchain = compiler_info(args.cc)
        runner = local_runner(args.runner)
        wamrc = None
        if args.variant == 'wasm-aot':
            import development_aot
            path = Path(args.wamrc or tinyrt.ROOT/'bin/wamrc.exe').resolve()
            if not path.is_file(): raise ValueError('release needs local pinned AOT compiler; pass --wamrc')
            wamrc = development_aot.compiler_path(json.loads(development_aot.PROFILE.read_bytes()),path)
        revision, built_at = build_identity(args)
        version = str(app['version'])
        name = f'{app["app_id"]}-v{version}-{args.variant}'
        output = Path(args.output).absolute() if args.output else root/'release'
        folder, archive = output/name, output/(name+'.zip')
        # Resolve only after checking link components. Refuse outputs inside source trees.
        for path in (output,*output.parents):
            if path.is_symlink(): raise ValueError('release output must not follow directory links')
        output = output.resolve(); folder,archive = output/name,output/(name+'.zip')
        protect_delivery(folder,archive,snapshots,key.public_key(),args.key_id)
        passed(stage)
        stage = 'native-tests'
        test = root/'tests/run_native.py'
        if not test.exists() and root == tinyrt.ROOT/'examples'/root.name:
            test = tinyrt.ROOT/'tests'/root.name/'run_native.py'
        native = 'not-run'
        if test.is_file():
            subprocess.run([sys.executable,str(test),'--cc',str(cc)],cwd=tinyrt.ROOT,check=True,capture_output=True,timeout=300)
            native = 'passed'
        passed(stage,native)
        stage = 'build-wasm'
        build = tinyrt.build(root,cc)
        wasm = Path(build['wasm']); wasm_raw = package.read_bounded(wasm)
        passed(stage)
        with tempfile.TemporaryDirectory(prefix='tinyrt-release-') as temporary:
            stage_root = Path(temporary)
            stage = 'wamr-preview'
            times = {shot['at_ms'] for shot in listing['screenshots'] if 'at_ms' in shot}
            # Run WAMR even for device-only screenshots. Include script end so no input is truncated.
            last_event = max((event['ms'] for event in read_events(snapshots[events])),default=0) if events else 0
            captures = sorted(times | {max(times | {last_event})})
            preview.run(root,events,','.join(map(str,captures)),stage_root/'preview',runner)
            passed(stage)
            stage = 'pack'
            if args.variant == 'wasm-aot':
                import development_aot
                package_path = stage_root/'package.trpkg'
                development_aot.pack_development(root,wasm,package_path,wamrc)
                package_raw = package.read_bounded(package_path)
            else:
                package_raw = package.build_package(wasm_raw,
                    snapshots[tinyrt.local_file(root,app['assets'])] if app.get('assets') else b'',
                    cover=package.encode_cover_png(assets['cover.png']),
                    **{field:app[field] for field in tinyrt.REQUIRED-{'sources'}},key_id=args.key_id,private_key=key)
            passed(stage)
            stage = 'validate-envelope'
            info,parts,metadata,requirements = inspect_package(package_raw,key.public_key(),args.key_id)
            if parts[1] != wasm_raw: raise ValueError('signed Wasm differs from previewed Wasm')
            if parts.get(4) != package.encode_cover_png(assets['cover.png']): raise ValueError('signed cover differs from input')
            passed(stage)
            stage = 'screenshots'
            screenshots = []; shot_files = {}
            for i,shot in enumerate(listing['screenshots'],1):
                path = f'screenshots/{i:02d}.png'
                if 'at_ms' in shot:
                    raw = bounded(stage_root/'preview'/f'frame-{shot["at_ms"]:06d}.png',path,1048576)
                    capture = 'wamr-preview'
                else:
                    raw = snapshots[tinyrt.local_file(root,shot['file'])]; capture = 'device'
                image = png(raw,path,(466,466),1048576)
                encoded = io.BytesIO(); image.save(encoded,format='PNG')
                final = encoded.getvalue(); png(final,path,(466,466),1048576)
                shot_files[path] = final
                extra = dict(caption=shot['caption']) if 'caption' in shot else {}
                screenshots.append(file_record(path,final,width=466,height=466,capture=capture,**extra))
            passed(stage)
        stage = 'manifest'
        command = 'python tools/tinyrt.py release '+('examples/'+root.name if root == tinyrt.ROOT/'examples'/root.name else '<app>')
        command += ' --variant '+args.variant
        if args.development_key: command += ' --development-key'
        elif args.key: command += ' --key <key> --key-id '+str(args.key_id)
        manifest = dict(schema_version=2,app_id=info['app_id'],title=info['title'],version=info['version'],
            variant='wasm-aot' if 2 in parts else 'wasm',requested_channel=args.channel,
            publisher_display=listing['publisher_display'],locale='zh-CN',summary=listing['summary'],
            category=listing['category'],tags=listing['tags'],controls=listing['controls'],
            display=dict(width=466,height=466,shape='round',full_bleed=listing['full_bleed']),
            package=metadata,requirements=requirements,signing='development' if development else 'release',
            verification=dict(native=native,wamr='passed',device='passed' if listing['device_verified'] else 'not-run'),
            rights=listing['rights'],cover=file_record('cover.png',assets['cover.png'],width=210,height=210),
            readme=file_record('README.md',assets['README.md']),licenses=file_record('LICENSES.md',assets['LICENSES.md']),
            screenshots=screenshots,release_notes=listing['release_notes'],changelog=file_record('CHANGELOG.md',assets['CHANGELOG.md']),
            build=dict(sdk_revision=revision,toolchain=toolchain,built_at=built_at,command=command,report=None))
        check_schema(manifest,'release-v2.schema.json')
        passed(stage,detail='package_version='+version)
        for path,raw in snapshots.items():
            if path.read_bytes() != raw: raise ValueError('release input changed during build: '+path.name)
        if package.read_bounded(wasm) != wasm_raw: raise ValueError('Wasm changed during preview')
        stage = 'archive'
        def assemble():
            report_raw = json_bytes(report)
            manifest['build']['report'] = file_record('build/report.json',report_raw)
            files = {'release.json':json_bytes(manifest),'package.trpkg':package_raw,**assets,**shot_files,'build/report.json':report_raw}
            # Explicit order is independent of caller dictionaries.
            names = ['release.json','package.trpkg','cover.png',*DOCS,*shot_files,'build/report.json']
            files = {name:files[name] for name in names}
            return files, archive_bytes(files,built_at)
        files, raw_zip = assemble()
        passed(stage)
        stage = 'self-check'
        validate_bundle(raw_zip,key.public_key(),args.key_id)
        passed(stage)
        files, raw_zip = assemble()
        validate_bundle(raw_zip,key.public_key(),args.key_id)
        stage = 'delivery'
        deliver(files,raw_zip,folder,archive,snapshots,key.public_key(),args.key_id)
        return dict(zip=str(archive),directory=str(folder),sha256=digest(raw_zip),
                    package_version=info['version'],variant=manifest['variant'])
    except (OSError,ValueError,TypeError,UnicodeError,subprocess.SubprocessError):
        # Fixed relative report path and stage name; subprocess stderr may contain keys/paths.
        report['steps'].append(dict(name=stage,result='failed'))
        target = root/'build/release-report.json'
        try:
            if not target.is_symlink() and target.parent.resolve() == target.parent.absolute():
                # Inputs may include the report name, even when input validation failed.
                app = tinyrt.load_manifest(args.app)
                inputs = [tinyrt.manifest_path(args.app),*snapshots]
                inputs += [tinyrt.local_file(root,name) for name in app['sources']]
                inputs += [tinyrt.local_file(root,app[name]) for name in ('assets','cover') if app.get(name)]
                if args.key: inputs.append(Path(args.key).resolve())
                for source in inputs:
                    if target.resolve() == source.resolve() or (target.exists() and source.exists() and target.samefile(source)):
                        raise ValueError('failure report must not overwrite an input')
                if target.exists():
                    check_report(bounded(target,'release-report.json',262144))
                tinyrt.write_atomic(target,json_bytes(report))
        except (OSError,ValueError,TypeError,UnicodeError): pass
        raise
