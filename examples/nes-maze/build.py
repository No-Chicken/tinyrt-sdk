"""Original NES Maze build; no ESP-IDF, WASI, network, or global installation."""
import argparse, importlib.util, json, os, subprocess, sys
from make_rom import generate
from pathlib import Path
HERE=Path(__file__).resolve().parent
SDK=HERE.parents[1]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cc',required=True)
    p.add_argument('--native',action='store_true')
    p.add_argument('--out',type=Path,default=HERE/'build')
    signing=p.add_mutually_exclusive_group()
    signing.add_argument('--development-key',action='store_true',help='sign using the PUBLIC demonstration key')
    signing.add_argument('--private-key',type=Path)
    p.add_argument('--key-id',type=int,default=1)
    a=p.parse_args()
    if a.native and (a.development_key or a.private_key):p.error('signing requires a Wasm build')
    a.out.mkdir(parents=True,exist_ok=True);generate(a.out)
    spec=importlib.util.spec_from_file_location('adapt_cpu',SDK/'third_party/nes/adapt_cpu.py');adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter);cpu=adapter.generate(a.out)
    zig=Path(a.cc).stem.lower()=='zig'
    cmd=[a.cc]+(['cc'] if zig else [])
    if not a.native:cmd+=['-target','wasm32-freestanding'] if zig else ['--target=wasm32-unknown-unknown']
    cmd+=['-std=c11','-O2','-fno-builtin','-ffunction-sections','-fdata-sections','-I',str(HERE),'-I',str(a.out),'-I',str(SDK/'third_party/nes/upstream/inc')]
    sources=[HERE/'engine.c',HERE.parent/'nes/port.c',cpu,SDK/'third_party/nes/upstream/src/nes_ppu.c']
    if a.native:
        sources+=[SDK/'tests/nes-maze/native.c'];target=a.out/'nes-maze-native.exe'
    else:
        sources+=[HERE/'main.c'];target=a.out/'nes-maze.wasm'
        cmd+=['-nostdlib','-I',str(HERE.parent/'nes/freestanding'),'-I',str(SDK/'include'),'-Wl,--no-entry','-Wl,--export=tinyrt_init','-Wl,--export=tinyrt_event','-Wl,--export=tinyrt_render','-Wl,-z,stack-size=16384','-Wl,--initial-memory=0x40000','-Wl,--max-memory=0x100000','-Wl,--strip-all','-mno-bulk-memory']
    env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(a.out/'zig-cache'))
    subprocess.run(cmd+[str(s) for s in sources]+['-o',str(target)],env=env,check=True)
    print(json.dumps({'output':str(target),'bytes':target.stat().st_size}),flush=True)
    if a.development_key or a.private_key:
        package=a.out/'nes-maze.trpkg'
        command=[sys.executable,str(SDK/'tools/tinyrt.py'),'pack',str(HERE),'--wasm',str(target),'--output',str(package),'--key-id',str(a.key_id)]
        command+=['--development-key'] if a.development_key else ['--key',str(a.private_key)]
        subprocess.run(command,check=True)
if __name__=='__main__':main()
