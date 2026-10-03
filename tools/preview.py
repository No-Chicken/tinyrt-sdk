"""Run real WAMR callbacks and rasterize their draw commands for desktop review."""
import hashlib
import io
import json
import os
import platform
from pathlib import Path
import subprocess
import urllib.request

import tinyrt


def runner_path(override=None):
    if override:return Path(override).resolve()
    if os.environ.get('TINYRT_RUNNER'):return Path(os.environ['TINYRT_RUNNER']).resolve()
    bundled=tinyrt.ROOT/'bin/tinyrt-run.exe'
    pin_path=Path(__file__).parent/'toolchains/desktop-windows-x64.json'
    if not pin_path.is_file():
        if bundled.is_file():return bundled
        raise ValueError('desktop runtime is missing; pass --runner or use the SDK release with bin/tinyrt-run.exe')
    pin=json.loads(pin_path.read_text(encoding='utf-8'))
    if platform.system()!='Windows' or platform.machine().lower() not in ('amd64','x86_64'):
        raise ValueError('automatic desktop runtime selection supports Windows x64; pass --runner for a local build')
    target=bundled if bundled.is_file() else Path.home()/'.cache/tinyrt'/pin['sha256']/'tinyrt-run.exe'
    if not target.exists():
        with urllib.request.urlopen(pin['url'],timeout=60) as response:data=response.read(16*1024*1024+1)
        if len(data)!=pin['size'] or hashlib.sha256(data).hexdigest()!=pin['sha256']:raise ValueError('desktop runtime download size or hash mismatch')
        tinyrt.write_atomic(target,data)
    if target.stat().st_size!=pin['size'] or hashlib.sha256(target.read_bytes()).hexdigest()!=pin['sha256']:raise ValueError('desktop runtime size or hash mismatch')
    return target


def color(value):return (value>>16 & 255,value>>8 & 255,value & 255)


