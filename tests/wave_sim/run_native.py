"""Compile production Wave code with assertions, no device access."""
import argparse
import os
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--cc',required=True)
args=parser.parse_args()
out=root/'build/tests/wave_sim';out.mkdir(parents=True,exist_ok=True)
env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(out/'zig-cache'))
cc=str(Path(args.cc).resolve())
command=[cc]+(['cc'] if Path(cc).stem=='zig' else [])
for variant,defines in [('wave',[]),('wave-metrics',['-DWAVE_METRICS=1'])]:
    executable=out/f'test-{variant}.exe'
    subprocess.run(command+['-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror']+defines+[
        '-I',str(root/'include'),str(root/'tests/wave_sim/test_wave.c'),
        str(root/'examples/wave_sim/wave_physics.c'),'-o',str(executable)],check=True,env=env)
    subprocess.run([str(executable)],check=True,timeout=120)
