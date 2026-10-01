"""Drive the production Snake guest through the public ABI, with a test host."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--cc', default=os.environ.get('TINYRT_CC'))
args = parser.parse_args()
cc = args.cc or shutil.which('clang') or shutil.which('zig')
if not cc:
    parser.error('pass --cc clang-or-zig')
cc = str(Path(cc).resolve()) if Path(cc).exists() else cc
out = root / 'build/tests'
out.mkdir(parents=True, exist_ok=True)
exe = out / ('test_snake.exe' if os.name == 'nt' else 'test_snake')
command = [cc] + (['cc'] if Path(cc).stem.lower() == 'zig' else [])
env = os.environ.copy()
env.setdefault('ZIG_GLOBAL_CACHE_DIR', str(out / 'zig-cache'))
subprocess.run(command + ['-std=c11', '-Wall', '-Wextra', '-Werror', '-O2',
    '-I', str(root / 'include'), str(root / 'examples/snake/main.c'),
    str(root / 'tests/snake/test_snake.c'), '-o', str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True)
