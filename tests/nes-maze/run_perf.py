"""Native dispatch/profile regression for the real NES maze CPU/PPU."""
import argparse, importlib.util, os, subprocess
from pathlib import Path

HERE=Path(__file__).resolve().parent
SDK=HERE.parents[1]
EXAMPLE=SDK/'examples/nes-maze'

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cc',required=True,type=Path)
    args=parser.parse_args()
    out=HERE/'build/perf';out.mkdir(parents=True,exist_ok=True)
    load('maze_rom',EXAMPLE/'make_rom.py').generate(out)
    cpu=load('adapt_cpu',SDK/'third_party/nes/adapt_cpu.py').generate(out)
    command=[str(args.cc)]+(['cc'] if args.cc.stem.lower()=='zig' else [])
    command+=['-std=c11','-O2','-fno-builtin','-ffunction-sections','-fdata-sections',
              '-I',str(EXAMPLE),'-I',str(out),
              '-I',str(SDK/'third_party/nes/upstream/inc')]
    env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(out/'zig-cache'))
    for name in ('test_perf','test_idle'):
        target=out/(name+'.exe')
        flags=['-DNES_TEST_MODE=1','-DNES_TEST_PROFILE=1'] if name=='test_perf' else []
        sources=[HERE/(name+'.c'),EXAMPLE.parent/'nes/port.c',cpu,SDK/'third_party/nes/upstream/src/nes_ppu.c']
        subprocess.run(command+flags+[str(p) for p in sources]+['-o',str(target)],env=env,check=True)
        subprocess.run([str(target)],check=True)
    traces=[]
    for cache in (0,1):
        target=out/f'test_cache_{cache}.exe'
        sources=[HERE/'test_cache_trace.c',EXAMPLE.parent/'nes/port.c',cpu,SDK/'third_party/nes/upstream/src/nes_ppu.c']
        subprocess.run(command+[f'-DMAZE_CACHE_BACKGROUND={cache}']+[str(p) for p in sources]+['-o',str(target)],env=env,check=True)
        traces.append(subprocess.run([str(target)],capture_output=True,check=True).stdout)
    if traces[0]!=traces[1]:raise AssertionError('Cached PPU output/CPU state differs from uncached interpreter')
    print(f'CACHE EQUIVALENCE PASS frames={len(traces[0].splitlines())}',flush=True)

if __name__=='__main__':main()
