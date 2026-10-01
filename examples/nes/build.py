"""Private NES N1 build; no ESP-IDF, WASI, network, or global installation."""
import argparse, importlib.util, json, os, subprocess
from make_rom import generate
from pathlib import Path
HERE=Path(__file__).resolve().parent
SDK=HERE.parents[1]
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cc',required=True)
    p.add_argument('--native',action='store_true')
    p.add_argument('--out',type=Path,default=HERE/'build')
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);generate(a.out)
    spec=importlib.util.spec_from_file_location('adapt_cpu',SDK/'third_party/nes/adapt_cpu.py');adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter);cpu=adapter.generate(a.out)
    zig=Path(a.cc).stem.lower()=='zig'
    cmd=[a.cc]+(['cc'] if zig else [])
    if not a.native:cmd+=['-target','wasm32-freestanding'] if zig else ['--target=wasm32-unknown-unknown']
    cmd+=['-std=c11','-O2','-fno-builtin','-ffunction-sections','-fdata-sections','-I',str(HERE),'-I',str(a.out),'-I',str(SDK/'third_party/nes/upstream/inc')]
    sources=[HERE/'engine.c',HERE/'port.c',cpu,SDK/'third_party/nes/upstream/src/nes_ppu.c']
    if a.native:
        sources+=[SDK/'tests/nes/native.c'];target=a.out/'nes-native.exe'
    else:
        sources+=[HERE/'main.c'];target=a.out/'nes.wasm'
        cmd+=['-nostdlib','-I',str(HERE/'freestanding'),'-I',str(SDK/'include'),'-Wl,--no-entry','-Wl,--export=tinyrt_init','-Wl,--export=tinyrt_event','-Wl,--export=tinyrt_render','-Wl,-z,stack-size=16384','-Wl,--initial-memory=0x40000','-Wl,--max-memory=0x100000','-Wl,--strip-all','-mno-bulk-memory']
    env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(a.out/'zig-cache'))
    subprocess.run(cmd+[str(s) for s in sources]+['-o',str(target)],env=env,check=True)
    print(json.dumps({'output':str(target),'bytes':target.stat().st_size}))
if __name__=='__main__':main()
