"""A defined stop callback is exported; existing three-callback apps still build."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

def leb(data,position):
    value=shift=0
    while True:
        byte=data[position];position+=1;value|=(byte&127)<<shift
        if byte<128:return value,position
        shift+=7

def exports(data):
    position=8
    while position<len(data):
        kind=data[position];size,start=leb(data,position+1);position=start+size
        if kind!=7:continue
        count,start=leb(data,start);names=[]
        for _ in range(count):
            length,start=leb(data,start);names.append(data[start:start+length].decode())
            start+=length+1;_,start=leb(data,start)
        return names
    return []

class OptionalStopTests(unittest.TestCase):
    def test_defined_stop_is_exported_and_legacy_app_stays_valid(self):
        cc=os.environ.get('TINYRT_CC') or shutil.which('clang') or shutil.which('zig')
        if not cc:self.skipTest('set TINYRT_CC for real Wasm build')
        with tempfile.TemporaryDirectory() as folder:
            app=Path(folder)
            (app/'app.json').write_text(json.dumps(dict(app_id='demo.stop',title='Stop',version=1,
                abi_version=1,permissions=15,memory_pages=2,budget=100000,sources=['main.c'])))
            base='#include "tinyrt.h"\nint32_t tinyrt_init(int32_t w,int32_t h){return 0;}\nint32_t tinyrt_event(int32_t k,int32_t x,int32_t y,int32_t a){return 0;}\nint32_t tinyrt_render(void){return draw_clear(0);}\n'
            for stop in (False,True):
                with self.subTest(stop=stop):
                    (app/'main.c').write_text(base+('int32_t tinyrt_stop(void){return kv_set(0,123);}\n' if stop else ''))
                    result=subprocess.run([sys.executable,str(ROOT/'tools/tinyrt.py'),'build',str(app),'--cc',cc],capture_output=True,text=True)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    names=exports((app/'build/demo.stop.wasm').read_bytes())
                    self.assertEqual('tinyrt_stop' in names,stop)

if __name__=='__main__':unittest.main()
