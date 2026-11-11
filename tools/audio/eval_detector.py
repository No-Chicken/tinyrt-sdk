"""Replay the guest's C detectors against 16 kHz mono WAVs or L1 feature CSVs."""
import argparse
from collections import Counter, defaultdict
import csv
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SDK = HERE.parents[1]
FIELDS = ('seq', 't_ms', 'rms_db_q8', 'floor_db_q8', 'peak', 'zcr',
          'band0_q15', 'band1_q15', 'band2_q15', 'band3_q15', 'flatness_q15',
          'pitch_hz_q4', 'pitch_conf_q15', 'flags')
FRAME = struct.Struct('<IIhh10H')
LABELS = ('blow', 'clap', 'scream', 'knock', 'speech', 'hum', 'other')
TARGET = {'blow': 'blow', 'clap': 'clap', 'scream': 'scream', 'pitch': 'hum'}


def rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def native(args, tmp):
    cc = args.cc or os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('gcc')
    if not cc:
        raise ValueError('supply --cc or TINYRT_CC (Zig, Clang or GCC)')
    params = json.loads(args.params.read_bytes()) if args.params else {}
    if not isinstance(params, dict):
        raise ValueError('blow parameters must be a JSON object')
    limits = {'gate.enter_ms': (10, 10000), 'gate.exit_ms': (10, 10000),
              'gate.enter_q15': (1, 32767), 'gate.exit_q15': (1, 32767),
              'spike_peak': (1, 32767), 'spike_frames': (1, 100),
              'spike_decay_ratio': (2, 100), 'playback_db_q8': (0, 42 * 256)}
    for group in ('level', 'unvoiced', 'low_band', 'flatness'):
        for key in ('lo', 'hi', 'weight'):
            limits[group + '.' + key] = (0, 100 if key == 'weight' else 32767)
    lines = ['static void apply_params(ta_blow_cfg_t *c) {', '    (void)c;']
    for key, value in params.items():
        if key not in limits or type(value) is not int or not limits[key][0] <= value <= limits[key][1]:
            raise ValueError(f'invalid blow parameter {key}={value!r}')
        lines.append(f'    c->{key} = {value};')
    for g in ('level', 'unvoiced', 'low_band', 'flatness'):
        lines.append(f'    if (c->{g}.lo >= c->{g}.hi) exit(2);')
    lines += ['    if (c->level.weight + c->unvoiced.weight + c->low_band.weight + c->flatness.weight != 100'
              ' || c->gate.enter_q15 <= c->gate.exit_q15) exit(2);', '}']
    (tmp/'blow_params.h').write_text('\n'.join(lines), encoding='ascii')
    exe = tmp/('replay.exe' if os.name == 'nt' else 'replay')
    cmd = [cc] + (['cc'] if Path(cc).stem == 'zig' else [])
    subprocess.run(cmd + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(SDK/'include'),
                          '-I', str(tmp), str(HERE/'replay.c'), '-o', str(exe)], check=True)
    return exe


def frames(source, tmp, extractor, dest, window=None):
    # EEDB zero-fills lost blocks in the WAV, while its CSV preserves the true
    # sequence numbers and hardware flags. Never feed filler into the extractor.
    initial_gap = 0
    metadata = source.with_suffix('.json')
    if metadata.is_file():
        sidecar = json.loads(metadata.read_bytes())
        if isinstance(sidecar, dict) and sidecar.get('rate_hz') == 16000:
            gaps = sidecar.get('gaps', [])
            if source.suffix.lower() == '.wav' and (gaps or sidecar.get('overrun_count', 0)):
                raise ValueError(f'{source}: lost audio blocks; use the corresponding device feature CSV')
            if gaps and gaps[0][0] == 0:
                initial_gap = gaps[0][1]
    if source.suffix.lower() == '.wav':
        csv_path = tmp/'features.csv'
        subprocess.run([extractor, source, csv_path], check=True)
        source = csv_path
    first_seq = last_delta = None
    times = []
    with dest.open('wb') as output:
        for row in rows(source):
            values = [int(row[key]) for key in FIELDS]
            seq = values[0]
            if first_seq is None:
                first_seq = seq
            delta = (seq - first_seq) & 0xffffffff
            if last_delta is not None and delta <= last_delta:
                raise ValueError(f'{source}: sequence must advance within a recording')
            end_sample = initial_gap + (delta + 1) * 160
            last_delta = delta
            if window is not None and not window[0] < end_sample <= window[1]:
                continue
            output.write(FRAME.pack(*values))
            times.append(end_sample)
    if not times:
        raise ValueError(f'{source}: no 10 ms frames')
    return times


