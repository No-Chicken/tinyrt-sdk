"""Create a separate axis-check manifest from the committed production source."""
import json
from pathlib import Path
import shutil
APP=Path(__file__).resolve().parents[1]
out=APP/'build/axis-app';out.mkdir(parents=True,exist_ok=True)
for name in ('axis_debug.c','cover.png'):shutil.copy2(APP/name,out/name)
manifest=dict(app_id='demo.wave-axis',title='Wave Axis Check',version=1,
    abi_version=1,permissions=11,memory_pages=2,budget=100000,
    sources=['axis_debug.c'],cover='cover.png')
(out/'app.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(out)
out=APP/'build/perf-app';out.mkdir(parents=True,exist_ok=True)
for name in ('main.c','wave_physics.c','wave_physics.h','cover.png'):shutil.copy2(APP/name,out/name)
defines=dict(json.loads((APP/'app.json').read_text(encoding='utf-8')).get('defines',{}))
defines['WAVE_METRICS']=1
manifest=dict(app_id='demo.wave-perf',title='Wave Performance Check',version=1,
    abi_version=1,permissions=11,memory_pages=4,budget=100000,
    sources=['main.c','wave_physics.c'],cover='cover.png',defines=defines)
(out/'app.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(out)
