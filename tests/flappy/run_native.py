"""Compile and exercise the production Sky Hop guest; no device access."""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--cc', required=True)
args = parser.parse_args()
output = root / 'build/tests'
output.mkdir(parents=True, exist_ok=True)
exe = output / 'test_flappy.exe'
cc = str(Path(args.cc).resolve())
env = os.environ.copy()
env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(output / 'zig-cache'))
command = [cc] + (['cc'] if Path(cc).stem == 'zig' else [])
subprocess.run(command + ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
    '-I', str(root / 'include'), str(root / 'tests/flappy/test_flappy.c'),
    '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe), str(root / "examples/flappy/resources.bin")], check=True)
