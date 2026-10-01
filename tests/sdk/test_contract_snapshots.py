"""SDK snapshots are checked against an explicit pinned core revision and hashes."""
import importlib.util
import json
import os
import subprocess
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
REVISION='1234567890abcdef1234567890abcdef12345678'

class ContractSnapshotTests(unittest.TestCase):
    def setUp(self):
        path=ROOT/'scripts/sync_contracts.py'
        self.assertTrue(path.is_file(),'explicit core contract synchronizer is missing')
        spec=importlib.util.spec_from_file_location('sdk_sync_contracts_test',path)
        self.sync=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.sync)
        temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup)
        self.core=Path(temp.name)/'core';self.sdk=Path(temp.name)/'sdk'
        (self.core/'contracts').mkdir(parents=True);self.sdk.mkdir()
        (self.core/'contracts/guest-v1.h').write_bytes(b'#ifndef GUEST_H\n#define GUEST_H\n#endif\n')
        for name in ('abi-v1.json','wire-v1.json'):
            (self.core/'contracts'/name).write_bytes(b'{"version": 1}\n')
        self.run_git('init','--quiet')
        self.run_git('config','user.name','Contract Test')
        self.run_git('config','user.email','contract-test@example.invalid')
        self.run_git('config','core.autocrlf','false')
        self.run_git('config','commit.gpgsign','false')
        self.run_git('config','core.hooksPath',str(self.core/'disabled-hooks'))
        self.run_git('add','contracts')
        self.run_git('commit','--quiet','-m','Contract fixture')
        self.revision=self.run_git('rev-parse','HEAD')

    def run_git(self,*args):
        result=subprocess.run(['git','-C',str(self.core),*args],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        return result.stdout.strip()

    def apply(self,revision=REVISION,check=False):
        if revision==REVISION:revision=self.revision
        return self.sync.sync_contracts(self.core,self.sdk,revision=revision,check=check)

    def test_sync_copies_three_contracts_and_generated_guest_header(self):
        result=self.apply()
        self.assertEqual(result['core_revision'],self.revision)
        for name in ('guest-v1.h','abi-v1.json','wire-v1.json'):
            self.assertEqual((self.sdk/'contracts'/name).read_bytes(),(self.core/'contracts'/name).read_bytes())
        self.assertEqual((self.sdk/'include/tinyrt.h').read_bytes(),(self.core/'contracts/guest-v1.h').read_bytes())
        self.apply(revision=None,check=True)

    def test_modified_generated_header_fails_check(self):
        self.apply();(self.sdk/'include/tinyrt.h').write_bytes(b'changed\n')
        with self.assertRaisesRegex(ValueError,'tinyrt.h'):self.apply(revision=None,check=True)

    def test_modified_lock_hash_fails_check(self):
        self.apply();path=self.sdk/'contracts/source.json';lock=json.loads(path.read_text())
        lock['files']['guest-v1.h']='0'*64;path.write_text(json.dumps(lock))
        with self.assertRaisesRegex(ValueError,'hash'):self.apply(revision=None,check=True)

    def test_modified_contract_snapshot_fails_check(self):
        self.apply();(self.sdk/'contracts/abi-v1.json').write_bytes(b'{"version": 2}\n')
        with self.assertRaisesRegex(ValueError,'abi-v1.json'):self.apply(revision=None,check=True)

    def test_core_revision_mismatch_is_rejected(self):
        with patch.object(self.sync,'read_core_revision',return_value='f'*40):
            with self.assertRaisesRegex(ValueError,'revision'):self.apply()
        self.assertFalse((self.sdk/'include/tinyrt.h').exists())

    def test_pending_bootstrap_is_explicit_and_never_passes_pinned_check(self):
        self.apply(revision='pending')
        with self.assertRaisesRegex(ValueError,'pending'):self.apply(revision=None,check=True)
        with self.assertRaisesRegex(ValueError,'pending'):self.apply(revision=None)

    def test_pin_can_replace_pending_snapshot(self):
        self.apply(revision='pending');self.apply()
        self.apply(revision=None,check=True)
        lock=json.loads((self.sdk/'contracts/source.json').read_text())
        self.assertEqual(lock['core_revision'],self.revision)

    def test_changed_core_bytes_with_same_lock_hash_fail_check(self):
        self.apply();(self.core/'contracts/wire-v1.json').write_bytes(b'{"version": 2}\n')
        with self.assertRaisesRegex(ValueError,'hash|committed'):self.apply(revision=None,check=True)

    def test_noncanonical_revision_and_crlf_are_rejected(self):
        with self.assertRaisesRegex(ValueError,'revision'):self.apply(revision='short')
        (self.core/'contracts/guest-v1.h').write_bytes(b'#define X 1\r\n')
        with self.assertRaisesRegex(ValueError,'LF'):self.apply(revision='pending')

    def test_core_path_cannot_accidentally_use_parent_repository_revision(self):
        with self.assertRaisesRegex(ValueError,'repository root'):
            self.sync.read_core_revision(self.core/'contracts')


    def test_assume_unchanged_cannot_pin_uncommitted_bytes(self):
        self.run_git('update-index','--assume-unchanged','contracts/guest-v1.h')
        (self.core/'contracts/guest-v1.h').write_bytes(b'#define UNCOMMITTED 2\n')
        self.run_git('diff','--quiet','HEAD')
        with self.assertRaisesRegex(ValueError,'committed'):self.apply()
        self.assertFalse((self.sdk/'include/tinyrt.h').exists())

    def test_skip_worktree_cannot_pin_uncommitted_bytes(self):
        self.run_git('update-index','--skip-worktree','contracts/wire-v1.json')
        (self.core/'contracts/wire-v1.json').write_bytes(b'{"version": 2}\n')
        self.run_git('diff','--quiet','HEAD')
        with self.assertRaisesRegex(ValueError,'committed'):self.apply()
        self.assertFalse((self.sdk/'contracts/source.json').exists())

    def test_check_rejects_hidden_source_modification(self):
        self.apply()
        self.run_git('update-index','--assume-unchanged','contracts/guest-v1.h')
        (self.core/'contracts/guest-v1.h').write_bytes(b'#define UNCOMMITTED 2\n')
        # Also alter the lock to match the uncommitted bytes: revision must remain authoritative.
        import hashlib
        path=self.sdk/'contracts/source.json';lock=json.loads(path.read_text())
        lock['files']['guest-v1.h']=hashlib.sha256((self.core/'contracts/guest-v1.h').read_bytes()).hexdigest()
        path.write_text(json.dumps(lock))
        (self.sdk/'contracts/guest-v1.h').write_bytes((self.core/'contracts/guest-v1.h').read_bytes())
        (self.sdk/'include/tinyrt.h').write_bytes((self.core/'contracts/guest-v1.h').read_bytes())
        with self.assertRaisesRegex(ValueError,'committed'):self.apply(revision=None,check=True)

    def test_git_symlink_contract_mode_is_rejected_without_os_symlink_privilege(self):
        blob=self.run_git('rev-parse','HEAD:contracts/guest-v1.h')
        self.run_git('update-index','--cacheinfo',f'120000,{blob},contracts/guest-v1.h')
        self.run_git('commit','--quiet','-m','Symlink contract fixture')
        self.revision=self.run_git('rev-parse','HEAD')
        self.run_git('update-index','--assume-unchanged','contracts/guest-v1.h')
        with self.assertRaisesRegex(ValueError,'regular|symlink'):self.apply()

    def test_output_hardlinks_do_not_modify_external_files(self):
        self.apply()
        paths=['include/tinyrt.h','contracts/source.json',*[f'contracts/{name}' for name in self.sync.FILES]]
        for number,relative in enumerate(paths):
            with self.subTest(output=relative):
                outside=self.sdk.parent/f'outside-{number}.txt';outside.write_bytes(b'KEEP EXTERNAL CONTENT\n')
                target=self.sdk/relative;target.unlink();os.link(outside,target)
                self.apply()
                self.assertEqual(outside.read_bytes(),b'KEEP EXTERNAL CONTENT\n')
                self.assertFalse(os.path.samefile(outside,target))
                self.apply(revision=None,check=True)

if __name__=='__main__':unittest.main()