def window_samples(start_ms, duration_ms):
    if start_ms is None:
        return None
    if start_ms < 0 or start_ms % 10:
        raise ValueError('window start must be a nonnegative multiple of 10 ms')
    return start_ms * 16, (start_ms + duration_ms) * 16


def calibration(path):
    if not path:
        return []
    c = json.loads(path.read_bytes())
    floor, ref = c['floor_db_q8'], c['ref_db_q8']
    if (c['ok'] != 1 or type(floor) is not int or type(ref) is not int
            or not -25600 <= floor < ref <= 0 or ref - floor < 1536):
        raise ValueError(f'invalid calibration: {path}')
    return [str(floor), str(ref)]


def replay(args, tmp, exe, source, cal):
    raw = tmp/'replay.bin'
    samples = frames(source, tmp, args.extractor, raw)
    trace = subprocess.check_output([exe, args.detector, raw, *cal], text=True)
    values = [tuple(map(int, line.split(','))) for line in trace.splitlines()]
    if len(values) != len(samples):
        raise ValueError('native replay output is incomplete')
    return list(zip(samples, values))


def intervals(trace):
    result = []
    start = None
    for sample, (active, valid, *_) in trace:
        if start is None and active:
            start = sample
        elif start is not None and not active:
            result.append((start, sample if valid else None))
            start = None
    if start is not None:
        result.append((start, None))
    return result


def annotations(path, end):
    last = 0
    result = []
    for row in rows(path):
        a, b = int(row['start_sample']), int(row['end_sample'])
        label, uncertain = row['label'], row['uncertain']
        if not last <= a < b <= end or label not in LABELS or uncertain not in ('0', '1'):
            raise ValueError(f'{path}: invalid or overlapping annotation {row}')
        result.append((a, b, label, uncertain == '1'))
        last = b
    return result


class Metrics:
    def __init__(self):
        self.hits = self.targets = self.uncertain = self.uncertain_starts = 0
        self.long = self.long_extra = self.invalid = self.total_frames = 0
        self.fp, self.samples = Counter(), Counter()
        self.onset, self.offset, self.cases = [], [], []

    def add(self, name, trace, labels, target):
        starts = intervals(trace)
        self.total_frames += len(trace)
        self.invalid += sum(not values[1] for _, values in trace)
        segments = []
        previous = 0
        for a, b, label, uncertain in labels:
            if previous < a:
                segments.append((previous, a, 'background', False))
            segments.append((a, b, label, uncertain))
            previous = b
        end = trace[-1][0]
        if previous < end:
            segments.append((previous, end, 'background', False))
        cursor = 0
        for a, b, label, uncertain in segments:
            first = cursor
            # end_sample is exclusive. The triggering frame's last sample is
            # end_sample - 1, so a transition at b belongs to [a, b).
            while cursor < len(starts) and starts[cursor][0] <= b:
                cursor += 1
            events = starts[first:cursor]
            if uncertain:
                self.uncertain += 1
                self.uncertain_starts += len(events)
                continue
            if label != target:
                self.samples[label] += b - a
                self.fp[label] += len(events)
                self.cases.extend((name, start, label) for start, _ in events)
                continue
            self.targets += 1
            if b - a >= 16000:
                self.long += 1
                self.long_extra += max(0, len(events) - 1)
            if events:
                self.hits += 1
                self.onset.append((events[0][0] - a) / 16)
                if events[0][1] is not None:
                    self.offset.append((events[0][1] - b) / 16)
                self.cases.extend((name, start, 'split') for start, _ in events[1:])
            else:
                self.cases.append((name, a, 'missed ' + label))

    def markdown(self, who):
        def latency(xs):
            if not xs:
                return 'n/a'
            xs = sorted(xs)
            return f'{xs[math.ceil(len(xs)*.5)-1]:g}/{xs[math.ceil(len(xs)*.95)-1]:g} ms (n={len(xs)})'
        lines = [f'## {who}', '', f'Hits / labelled targets: {self.hits}/{self.targets}',
                 f'Onset P50/P95: {latency(self.onset)}', f'Release P50/P95: {latency(self.offset)}',
                 f'Extra starts in long actions / long labels: {self.long_extra}/{self.long}',
                 f'Starts in uncertain intervals / uncertain labels: {self.uncertain_starts}/{self.uncertain}',
                 f'Invalid outputs / frames: {self.invalid}/{self.total_frames}', '',
                 '| Interference | False starts | Seconds | False starts/hour |', '|---|---:|---:|---:|']
        for label, samples in sorted(self.samples.items()):
            if samples:
                seconds = samples / 16000
                lines.append(f'| {label} | {self.fp[label]} | {seconds:.3f} | {3600*self.fp[label]/seconds:.3f} |')
        return '\n'.join(lines)


