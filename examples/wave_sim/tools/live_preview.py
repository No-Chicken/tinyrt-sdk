"""Arrow-key and touch preview using production Wasm in the real WAMR runner."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tkinter as tk

APP=Path(__file__).resolve().parents[1]
SDK=APP.parents[1]
sys.path.insert(0,str(SDK/'tools'))
import preview
from PIL import Image,ImageDraw,ImageTk


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--runner',required=True)
    args=parser.parse_args()
    manifest=json.loads((APP/'app.json').read_text(encoding='utf-8'))
    proc=subprocess.Popen([str(Path(args.runner).resolve()),str(APP/'build/demo.wave-sim.wasm'),
        '--pages',str(manifest['memory_pages']),'--permissions',str(manifest['permissions']),
        '--budget',str(manifest['budget'])],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
    def read():
        line=proc.stdout.readline()
        if not line:raise RuntimeError('runner stopped: '+proc.stderr.read())
        record=json.loads(line)
        if record['status']:raise RuntimeError(record['error'])
        return record
    initial=read()
    interval=initial['clock_interval_ms']
    window=tk.Tk();window.title('Wave Simulator · WAMR preview')
    window.configure(bg='#060a1a')
    display=tk.Label(window,bg='#060a1a');display.pack()
    tk.Label(window,text='Arrow keys: tilt · Space: KEY1 · Click: splash · Hold: theme\nInterpreter preview; device FPS is measured separately.',
        fg='#a8f0ff',bg='#060a1a',padx=12,pady=12).pack()
    held=set();pending=[];clock=0;deadline=interval;image_ref=None;closed=False
    def key(event,down):
        if event.keysym in ('Left','Right','Up','Down'):
            if down:held.add(event.keysym)
            else:held.discard(event.keysym)
        elif event.keysym=='space':
            if down and 'space' not in held:held.add('space');pending.append('key 1 1')
            elif not down and 'space' in held:held.discard('space');pending.append('key 1 0')
    window.bind('<KeyPress>',lambda event:key(event,True))
    window.bind('<KeyRelease>',lambda event:key(event,False))
    display.bind('<ButtonPress-1>',lambda e:pending.append(f'pointer 3 {min(465,max(0,e.x))} {min(465,max(0,e.y))}'))
    display.bind('<ButtonRelease-1>',lambda e:pending.append(f'pointer 1 {min(465,max(0,e.x))} {min(465,max(0,e.y))}'))
    def cancel(event=None):
        held.clear();pending.append('pointer 5 0 0')
    window.bind('<FocusOut>',cancel)
    mask=Image.new('L',(466,466),0);ImageDraw.Draw(mask).ellipse((0,0,465,465),fill=255)
    def update():
        nonlocal clock,deadline,image_ref
        if closed:return
        try:
            x=700*(('Right' in held)-('Left' in held))
            y=700*(('Down' in held)-('Up' in held))
            if not x and not y:y=700
            commands=[*pending,f'motion {x} {y} 700'];pending.clear()
            target=clock+33
            while deadline<=target:commands.append(f'tick {deadline & 0xffffffff}');deadline+=interval
            commands.extend([f'time {target & 0xffffffff}','capture'])
            proc.stdin.write('\n'.join(commands)+'\n');proc.stdin.flush()
            while True:
                record=read()
                if record['phase']=='capture':break
            clock=target
            image=preview.raster(record)
            image=Image.composite(image,Image.new('RGB',(466,466),'#060a1a'),mask)
            image_ref=ImageTk.PhotoImage(image);display.configure(image=image_ref)
        except Exception as error:
            tk.Label(window,text=str(error),fg='#ff9a2e',bg='#060a1a').pack();return
        window.after(33,update)
    def close():
        nonlocal closed
        closed=True
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill()
        window.destroy()
    window.protocol('WM_DELETE_WINDOW',close)
    window.after(0,update);window.mainloop()


if __name__=='__main__':main()
