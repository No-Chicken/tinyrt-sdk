"""Reproducible classic sprite/PCM conversion; requires Pillow, no network."""
from pathlib import Path
import struct
import wave
from PIL import Image

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / 'assets'
NEAREST = Image.Resampling.NEAREST
data = bytearray()
defs = []
resident = []

def rgb565(image):
    return b''.join(struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                    for r, g, b, a in image.convert('RGBA').getdata())

def sprite(name, image):
    image = image.convert('RGBA')
    runs = []
    rows = [0]
    for y in range(image.height):
        x = 0
        while x < image.width:
            if image.getpixel((x, y))[3] < 128:
                x += 1
                continue
            start = x
            while x < image.width and image.getpixel((x, y))[3] >= 128:
                x += 1
            offset = len(data)
            data.extend(rgb565(image.crop((start, y, x, y + 1))))
            runs.append((start, x - start, offset))
        rows.append(len(runs))
    defs.append('static const span_t '+name+'_spans[] = {' + ','.join('{%d,%d,%d}' % r for r in runs) + '};')
    defs.append('static const uint16_t '+name+'_rows[] = {' + ','.join(map(str, rows)) + '};')
    resident.append((name,image.copy()))
    return image

def load(name):
    return Image.open(SOURCE / 'sprites' / (name + '.png')).convert('RGBA')

bg = load('background-day').crop((0, 0, 288, 400)).resize((233, 198), NEAREST)
scene = Image.new('RGBA', (233, 233))
scene.paste(bg, (0, 0))
base = load('base').resize((168, 56), NEAREST)
scene.paste(base, (0, 198)); scene.paste(base, (168, 198))
resident.append(('background',scene.copy()))
data.extend(rgb565(scene))
sprite('ground', base)
sprite('pipe', load('pipe-green').resize((34, 198), NEAREST))
for index, frame in enumerate(('upflap', 'midflap', 'downflap')):
    sprite('bird'+str(index), load('yellowbird-'+frame).resize((24, 17), NEAREST))
sprite('bird_dead', load('yellowbird-midflap').rotate(-90, expand=True).resize((17, 24), NEAREST))
for n in range(10):
    image = load(str(n))
    sprite('digit'+str(n), image.resize((12 if n != 1 else 8, 18), NEAREST))
    sprite('small'+str(n), image.resize((8 if n != 1 else 5, 12), NEAREST))
atlas = Image.open(SOURCE / 'atlas.png').convert('RGBA')
def crop(name, box, size):
    return sprite(name, atlas.crop(box).resize(size, NEAREST))
panel = crop('panel', (0, 516, 238, 642), (166, 88))
over = crop('game_over', (784, 116, 988, 170), (142, 38))
okay = crop('okay', (924, 84, 1004, 112), (62, 22))
crop('new_best', (224, 1002, 256, 1016), (20, 9))
crop('medal_silver', (240, 516, 288, 564), (30, 30))
crop('medal_gold', (240, 564, 288, 612), (30, 30))
crop('ready', (584, 116, 776, 170), (134, 38))
crop('tap', (584, 220, 704, 284), (60, 32))

for name in ('wing', 'point', 'hit', 'die', 'swoosh'):
    with wave.open(str(SOURCE / 'audio' / (name+'.wav'))) as source:
        channels, width, rate, frames, _, _ = source.getparams()
        assert width == 2
        raw = struct.unpack('<'+'h'*(frames*channels), source.readframes(frames))
    mono = [sum(raw[i:i+channels]) // channels for i in range(0,len(raw),channels)]
    # Remove silent padding, then resample the original effect to host PCM16/16k.
    active = [i for i,v in enumerate(mono) if abs(v)>64]
    mono = mono[max(0,active[0]-rate//100):min(len(mono),active[-1]+rate//50)]
    count = min(16000, len(mono)*16000//rate)
    samples = []
    for i in range(count):
        position = i*rate/16000
        j = int(position); f = position-j
        value = round(mono[j]*(1-f)+mono[min(j+1,len(mono)-1)]*f)
        # Short tail fade avoids truncation clicks at the bounded clip limit.
        if i>=count-160: value = value*(count-i)//160
        samples.append(value)
    offset = len(data)
    data.extend(struct.pack('<'+'h'*count,*samples))
    defs.append('#define SOUND_%s_OFFSET %du\n#define SOUND_%s_LENGTH %du' % (name.upper(),offset,name.upper(),count*2))

# Lossless INDEX8 residency uses exactly the RGB565 colors from the legacy art.
# Index zero remains transparent; original span/audio offsets stay unchanged.
atlas_width=512
x=y=shelf=0
placed=[]
colors={}
for name,image in resident:
    if x+image.width>atlas_width:x=0;y+=shelf;shelf=0
    placed.append((name,image,x,y))
    opaque=int(all(alpha>=128 for red,green,blue,alpha in image.getdata()))
    if name!='background':
        defs.append('static const sprite_t '+name+' = {%d,%d,%s_rows,%s_spans,%d,%d,%d};' % (image.width,image.height,name,name,x,y,opaque))
    else:defs.append('static const sprite_t background = {%d,%d,0,0,%d,%d,%d};' % (image.width,image.height,x,y,opaque))
    for red,green,blue,alpha in image.getdata():
        if alpha>=128:
            color=((red>>3)<<11)|((green>>2)<<5)|(blue>>3)
            if color not in colors:colors[color]=len(colors)+1
    x+=image.width;shelf=max(shelf,image.height)
atlas_height=y+shelf
assert len(colors)<=255 and atlas_height<=512 and atlas_width*atlas_height<=262144
indices=bytearray(atlas_width*atlas_height)
for name,image,x,y in placed:
    for row in range(image.height):
        for col in range(image.width):
            red,green,blue,alpha=image.getpixel((col,row))
            if alpha>=128:indices[(y+row)*atlas_width+x+col]=colors[((red>>3)<<11)|((green>>2)<<5)|(blue>>3)]
defs.append('#define RESIDENT_ATLAS_OFFSET %du\n#define RESIDENT_ATLAS_WIDTH %du\n#define RESIDENT_ATLAS_HEIGHT %du\n#define RESIDENT_ATLAS_LENGTH %du' % (len(data),atlas_width,atlas_height,len(indices)))
data.extend(indices)
defs.append('#define RESIDENT_PALETTE_OFFSET %du\n#define RESIDENT_PALETTE_COUNT %du' % (len(data),len(colors)+1))
data.extend(struct.pack('<'+'H'*(len(colors)+1),0,*colors.keys()))
(ROOT/'resources.bin').write_bytes(data)
(ROOT/'assets_generated.h').write_text('/* Generated by prepare_assets.py. */\n'+'\n'.join(defs)+'\n',encoding='utf-8')
# App cover uses the actual shipped art, with no unrelated generated illustration.
cover=scene.copy()
cover.alpha_composite(load('pipe-green').resize((34,198),NEAREST),(176,142))
cover.alpha_composite(load('yellowbird-midflap').resize((51,36),NEAREST),(70,91))
cover.alpha_composite(atlas.crop((584,116,776,170)).resize((160,45),NEAREST),(37,37))
cover.resize((210,210),NEAREST).save(ROOT/'cover.png')
print('Generated',len(data),'resource bytes')
