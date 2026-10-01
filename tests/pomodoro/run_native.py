"""Compile the production guest natively, then drive its public ABI with a clock."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--cc',default=os.environ.get('TINYRT_CC'))
p.add_argument('--fast',action='store_true',help='test the explicitly marked 25s/5s build')
p.add_argument('--round',action='store_true',help='check circular geometry and touch boundaries')
a=p.parse_args()
assert (root/'examples/pomodoro/main.c').is_file(), 'Pomodoro guest implementation is missing'
cc=a.cc or shutil.which('clang') or shutil.which('zig')
if not cc:p.error('pass --cc clang-or-zig')
cc=str(Path(cc).resolve()) if Path(cc).exists() else cc
out=root/'build/tests';out.mkdir(parents=True,exist_ok=True)
exe=out/(('test_round' if a.round else 'test_pomodoro_fast' if a.fast else 'test_pomodoro')+('.exe' if os.name=='nt' else ''))
command=[cc]+(['cc'] if Path(cc).stem.lower()=='zig' else [])
if a.fast:command+=['-DPOMODORO_FAST=1']
env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(out/'zig-cache'))
subprocess.run(command+['-std=c11','-Wall','-Wextra','-Werror','-O2','-I',str(root/'include'),
    str(root/'examples/pomodoro/main.c'),str(root/'tests/pomodoro'/('test_round.c' if a.round else 'test_pomodoro.c')),'-o',str(exe)],check=True,env=env)
subprocess.run([str(exe)]+(['--fast'] if a.fast else []),check=True)
