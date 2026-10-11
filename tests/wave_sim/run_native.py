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
for variant,defines in [('wave',[]),('wave-metrics',['-DWAVE_METRICS=1']),
                        ('wave-s31',['-DWAVE_IMU_MOUNT_DEG=0']),
                        ('wave-400',['-DWAVE_N=400']),
                        ('wave-coarse',['-DWAVE_GRID=20','-DWAVE_N=130']),
                        ('wave-coarse-metrics',['-DWAVE_GRID=20','-DWAVE_N=130','-DWAVE_METRICS=1']),
                        ('wave-coarse-s31',['-DWAVE_GRID=20','-DWAVE_N=130','-DWAVE_IMU_MOUNT_DEG=0'])]:
    executable=out/f'test-{variant}.exe'
    subprocess.run(command+['-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror']+defines+[
        '-I',str(root/'include'),str(root/'tests/wave_sim/test_wave.c'),
        str(root/'examples/wave_sim/wave_physics.c'),'-o',str(executable)],check=True,env=env)
    subprocess.run([str(executable)],check=True,timeout=120)

# Exercise the 3D model at the normal and high-density limits, including
# cooperative execution and the actual application's projection/input code.
for count in (600, 900, 1200):
    for test in ('test_balls', 'test_balls_app'):
        executable=out/f'{test}-{count}.exe'
        subprocess.run(command+['-std=c11','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
            f'-DWAVE_BALL_COUNT={count}','-I',str(root/'include'),
            str(root/f'tests/wave_sim/{test}.c'),
            str(root/'examples/wave_sim/wave_balls.c'),'-o',str(executable)],check=True,env=env)
        subprocess.run([str(executable)],check=True,timeout=120)
