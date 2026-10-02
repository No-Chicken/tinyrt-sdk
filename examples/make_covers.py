"""Generate original demo cover art from local game geometry; no external media."""
from pathlib import Path
import importlib.util
import json
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parent

def generate():
    image=Image.new('RGB',(210,210),'#101418');draw=ImageDraw.Draw(image)
    draw.rounded_rectangle((12,12,197,197),radius=32,fill='#182530')
    cells=[(5,5),(5,4),(4,4),(3,4),(3,3),(3,2),(4,2),(5,2),(6,2)]
    for i,(x,y) in enumerate(cells):
        draw.rounded_rectangle((30+x*18,27+y*20,45+x*18,43+y*20),radius=4,fill='#55D7AC' if i==0 else '#2DA67C')
    draw.ellipse((62,129,78,145),fill='#FFB95D')
    draw.ellipse((128,130,131,133),fill='#101418')
    image.save(ROOT/'snake/cover.png',optimize=True)
    spec=importlib.util.spec_from_file_location('maze_cover_source',ROOT/'nes-maze/make_rom.py')
    maze=importlib.util.module_from_spec(spec);spec.loader.exec_module(maze)
    image=Image.new('RGB',(210,210),'#101418');draw=ImageDraw.Draw(image)
    draw.rounded_rectangle((8,8,201,201),radius=26,fill='#182530')
    for y,row in enumerate(maze.MAP):
        for x,tile in enumerate(row):
            px,py=15+x*15,31+y*15
            if tile=='#':draw.rectangle((px,py,px+12,py+12),fill='#3B8EF0')
            elif tile=='G':draw.rectangle((px+2,py+2,px+10,py+10),fill='#FFD66B')
    x,y=maze.START;px,py=15+x*15,31+y*15
    draw.rectangle((px+1,py+1,px+11,py+11),fill='#55D7AC')
    image.save(ROOT/'nes-maze/cover.png',optimize=True)

if __name__=='__main__':generate()
