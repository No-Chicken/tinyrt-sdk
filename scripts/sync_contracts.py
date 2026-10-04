#!/usr/bin/env python3
"""Generate/check SDK contract snapshots from an explicit pinned TinyRT core.

Normal application builds use the shipped snapshot and do not need the core.
--revision pending only prepares an explicitly unpinned development snapshot;
--check and ordinary sync reject pending until the real core commit is supplied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
FILES=('guest-v1.h','guest-gfx-v1.h','abi-v1.json','wire-v1.json')


def core_git(core,*args):
    result=subprocess.run(['git','-C',str(core),*args],capture_output=True)
    if result.returncode:
        detail=result.stderr.decode('utf-8',errors='replace').strip()
        raise ValueError(f'cannot verify core revision/committed contracts: {detail}')
    return result.stdout


def read_core_revision(core):
    """Refuse a parent repository; callers read contracts from the pinned Git blobs."""
    core=Path(core).resolve()
    repository=Path(core_git(core,'rev-parse','--show-toplevel').decode().strip()).resolve()
    if repository!=core:
        raise ValueError('--core must identify the core repository root, not a directory in a parent repository')
    return core_git(core,'rev-parse','HEAD').decode('ascii').strip()


def read_committed_contracts(core,revision):
    """Check actual bytes, regardless of assume-unchanged/skip-worktree flags."""
    contents={}
    for name in FILES:
        relative='contracts/'+name
        entry=core_git(core,'ls-tree','-z',revision,'--',relative)
        if not entry or entry.count(b'\0')!=1:
            raise ValueError(f'missing committed contract: {relative}')
        metadata,path=entry.rstrip(b'\0').split(b'\t',1)
        mode,kind,blob=metadata.split()
        if path.decode('utf-8')!=relative or mode not in (b'100644',b'100755') or kind!=b'blob':
            raise ValueError(f'committed contract must be a regular file, not a symlink: {relative}')
        data=core_git(core,'cat-file','blob',blob.decode('ascii'))
        source=core/relative
        if source.is_symlink() or not source.is_file() or not source.resolve().is_relative_to(core):
            raise ValueError(f'committed contract requires a regular source file within core: {relative}')
        if source.read_bytes()!=data:
            raise ValueError(f'source contract differs from committed bytes: {relative}')
        contents[name]=data
    return contents


def replace_output(path,data):
    """Replace a name without modifying any other hardlinks to the old inode."""
    path.parent.mkdir(parents=True,exist_ok=True)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent,prefix='.'+path.name+'.',delete=False) as output:
            temporary=Path(output.name)
            output.write(data)
        os.replace(temporary,path)
    finally:
        if temporary is not None and temporary.exists():temporary.unlink()


def read_lock(path):
    lock=json.loads(path.read_text(encoding='utf-8'))
    if not isinstance(lock,dict) or set(lock)!={'format_version','core_revision','files'} or lock['format_version']!=1:
        raise ValueError('invalid contract source lock')
    if not isinstance(lock['files'],dict) or set(lock['files'])!=set(FILES):
        raise ValueError('contract lock must record exactly four file hashes')
    for value in lock['files'].values():
        if not isinstance(value,str) or re.fullmatch('[0-9a-f]{64}',value) is None:
            raise ValueError('invalid contract source hash')
    return lock


def sync_contracts(core,sdk=ROOT,*,revision=None,check=False):
    core=Path(core).resolve();sdk=Path(sdk).resolve()
    if core==sdk:raise ValueError('core and SDK must use distinct roots')
    lock_path=sdk/'contracts/source.json'
    old=None
    if check and revision is not None:raise ValueError('--check reads the lock; do not supply --revision')
    if revision is None:
        if not lock_path.is_file():raise ValueError('missing source lock: provide --revision <core commit>')
        old=read_lock(lock_path);revision=old['core_revision']
        if revision=='pending':raise ValueError('core revision is pending; supply the actual committed revision before --check or normal sync')
    if revision!='pending' and (not isinstance(revision,str) or re.fullmatch('[0-9a-f]{40}',revision) is None):
        raise ValueError('revision must be the full 40-character lowercase Git SHA, or explicit pending bootstrap')
    if revision!='pending':
        actual=read_core_revision(core)
        if actual!=revision:raise ValueError(f'core revision mismatch: expected {revision}, found {actual}')
        contents=read_committed_contracts(core,revision)
    else:
        contents={name:(core/'contracts'/name).read_bytes() for name in FILES}
    for name,data in contents.items():
        data.decode('utf-8',errors='strict')
        if b'\r' in data:raise ValueError(f'{name} must use canonical LF bytes before synchronizing')
        if name.endswith('.json'):json.loads(data)
    hashes={name:hashlib.sha256(data).hexdigest() for name,data in contents.items()}
    if old is not None and old['files']!=hashes:
        raise ValueError('source contract hash does not match the pinned lock')
    lock={'format_version':1,'core_revision':revision,'files':hashes}
    outputs={sdk/'contracts'/name:data for name,data in contents.items()}
    outputs[sdk/'include/tinyrt.h']=contents['guest-v1.h']
    outputs[sdk/'include/tinyrt_gfx.h']=contents['guest-gfx-v1.h']
    if check:
        for path,data in outputs.items():
            if not path.is_file() or path.read_bytes()!=data:
                raise ValueError(f'generated contract differs: {path.relative_to(sdk)}')
    else:
        outputs[lock_path]=(json.dumps(lock,indent=2,sort_keys=True)+'\n').encode('utf-8')
        # Resolve every output before writes: generated paths must stay in SDK.
        for path in outputs:
            if path.is_symlink() or not path.resolve().is_relative_to(sdk):
                raise ValueError('generated contract output must stay within SDK and must not be a symlink')
            if path.exists() and not path.is_file():
                raise ValueError('generated contract output must be a regular file')
        for path,data in outputs.items():
            replace_output(path,data)
    return {'core_revision':revision,'files':hashes,'checked':check,'pinned':revision!='pending'}


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',required=True,type=Path,help='explicit core repository root')
    parser.add_argument('--revision',help='pin a new full core Git SHA; pending is a bootstrap-only escape hatch')
    parser.add_argument('--check',action='store_true',help='verify revision, hashes and generated copies without writing')
    args=parser.parse_args(argv)
    try:
        result=sync_contracts(args.core,revision=args.revision,check=args.check)
        if not result['pinned']:print('WARNING: contract revision pending; pinned --check will fail until finalized.',file=sys.stderr)
        print(json.dumps(result,sort_keys=True));return 0
    except (OSError,ValueError,TypeError,UnicodeError) as error:
        parser.exit(2,f'sync_contracts: {error}\n')

if __name__=='__main__':raise SystemExit(main())
