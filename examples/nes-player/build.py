"""Build the resource-ROM player with pinned freestanding NES sources."""
import argparse, importlib.util, json, os, subprocess, sys
from pathlib import Path
from make_rom import generate
HERE=Path(__file__).resolve().parent
SDK=HERE.parents[1]
def main():
    cli=argparse.ArgumentParser();cli.add_argument('--cc',required=True)
    cli.add_argument('--rom',type=Path,help='local, legally held mapper-0 ROM; never committed')
    cli.add_argument('--aot',action='store_true');cli.add_argument('--wamrc',type=Path)
    cli.add_argument('--no-idle-batch',action='store_true');cli.add_argument('--development-key',action='store_true');cli.add_argument('--native',action='store_true')
    args=cli.parse_args();out=HERE/'build';out.mkdir(parents=True,exist_ok=True)
    if args.native and (args.aot or args.development_key):cli.error('native tests do not make installable packages')
    if args.rom:
        rom=args.rom.read_bytes()
        if not 16<=len(rom)<=40976:raise ValueError('mapper-0 ROM must be 16..40976 bytes')
        (out/'game.nes').write_bytes(rom)
    else:print(json.dumps(generate(out)))
    spec=importlib.util.spec_from_file_location('adapt_cpu',SDK/'third_party/nes/adapt_cpu.py')
    adapter=importlib.util.module_from_spec(spec);spec.loader.exec_module(adapter);cpu=adapter.generate(out)
    cc=[args.cc]+(['cc'] if Path(args.cc).stem.lower()=='zig' else [])
    if not args.native:cc+=['-target','wasm32-freestanding'] if Path(args.cc).stem.lower()=='zig' else ['--target=wasm32-unknown-unknown']
    cc+=['-std=c11','-O2','-fno-builtin','-I',str(HERE),'-I',str(HERE.parent/'nes'),
         '-I',str(SDK/'include'),'-I',str(SDK/'third_party/nes/upstream/inc')]
    if args.no_idle_batch:cc+=['-DNES_IDLE_BATCH=0']
    if args.native:cc+=['-DNES_NATIVE_TRACE=1']
    sources=[HERE/'engine.c',HERE.parent/'nes/port.c',cpu,SDK/'third_party/nes/upstream/src/nes_ppu.c']
    if args.native:
        sources+=[SDK/'tests/nes-player/native.c'];target=out/'nes-player-native.exe'
    else:
        sources+=[HERE/'main.c'];target=out/'demo.nes-scroll.wasm'
        cc+=['-nostdlib','-I',str(HERE.parent/'nes/freestanding'),'-Wl,--no-entry',
             '-Wl,--export=tinyrt_init','-Wl,--export=tinyrt_event','-Wl,--export=tinyrt_render',
             '-Wl,-z,stack-size=16384','-Wl,--initial-memory=0x40000','-Wl,--max-memory=0x100000',
             '-Wl,--strip-all','-mno-bulk-memory']
    env=os.environ.copy();env.setdefault('ZIG_GLOBAL_CACHE_DIR',str(out/'zig-cache'))
    subprocess.run(cc+[str(s) for s in sources]+['-o',str(target)],env=env,check=True)
    print(json.dumps({'output':str(target),'bytes':target.stat().st_size}))
    if args.aot or args.development_key:
        command=[sys.executable,str(SDK/'tools/tinyrt.py'),'pack',str(HERE),'--development-key']
        if args.aot:command+=['--aot']
        if args.wamrc:command+=['--wamrc',str(args.wamrc)]
        subprocess.run(command,check=True)
if __name__=='__main__':main()
