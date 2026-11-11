"""Run the actual guest detectors with controlled ten-millisecond feature frames."""
import csv
import json
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/audio'))
import eval_detector as ev


class AudioReplay(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = os.getenv('TINYRT_CC') or shutil.which('clang') or shutil.which('gcc')
        if not cc:
            raise unittest.SkipTest('set TINYRT_CC to compile the native detector')
        temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(temporary.cleanup)
        cls.tmp = Path(temporary.name)
        cls.exe = ev.native(SimpleNamespace(cc=cc, params=None), cls.tmp)

    def features(self, name, sequence):
        path = self.tmp/(name+'.csv')
        with path.open('w', newline='') as stream:
            rows = csv.DictWriter(stream, fieldnames=ev.FIELDS)
            rows.writeheader()
            for i, changes in enumerate(sequence):
                frame = dict.fromkeys(ev.FIELDS, 0)
                frame.update(seq=i, t_ms=i*10+10, floor_db_q8=-60*256, rms_db_q8=-60*256)
                frame.update(changes)
                rows.writerow(frame)
        return path

    def trace(self, detector, sequence, cal=()):
        args = SimpleNamespace(detector=detector, extractor=None)
        return ev.replay(args, self.tmp, self.exe, self.features(detector, sequence), cal)

    def test_reference_and_calibrated_strength(self):
        quiet, ref = self.tmp/'quiet.bin', self.tmp/'ref.bin'
        ev.frames(self.features('quiet', [{}]*100), self.tmp, None, quiet)
        ev.frames(self.features('ref', [{}]*25 + [dict(rms_db_q8=-42*256)]*30), self.tmp, None, ref)
        cal = json.loads(subprocess.check_output([self.exe, 'calibrate', quiet, ref]))
        self.assertEqual(cal, dict(ok=1, floor_db_q8=-60*256, ref_db_q8=-42*256))
        self.assertNotEqual(subprocess.run([self.exe, 'calibrate', quiet, quiet], capture_output=True).returncode, 0)
        light = dict(rms_db_q8=-42*256, band0_q15=31000, flatness_q15=20000)
        strong = dict(light, rms_db_q8=-24*256)
        trace = self.trace('blow', [light]*12 + [strong]*60 + [{}]*8,
                           ('-15360', '-10752'))
        self.assertEqual([r[1][0] for r in trace[:12]], [0]*9 + [1]*3)
        self.assertEqual(trace[11][1][3], 0)
        self.assertGreater(trace[71][1][3], 32600)
        self.assertEqual(trace[-2][1][0], 1)
        self.assertEqual(trace[-1][1][0], 0)

    def test_flags_gap_peak_and_playback(self):
        blown = dict(rms_db_q8=-48*256, band0_q15=30000, flatness_q15=20000, peak=3000)
        trace = self.trace('blow', [dict(blown, flags=1)]*15 + [blown]*10 +
                           [dict(blown, flags=4), blown, dict(blown, seq=28)])
        self.assertFalse(any(row[1][0] for row in trace[:15]))
        self.assertEqual(trace[24][1][0], 1)
        self.assertEqual(trace[25][1][:2], (0, 0))
        self.assertEqual(trace[27][1][:2], (0, 0))
        trace = self.trace('blow', [dict(blown, peak=30000)]*7 + [dict(blown, peak=1000)]*8)
        self.assertFalse(any(row[1][0] for row in trace))

    def test_clap_scream_and_pitch(self):
        peak = dict(peak=27000, flatness_q15=21000)
        clap = self.trace('clap', [peak, {}, peak, {}] + [{}]*18 + [peak, {}])
        self.assertEqual([i for i, (_, values) in enumerate(clap) if values[0]], [1, 23])
        shout = dict(rms_db_q8=-21*256, pitch_conf_q15=27000, band2_q15=9300)
        scream = self.trace('scream', [shout]*15 + [{}]*12)
        self.assertEqual([i for i, (_, v) in enumerate(scream) if v[0]], list(range(14, 26)))
        note = dict(pitch_hz_q4=7040, pitch_conf_q15=30000)
        pitch = self.trace('pitch', [note]*4 + [dict(note, pitch_hz_q4=3520)] + [{}]*3)
        self.assertEqual([v[4] for _, v in pitch], [7040]*7 + [0])

    def test_one_onset_per_annotation_and_uncertain_exclusion(self):
        metrics = ev.Metrics()
        labels = [(0, 16000, 'blow', False), (16000, 24000, 'clap', False),
                  (24000, 32000, 'blow', True)]
        states = [(1600, 1), (5000, 0), (6000, 1), (15000, 0), (18000, 1),
                  (22000, 0), (26000, 1), (32000, 0)]
        metrics.add('session', [(t, (a, 1)) for t, a in states], labels, 'blow')
        self.assertEqual((metrics.hits, metrics.targets, metrics.long_extra), (1, 1, 1))
        self.assertEqual(metrics.fp['clap'], 1)
        self.assertEqual((metrics.uncertain_starts, metrics.uncertain), (1, 1))

    def test_eedb_recording_gap_preserves_sample_alignment(self):
        source = self.features('dropped', [dict(seq=200), dict(seq=203)])
        source.with_suffix('.json').write_text(json.dumps(dict(rate_hz=16000,
            overrun_count=4, gaps=[[0, 320], [480, 800]])))
        self.assertEqual(ev.frames(source, self.tmp, None, self.tmp/'dropped.bin'), [480, 960])
        with self.assertRaisesRegex(ValueError, 'lost audio blocks'):
            ev.frames(source.with_suffix('.wav'), self.tmp, None, self.tmp/'invalid.bin')