def floor_scale(image,width,height):
    """Match Host source=floor(destination*source_size/destination_size)."""
    from PIL import Image
    sw,sh=image.size;source=image.tobytes()
    columns=[x*sw//width for x in range(width)]
    rows={}
    for y in {y*sh//height for y in range(height)}:
        start=y*sw*3
        rows[y]=b''.join(source[start+x*3:start+x*3+3] for x in columns)
    return Image.frombytes('RGB',(width,height),b''.join(rows[y*sh//height] for y in range(height)))


def protect_artifact(path,inputs,kind):
    """Only update our own preview artifacts; never follow output file links."""
    requested=Path(path)
    if requested.is_symlink():raise ValueError('preview output must not be a symbolic link')
    path=requested.resolve()
    for item in inputs:
        if item is None:continue
        item=Path(item).resolve()
        if path==item or (path.exists() and item.exists() and path.samefile(item)):
            raise ValueError('preview output must not overwrite an input')
    if path.exists():
        if not path.is_file():raise ValueError('preview output must be a regular artifact file')
        try:
            if kind=='png':
                from PIL import Image
                with Image.open(path) as image:
                    owned=image.format=='PNG' and image.info.get('tinyrt_preview')=='1'
                    image.verify()
            else:
                if path.stat().st_size>16*1024*1024:raise ValueError('preview report is too large')
                value=json.loads(path.read_text(encoding='utf-8'))
                owned=isinstance(value,dict) and value.get('producer')=='tinyrt-preview' and value.get('schema')==1
        except (OSError,ValueError,UnicodeError) as error:
            raise ValueError('existing preview output is not a valid owned artifact; choose a new directory') from error
        if not owned:raise ValueError('existing preview output is not owned by this tool; choose a new directory')
    return path


def raster(frame):
    from PIL import Image,ImageDraw,ImageFont
    canvas=Image.new('RGB',(466,466));draw=ImageDraw.Draw(canvas)
    for command in frame['frame']:
        c=command;kind=c['kind'];x,y,w,h=(c[k] for k in ('x','y','w','h'));rgb=color(c['rgb'])
        if kind==1:draw.rectangle((0,0,465,465),fill=rgb)
        elif kind in (2,4):
            if w>0 and h>0:
                if kind==4:draw.rounded_rectangle((x,y,x+w-1,y+h-1),radius=c['radius'],fill=rgb)
                else:draw.rectangle((x,y,x+w-1,y+h-1),fill=rgb)
        elif kind in (7,8):
            sw,sh=(c['source_w'],c['source_h']) if kind==8 else (w,h)
            raw=bytes.fromhex(frame['pixels_hex'])
            if len(raw)!=sw*sh*2:raise ValueError('runtime pixel payload does not match command')
            image=Image.frombytes('RGB',(sw,sh),raw,'raw','BGR;16',0,1)
            if kind==8:image=floor_scale(image,w,h)
            canvas.paste(image,(x,y))
        elif kind==5:
            r=c['radius'];draw.arc((x-r,y-r,x+r,y+r),c['start_angle'],c['end_angle'],fill=rgb,width=c['thickness'])
        elif kind in (3,6):
            px=c['font_px'] if kind==6 else 24
            fonts=[Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts/seguiemj.ttf',Path('DejaVuSans.ttf')]
            font=None
            for candidate in fonts:
                try:font=ImageFont.truetype(str(candidate),px);break
                except OSError:pass
            if font is None:font=ImageFont.load_default(size=px)
            text=c['text'];tx=x
            if kind==6:
                length=draw.textlength(text,font=font)
                if c['align']==1:tx+=(w-length)/2
                elif c['align']==2:tx+=w-length
                box=Image.new('RGB',(w,h),rgb)
                # Crop the actual background so text-box clipping is preserved.
                box=canvas.crop((x,y,x+w,y+h));ImageDraw.Draw(box).text((tx-x,0),text,font=font,fill=rgb,anchor='lt');canvas.paste(box,(x,y))
            else:draw.text((tx,y),text,font=font,fill=rgb,anchor='lt')
    mask=Image.new('L',(466,466));ImageDraw.Draw(mask).ellipse((0,0,465,465),fill=255)
    canvas.putalpha(mask);return canvas


def run(app,events=None,frames='0,330',output=None,runner=None,step_ms=None):
    manifest=tinyrt.manifest_path(app);m=tinyrt.load_manifest(manifest)
    wasm=manifest.parent/'build'/(m['app_id']+'.wasm')
    if not wasm.is_file():raise ValueError('build the app before running the preview')
    if step_ms is not None:tinyrt.integer(step_ms,1,1000,'step ms')
    captures=sorted(set(int(value) for value in frames.split(',')))
    if not captures or captures[0]<0 or captures[-1]>60000 or len(captures)>120:raise ValueError('capture times must be 0..60000 ms, at most 120')
    scripted=json.loads(Path(events).read_text(encoding='utf-8-sig')) if events else []
    if not isinstance(scripted,list) or len(scripted)>2000:raise ValueError('events must be a list with at most 2000 entries')
    points={ms:[] for ms in captures}
    for event in scripted:
        if not isinstance(event,dict):raise ValueError('event must be an object')
        expected={'ms','kind','x','y','arg'} if event.get('kind')=='motion' else {'ms','kind','x','y'}
        if set(event)!=expected:raise ValueError('event has missing or unexpected fields')
        ms=tinyrt.integer(event['ms'],0,captures[-1],'event time')
        if event['kind']=='motion':
            values=[tinyrt.integer(event[name],-16000,16000,name) for name in ('x','y','arg')]
            points.setdefault(ms,[]).append('motion '+' '.join(map(str,values)));continue
        if event['kind'] in ('key_press','key_release'):
            x=tinyrt.integer(event['x'],1,1,'key identity')
            pressed=1 if event['kind']=='key_press' else 0
            y=tinyrt.integer(event['y'],pressed,pressed,'key state')
            points.setdefault(ms,[]).append(f'key {x} {y}');continue
        kind={'press':3,'move':4,'release':1,'cancel':5}.get(event['kind'])
        if kind is None:raise ValueError('unknown pointer event')
        x=tinyrt.integer(event['x'],0,465,'x');y=tinyrt.integer(event['y'],0,465,'y')
        points.setdefault(ms,[]).append(f'pointer {kind} {x} {y}')
    if step_ms is not None:
        for ms in range(step_ms,captures[-1]+1,step_ms):points.setdefault(ms,[]).insert(0,f'tick {ms}')
    lines=[]
    for ms,commands in sorted(points.items()):
        lines += [f'{"advance" if step_ms is None else "time"} {ms}',*commands]
        if ms in captures:lines.append('capture')
    runtime=runner_path(runner)
    inputs=[manifest,wasm,runtime,events,*[tinyrt.local_file(manifest.parent,name) for name in m['sources']]]
    asset=tinyrt.local_file(manifest.parent,m['assets']) if m.get('assets') else None
    if asset:inputs.append(asset)
    destination=Path(output or manifest.parent/'build/preview').resolve()
    paths={ms:protect_artifact(destination/f'frame-{ms:06d}.png',inputs,'png') for ms in captures}
    report_path=protect_artifact(destination/'report.json',inputs,'report')
    command=[str(runtime),str(wasm),'--pages',str(m['memory_pages']),
             '--permissions',str(m['permissions']),'--budget',str(m['budget'])]
    if asset:command+=['--assets',str(asset)]
    result=subprocess.run(command,input='\n'.join(lines)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=120)
    records=[json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
    errors=[item for item in records if item['status']]
    if result.returncode or errors:raise ValueError('desktop runtime rejected app: '+json.dumps(errors or result.stderr))
    actual_captures=[record['now_ms'] for record in records if record['phase']=='capture']
    if actual_captures!=captures:raise ValueError('desktop runtime did not return the requested capture times')
    destination.mkdir(parents=True,exist_ok=True)
    images=[]
    for record in records:
        if record['phase']!='capture':continue
        from PIL.PngImagePlugin import PngInfo
        pixels=raster(record);path=protect_artifact(paths[record['now_ms']],inputs,'png')
        metadata=PngInfo();metadata.add_text('tinyrt_preview','1')
        encoded=io.BytesIO();pixels.save(encoded,format='PNG',pnginfo=metadata)
        tinyrt.write_atomic(path,encoded.getvalue());images.append(str(path))
    timings=[dict(phase=item['phase'],ms=item['now_ms'],elapsed_ms=item['elapsed_ms'],heap_bytes=item['heap_bytes'])
             for item in records if item['phase'] not in ('capture','shutdown')]
    report=dict(producer='tinyrt-preview',schema=1,engine='WAMR classic interpreter',device_performance=False,
                scheduling='guest_clock' if step_ms is None else 'fixed_injection',
                step_ms=step_ms,images=images,timings=timings)
    report_path=protect_artifact(report_path,inputs,'report')
    tinyrt.write_atomic(report_path,(json.dumps(report,indent=2)+'\n').encode('utf-8'))
    return dict(images=images,report=str(report_path),device_performance=False,scheduling=report['scheduling'])
