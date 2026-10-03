"""Release behavior: strict inputs, authenticated ZIPs and failure isolation."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import package
import tinyrt

# A function import, encoded independently of the release parser.
IMPORT = b'\x01\x06tinyrt\x0adraw_clear\x00\x00'
WASM = b'\0asm\1\0\0\0\x02' + bytes([len(IMPORT)]) + IMPORT


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        import release_bundle
        self.release = release_bundle
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.app = Path(self.temp.name) / 'app'
        self.app.mkdir()
        self.manifest = dict(app_id='demo.test', title='Test', version=1,
                             abi_version=1, permissions=1, memory_pages=2,
                             budget=100000, sources=['main.c'], cover='cover.png')
        self.listing = dict(summary='A test', category='game', tags=[],
                            controls=['Tap'], publisher_display='Tester',
                            release_notes='Initial release', full_bleed=True,
                            rights=dict(status='unverified', note='See LICENSES.md'),
                            device_verified=False, display_version='0.0.1',
                            screenshots=[dict(at_ms=32, caption='Start')])
        self.write_json('app.json', self.manifest)
        self.write_json('listing.json', self.listing)
        (self.app / 'main.c').write_bytes(b'/* source */\n')
        for name in ('README.md', 'CHANGELOG.md', 'LICENSES.md'):
            (self.app / name).write_bytes(b'# Test\n')
        Image.new('RGB', (210, 210), '#101418').save(self.app / 'cover.png')
        self.args = argparse.Namespace(app=self.app, cc='compiler', runner=self.app/'runner',
                                       variant='wasm', output=None, channel='development',
                                       development_key=True, key=None, key_id=1, wamrc=None,
                                       preview_ms=60000, sdk_revision='a'*40, built_at='2026-10-03T00:00:00Z')
        self.args.runner.write_bytes(b'local runner')

    def write_json(self, name, value):
        (self.app / name).write_bytes((json.dumps(value) + '\n').encode())

    def fake_build(self, *args, **kwargs):
        destination = self.app/'build/demo.test.wasm'
        destination.parent.mkdir(exist_ok=True)
        destination.write_bytes(WASM)
        return dict(wasm=str(destination))

    def fake_preview(self, app, events, frames, output, runner, *args):
        output.mkdir(parents=True, exist_ok=True)
        for ms in map(int, frames.split(',')):
            Image.new('RGBA', (466, 466), '#abcdef').save(output/f'frame-{ms:06d}.png')
        return dict(images=[])

    def run_release(self, preview=None):
        with patch.object(tinyrt, 'build', side_effect=self.fake_build), \
             patch('preview.run', side_effect=preview or self.fake_preview), \
             patch.object(self.release, 'compiler_info', return_value=('compiler', 'zig 0.13.0')):
            return self.release.make_release(self.args)

    def test_reproducible_zip_authentication_and_exact_layout(self):
        result = self.run_release()
        archive = Path(result['zip'])
        before = archive.read_bytes()
        self.run_release()
        self.assertEqual(before, archive.read_bytes())
        manifest = self.release.validate_bundle(before, package.development_key().public_key(), 1)
        self.assertEqual(manifest['schema_version'], 2)
        self.assertEqual(manifest['version'], 1)
        self.assertEqual(manifest['requirements']['imports'], ['draw_clear'])
        self.assertEqual(manifest['verification'], dict(native='not-run', wamr='passed', device='not-run'))
        with zipfile.ZipFile(io.BytesIO(before)) as z:
            self.assertEqual(z.namelist(), ['release.json', 'package.trpkg', 'cover.png',
                                           'README.md', 'CHANGELOG.md', 'LICENSES.md',
                                           'screenshots/01.png', 'build/report.json'])
            for name in ('release.json', 'build/report.json'):
                self.assertNotIn(str(self.app).encode(), z.read(name))
        self.assertIn('v0.0.1-wasm.zip', archive.name)

    def test_required_negative_inputs_fail_before_build(self):
        cases = [('missing changelog', 'CHANGELOG.md'), ('unknown field', 'unknown'),
                 ('late screenshot', 'late'), ('wrong device size', 'device'),
                 ('wrong cover size', 'cover'), ('BOM', 'bom'), ('CRLF', 'crlf'),
                 ('development stable', 'channel')]
        for label, kind in cases:
            with self.subTest(label=label):
                changelog = self.app/'CHANGELOG.md'
                changelog.write_bytes(b'# Test\n')
                Image.new('RGB', (210, 210)).save(self.app/'cover.png')
                self.write_json('listing.json', self.listing)
                self.args.channel = 'development'
                if kind == 'CHANGELOG.md': changelog.unlink()
                elif kind == 'unknown': self.write_json('listing.json', dict(self.listing, unknown=True))
                elif kind == 'late': self.write_json('listing.json', dict(self.listing, screenshots=[dict(at_ms=60001)]))
                elif kind == 'device':
                    Image.new('RGB', (100, 100)).save(self.app/'device.png')
                    self.write_json('listing.json', dict(self.listing, screenshots=[dict(file='device.png')]))
                elif kind == 'cover': Image.new('RGB', (100, 100)).save(self.app/'cover.png')
                elif kind == 'bom': changelog.write_bytes(b'\xef\xbb\xbf# Test\n')
                elif kind == 'crlf': changelog.write_bytes(b'# Test\r\n')
                else: self.args.channel = 'stable'
                with patch.object(tinyrt, 'build') as build, self.assertRaises(ValueError):
                    self.run_release()
                build.assert_not_called()
                self.assertFalse(list(self.app.glob('release/*.zip')))

    def test_preview_failure_leaves_no_zip_and_records_failure(self):
        def fail(*args): raise ValueError('preview failed')
        with self.assertRaisesRegex(ValueError, 'preview failed'):
            self.run_release(fail)
        self.assertFalse(list(self.app.glob('release/*.zip')))
        report = json.loads((self.app/'build/release-report.json').read_bytes())
        self.assertIn(dict(name='wamr-preview', result='failed'), report['steps'])

    def test_duplicate_listing_keys_and_ambiguous_capture_rejected(self):
        (self.app/'listing.json').write_bytes(b'{"summary":"x","summary":"y"}')
        with self.assertRaisesRegex(ValueError, 'duplicate'):
            self.run_release()
        self.write_json('listing.json', dict(self.listing, screenshots=[dict(at_ms=32, file='cover.png')]))
        with self.assertRaises(ValueError): self.run_release()

    def test_tampered_file_and_metadata_rejected(self):
        archive = Path(self.run_release()['zip']).read_bytes()
        for changed in ('README.md', 'release.json'):
            with self.subTest(changed=changed):
                data = io.BytesIO()
                with zipfile.ZipFile(io.BytesIO(archive)) as source, zipfile.ZipFile(data, 'w') as dest:
                    for item in source.infolist():
                        raw = source.read(item)
                        if item.filename == changed:
                            if changed == 'release.json':
                                value = json.loads(raw); value['version'] = 9
                                raw = (json.dumps(value, indent=2)+'\n').encode()
                            else: raw += b'Tampered\n'
                        dest.writestr(item, raw)
                with self.assertRaises(ValueError):
                    self.release.validate_bundle(data.getvalue(), package.development_key().public_key(), 1)

    def test_failed_rebuild_preserves_existing_deliverable(self):
        archive = Path(self.run_release()['zip'])
        before = archive.read_bytes()
        def fail(*args): raise ValueError('preview failed')
        with self.assertRaises(ValueError): self.run_release(fail)
        self.assertEqual(archive.read_bytes(), before)

    def test_local_tools_required_no_automatic_download(self):
        self.args.runner = self.app/'missing-runner'
        with patch('urllib.request.urlopen') as network, self.assertRaises(ValueError):
            self.run_release()
        network.assert_not_called()

    def test_unknown_output_is_not_overwritten(self):
        archive = Path(self.run_release()['zip'])
        archive.write_bytes(b'user file')
        with self.assertRaises(ValueError): self.run_release()
        self.assertEqual(archive.read_bytes(), b'user file')

    def test_failure_report_cannot_overwrite_resource_input(self):
        target = self.app/'build/release-report.json'
        target.parent.mkdir()
        original = b'{"schema":"tinyrt.build-report.v1","steps":[],"warnings":[],"resource":42}'
        target.write_bytes(original)
        self.write_json('app.json', dict(self.manifest, assets='build/release-report.json'))
        self.args.runner = self.app/'missing-runner'
        with self.assertRaises(ValueError): self.run_release()
        self.assertEqual(target.read_bytes(), original)

    def test_zip_entry_order_is_checked(self):
        archive = Path(self.run_release()['zip']).read_bytes()
        output = io.BytesIO()
        with zipfile.ZipFile(io.BytesIO(archive)) as source, zipfile.ZipFile(output,'w') as dest:
            for item in reversed(source.infolist()): dest.writestr(item,source.read(item))
        with self.assertRaises(ValueError):
            self.release.validate_bundle(output.getvalue(),package.development_key().public_key(),1)

    def test_events_keep_existing_run_encoding_and_timing_rules(self):
        (self.app/'events.json').write_bytes(b'\xef\xbb\xbf[\r\n{"ms":33,"kind":"press","x":1,"y":1}\r\n]\r\n')
        self.run_release()

    def test_native_test_failure_stops_before_build(self):
        tests = self.app/'tests'; tests.mkdir()
        (tests/'run_native.py').write_bytes(b'raise SystemExit(1)\n')
        with patch.object(tinyrt,'build') as build, self.assertRaises(Exception):
            self.run_release()
        build.assert_not_called()
        self.assertFalse(list(self.app.glob('release/*.zip')))

    def test_zig_version_uses_its_supported_subcommand(self):
        import subprocess
        compiler = self.app/'zig.exe'; compiler.write_bytes(b'local compiler')
        def version(command, **kwargs):
            if command[1:] != ['version']:
                raise subprocess.CalledProcessError(1,command,stderr='unknown command')
            return subprocess.CompletedProcess(command,0,stdout='0.13.0\n')
        with patch('subprocess.run',side_effect=version):
            self.assertEqual(self.release.compiler_info(str(compiler))[1],'zig 0.13.0')

    def test_transient_windows_zip_lock_is_retried(self):
        archive = Path(self.run_release()['zip'])
        original_replace = os.replace
        blocked = []
        def replace(source, target):
            if Path(target) == archive and not blocked:
                blocked.append(True)
                raise PermissionError('temporary scanner lock')
            return original_replace(source,target)
        with patch('os.replace',side_effect=replace): self.run_release()
        self.assertTrue(blocked)

    def test_failed_rollback_preserves_recovery_backup(self):
        result = self.run_release()
        archive, folder = Path(result['zip']), Path(result['directory'])
        previous = (folder/'package.trpkg').read_bytes()
        original_replace = os.replace
        def replace(source, target):
            if Path(target) == archive or Path(source).name == 'previous':
                raise PermissionError('persistent lock')
            return original_replace(source,target)
        with patch('os.replace',side_effect=replace), patch('time.sleep'), self.assertRaises(ValueError):
            self.run_release()
        backups = list(archive.parent.glob('.tinyrt-delivery-*/previous/package.trpkg'))
        self.assertEqual(len(backups),1)
        self.assertEqual(backups[0].read_bytes(),previous)
        self.assertTrue(archive.is_file())


if __name__ == '__main__': unittest.main()