def evaluate(args, tmp, exe):
    dataset = rows(args.sessions)
    by_person = defaultdict(set)
    for row in dataset:
        by_person[row['player']].add(row['split'])
    if any(len(splits) != 1 for splits in by_person.values()):
        raise ValueError('a player occurs in multiple splits')
    selected = [r for r in dataset if r['split'] == args.split]
    if not selected:
        raise ValueError('no recordings in selected split')
    groups, overall = defaultdict(Metrics), Metrics()
    for row in selected:
        source = args.sessions.parent/row['recording']
        cal = calibration(args.sessions.parent/row['calibration']) if row.get('calibration') else []
        trace = replay(args, tmp, exe, source, cal)
        labels = annotations(source.with_suffix('.labels.csv'), trace[-1][0])
        for m in (groups[row['player']], overall):
            m.add(row['recording'], trace, labels, TARGET[args.detector])
    parts = [f'# {args.detector} · {args.split}', overall.markdown('All players')]
    parts.extend(m.markdown(who) for who, m in sorted(groups.items()))
    parts.append('## Clips for review\n\n' + '\n'.join(f'- {name} @ {t/16000:.3f} s: {reason}'
                 for name, t, reason in overall.cases))
    return '\n\n'.join(parts)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='action', required=True)
    cal = subs.add_parser('calibrate', help='compute a player calibration from two clean windows')
    cal.add_argument('quiet', type=Path)
    cal.add_argument('reference', type=Path)
    cal.add_argument('--quiet-start-ms', type=int, help='use exactly the next 1000 ms of quiet')
    cal.add_argument('--reference-start-ms', type=int, help='use exactly the next 300 ms of the annotated action')
    evaluate_cmd = subs.add_parser('evaluate', help='score annotated sessions by player')
    evaluate_cmd.add_argument('sessions', type=Path)
    evaluate_cmd.add_argument('--split', required=True, choices=('train', 'validation', 'test'))
    trace_cmd = subs.add_parser('trace', help='print per-frame detector output')
    trace_cmd.add_argument('recording', type=Path)
    trace_cmd.add_argument('--calibration', type=Path)
    for cmd in (evaluate_cmd, trace_cmd):
        cmd.add_argument('--detector', choices=tuple(TARGET), default='blow')
    for cmd in (cal, evaluate_cmd, trace_cmd):
        cmd.add_argument('--cc')
        cmd.add_argument('--extractor', default='tinyrt-mic-features')
        cmd.add_argument('--params', type=Path, help='blow configuration overrides in JSON')
        cmd.add_argument('-o', '--output', type=Path, help='write a new output file (default stdout)')
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix='tinyrt-audio-') as temp:
            tmp = Path(temp)
            exe = native(args, tmp)
            if args.action == 'evaluate':
                output = evaluate(args, tmp, exe)
            elif args.action == 'trace':
                trace = replay(args, tmp, exe, args.recording, calibration(args.calibration))
                lines = ['end_sample,active,valid,score_q15,strength_q15,pitch_hz_q4']
                lines.extend(','.join(map(str, (sample, *values))) for sample, values in trace)
                output = '\n'.join(lines)
            else:
                quiet, ref = tmp/'quiet.bin', tmp/'ref.bin'
                frames(args.quiet, tmp, args.extractor, quiet, window_samples(args.quiet_start_ms, 1000))
                frames(args.reference, tmp, args.extractor, ref, window_samples(args.reference_start_ms, 300))
                output = subprocess.check_output([exe, 'calibrate', quiet, ref], text=True).strip()
        if args.output:
            with args.output.open('x', encoding='utf-8', newline='\n') as stream:
                stream.write(output + '\n')
        else:
            print(output)
    except (OSError, ValueError, KeyError, struct.error, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')


if __name__ == '__main__':
    main()
