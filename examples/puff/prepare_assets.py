"""Generate Puff 2 sprites, text and sounds. All art and sound are drawn here
from code; no external assets. Requires Pillow and the Microsoft YaHei font."""
from pathlib import Path
import math
import struct
from PIL import Image, ImageDraw, ImageFont

APP = Path(__file__).resolve().parent
ATLAS_W, ATLAS_H = 512, 512
pending = []                # (name, image) packed at the end
MASKED = set()              # sprites that need per-row opacity masks

def add(name, image, masked=False):
    pending.append((name, image))
    if masked: MASKED.add(name)

def canvas(w, h):
    image = Image.new('RGBA', (w, h)); return image, ImageDraw.Draw(image)

OUT = '#2e2440'

# ---------------------------------------------------------------- hero "Doudou" (original Puff robot)
# Same pixel art as Puff v2 (from Pixel Quest): white robot, cyan visor, orange body.
HERO_COLORS = {'.': None, 'k': (34, 25, 43), 'w': (244, 244, 248), 'c': (63, 208, 255), 'C': (31, 127, 184),
               'o': (255, 140, 46), 'O': (194, 83, 26), 'y': (255, 214, 63)}
HERO = [['................',
         '........ky......',
         '........k.......',
         '.....kkkkkk.....',
         '....kwwwwwwk....',
         '...kwwwwwwwwk...',
         '...kwwkkkkkkk...',
         '...kwkcccccCk...',
         '...kwkcwcccCk...',
         '...kwwkkkkkkk...',
         '....kwwwwwwk....',
         '...kkoooooOkk...',
         '..kwkoyyyoOkwk..',
         '..kkkooooOOkkk..',
         '....kOOk.kOOk...',
         '....kkkk.kkkk...'],
        ['................',
         '........ky......',
         '........k.......',
         '.....kkkkkk.....',
         '....kwwwwwwk....',
         '...kwwwwwwwwk...',
         '...kwwkkkkkkk...',
         '...kwkcccccCk...',
         '...kwkcwcccCk...',
         '...kwwkkkkkkk...',
         '....kwwwwwwk....',
         '...kkoooooOkk...',
         '..kwkoyyyoOkwk..',
         '..kkkooooOOkkk..',
         '...kOOk..kOOk...',
         '...kkkk...kkk...']]
def hero(frame, hurt=False):
    rows = HERO[frame]
    image = Image.new('RGBA', (16, 16))
    for y, row in enumerate(rows):
        for x, key in enumerate(row):
            rgb = HERO_COLORS[key]
            if rgb is None: continue
            if hurt and key in 'wcC': rgb = (255, 120, 120) if key == 'w' else (200, 40, 60)
            image.putpixel((x, y), (*rgb, 255))
    return image.resize((32, 32), Image.NEAREST)   # drawn at 2x in the atlas, 32 px on screen
add('hero0', hero(0), True)          # idle
add('hero1', hero(1), True)          # blowing / legs kicking
add('hero2', hero(0, True), True)    # hurt

def balloon(body, dark):
    image, d = canvas(22, 30)
    d.ellipse((1, 0, 20, 23), fill=OUT); d.ellipse((2, 1, 19, 22), fill=body)
    d.ellipse((5, 3, 9, 10), fill='#ffffff')
    d.polygon([(9, 22), (12, 22), (10, 26)], fill=dark)
    return image
for name, body, dark in [('b_red', '#ff5d6c', '#b8323f'), ('b_blue', '#4fb3ff', '#2a6fb0'),
                         ('b_green', '#5ad18a', '#2f8a55'), ('b_yellow', '#ffd23f', '#b8901c'),
                         ('b_purple', '#b98bff', '#6c47b8')]:
    add(name, balloon(body, dark), True)

# ---------------------------------------------------------------- enemies
def bee(frame):
    image, d = canvas(34, 30)
    wing = (8, 2, 18, 12) if frame else (8, 6, 18, 14)
    d.ellipse(wing, fill='#e8f6ff', outline=OUT); d.ellipse((wing[0] + 8, wing[1], wing[2] + 8, wing[3]), fill='#e8f6ff', outline=OUT)
    d.ellipse((3, 11, 30, 28), fill=OUT); d.ellipse((5, 13, 28, 26), fill='#ffcc33')
    for x in (11, 18): d.rectangle((x, 13, x + 3, 26), fill=OUT)
    d.ellipse((22, 15, 26, 19), fill='white'); d.point((24, 17), fill=OUT)
    d.polygon([(3, 19), (0, 21), (3, 23)], fill=OUT)
    return image
add('bee0', bee(0), True); add('bee1', bee(1), True)

def crow(frame):
    image, d = canvas(40, 32)
    d.ellipse((8, 10, 32, 28), fill='#2b2b3d')
    if frame == 0: d.polygon([(10, 16), (0, 4), (18, 12)], fill='#3a3a55'); d.polygon([(30, 16), (40, 4), (22, 12)], fill='#3a3a55')
    elif frame == 1: d.polygon([(10, 18), (2, 28), (18, 20)], fill='#3a3a55'); d.polygon([(30, 18), (38, 28), (22, 20)], fill='#3a3a55')
    else: d.polygon([(12, 20), (4, 22), (14, 24)], fill='#3a3a55'); d.polygon([(28, 20), (36, 22), (26, 24)], fill='#3a3a55')
    d.ellipse((24, 12, 30, 18), fill='#fff0a0'); d.point((27, 15), fill='black')
    d.polygon([(31, 17), (38, 19), (31, 21)], fill='#ffa63d')
    return image
for i in range(3): add('crow%d' % i, crow(i), True)

spike, d = canvas(32, 32)
for k in range(8):
    a = k * math.pi / 4
    d.polygon([(16 + 15 * math.cos(a), 16 + 15 * math.sin(a)), (16 + 7 * math.cos(a + .4), 16 + 7 * math.sin(a + .4)),
               (16 + 7 * math.cos(a - .4), 16 + 7 * math.sin(a - .4))], fill='#8c5a9e')
d.ellipse((7, 7, 25, 25), fill=OUT); d.ellipse((9, 9, 23, 23), fill='#c28bd8'); d.ellipse((11, 11, 16, 16), fill='#ead2f2')
add('spike', spike, True)

thorn, d = canvas(32, 24)
d.rectangle((0, 8, 31, 17), fill='#3f7a3a')
for x in range(0, 32, 8):
    d.polygon([(x, 8), (x + 4, 0), (x + 8, 8)], fill='#6bb35e'); d.polygon([(x, 17), (x + 4, 24), (x + 8, 17)], fill='#2d5a2a')
add('thorn', thorn, True)

def cloud(w, h, color, shade):
    image, d = canvas(w, h)
    d.ellipse((0, h * .35, w * .55, h), fill=shade); d.ellipse((w * .45, h * .35, w, h), fill=shade)
    d.ellipse((w * .15, 0, w * .7, h * .9), fill=color); d.ellipse((w * .45, h * .2, w * .9, h * .9), fill=color)
    d.ellipse((0, h * .4, w * .5, h * .95), fill=color)
    return image
add('cloud', cloud(64, 32, '#ffffff', '#d7e8f5'))
add('bounce', cloud(64, 28, '#ffd6ec', '#f3a8cc'), True)
tc = cloud(64, 36, '#6b6f8a', '#4a4d63'); dd = ImageDraw.Draw(tc); dd.ellipse((22, 14, 28, 20), fill='white'); dd.ellipse((36, 14, 42, 20), fill='white')
add('thunder0', tc, True)
tc2 = cloud(64, 36, '#8a7dc9', '#5d5296'); dd = ImageDraw.Draw(tc2); dd.ellipse((22, 14, 28, 20), fill='#fff36b'); dd.ellipse((36, 14, 42, 20), fill='#fff36b')
add('thunder1', tc2, True)
bolt, d = canvas(24, 64)
d.polygon([(14, 0), (2, 30), (11, 30), (6, 64), (22, 24), (13, 24), (20, 0)], fill='#fff36b', outline='#ffb800')
add('bolt', bolt, True)

fly, d = canvas(16, 16)
d.ellipse((0, 0, 15, 15), fill='#3a4a10'); d.ellipse((3, 3, 12, 12), fill='#e8ff6b'); d.ellipse((5, 5, 9, 9), fill='white')
add('firefly', fly, True)
zap, d = canvas(32, 12)
for x in range(0, 32, 4): d.line((x, 6 + (2 if x % 8 else -2), x + 4, 6 - (2 if x % 8 else -2)), fill='#9df5ff', width=2)
add('zap', zap)
post, d = canvas(10, 20)
d.rectangle((2, 0, 7, 19), fill='#5a6070'); d.ellipse((0, 0, 9, 8), fill='#ffec5c')
add('post', post, True)
arrow, d = canvas(24, 16)
d.polygon([(0, 5), (14, 5), (14, 0), (24, 8), (14, 16), (14, 11), (0, 11)], fill='#ffffff')
add('arrow', arrow)
drop, d = canvas(6, 12)
d.ellipse((0, 4, 5, 11), fill='#9ec9ff'); d.polygon([(3, 0), (0, 6), (5, 6)], fill='#9ec9ff')
add('drop', drop)
swirl, d = canvas(48, 48)
for r, c in [(22, '#b3a8d8'), (15, '#8c7fc0'), (8, '#6a5ca8')]:
    d.arc((24 - r, 24 - r, 24 + r, 24 + r), 0, 280, fill=c, width=4)
add('swirl', swirl)

# ---------------------------------------------------------------- pickups
coin_frames = []
for i, w in enumerate((20, 14, 6, 14)):
    image, d = canvas(20, 20)
    x0 = (20 - w) // 2
    d.ellipse((x0, 0, x0 + w - 1, 19), fill='#c98a00'); d.ellipse((x0 + 1, 1, x0 + w - 2, 17), fill='#ffd23f')
    if w > 10: d.rectangle((9, 5, 10, 13), fill='#fff1a8')
    add('coin%d' % i, image)
def star_shape(size, fill, edge):
    image, d = canvas(size, size)
    c = size / 2
    pts = [(c + (c - 1) * (1 if k % 2 == 0 else .45) * math.sin(k * math.pi / 5),
            c - (c - 1) * (1 if k % 2 == 0 else .45) * math.cos(k * math.pi / 5)) for k in range(10)]
    d.polygon(pts, fill=fill, outline=edge)
    return image
add('starcoin', star_shape(26, '#fff36b', '#e09a00'))
add('star_on', star_shape(30, '#ffd23f', '#b87a00')); add('star_off', star_shape(30, '#4a4f66', '#2e3242'))
gift, d = canvas(28, 28)
d.ellipse((0, 0, 27, 27), fill=(255, 255, 255, 110)); d.ellipse((0, 0, 27, 27), outline='#ffffff')
d.ellipse((5, 4, 11, 10), fill='white')
add('bubble', gift)
def icon(name, draw):
    image, d = canvas(20, 20); draw(d); add(name, image)
icon('i_balloon', lambda d: (d.ellipse((3, 0, 16, 15), fill='#ff5d6c'), d.line((10, 15, 10, 19), fill='white')))
icon('i_shield', lambda d: d.polygon([(10, 0), (19, 4), (17, 14), (10, 19), (3, 14), (1, 4)], fill='#5ec8ff', outline='white'))
icon('i_magnet', lambda d: (d.arc((2, 2, 17, 17), 180, 360, fill='#ff4d4d', width=5), d.rectangle((2, 9, 6, 16), fill='#ff4d4d'),
                             d.rectangle((13, 9, 17, 16), fill='#ff4d4d'), d.rectangle((2, 14, 6, 17), fill='white'), d.rectangle((13, 14, 17, 17), fill='white')))
icon('i_feather', lambda d: (d.ellipse((5, 0, 15, 18), fill='#ffffff'), d.line((4, 19, 14, 2), fill='#9aa6c8', width=2)))
icon('i_hourglass', lambda d: (d.polygon([(3, 1), (16, 1), (10, 10)], fill='#ffd27a'), d.polygon([(3, 18), (16, 18), (10, 10)], fill='#ffd27a'),
                                d.line((2, 1, 17, 1), fill='#8a5a2a', width=2), d.line((2, 18, 17, 18), fill='#8a5a2a', width=2)))
icon('i_pin', lambda d: (d.ellipse((1, 1, 9, 9), fill='#ff5d9e'), d.line((6, 6, 18, 18), fill='#d0d6e8', width=2)))
flag, d = canvas(48, 64)
d.rectangle((6, 0, 9, 63), fill='#5a5070'); d.polygon([(10, 2), (46, 12), (10, 24)], fill='#ff5d6c')
d.polygon([(10, 2), (28, 7), (10, 12)], fill='#ff9aa4')
add('flag', flag)

# ---------------------------------------------------------------- bosses (big, masked)
def queen(frame):
    image, d = canvas(96, 80)
    wy = 6 if frame else 14
    d.ellipse((18, wy, 46, wy + 26), fill='#e8f6ff', outline=OUT); d.ellipse((50, wy, 78, wy + 26), fill='#e8f6ff', outline=OUT)
    d.ellipse((10, 26, 86, 78), fill=OUT); d.ellipse((13, 29, 83, 75), fill='#ffbf1f')
    for x in (30, 46, 62): d.rectangle((x, 30, x + 7, 74), fill=OUT)
    d.polygon([(34, 22), (40, 6), (48, 18), (56, 6), (62, 22)], fill='#ffe066', outline='#b88a00')
    d.ellipse((52, 38, 64, 50), fill='white'); d.ellipse((57, 42, 62, 47), fill=OUT)
    d.ellipse((40, 62, 56, 76), fill='#ff6b9a')   # weak spot (belly)
    return image
half = lambda im: im.resize((im.width // 2, im.height // 2), Image.NEAREST)
add('queen0', half(queen(0))); add('queen1', half(queen(1)))
def storm(frame):
    image = cloud(112, 64, '#5c5f80' if not frame else '#8b7fd0', '#3c3f58' if not frame else '#5d5296')
    d = ImageDraw.Draw(image)
    eye = '#ffffff' if not frame else '#fff36b'
    d.ellipse((38, 24, 50, 36), fill=eye); d.ellipse((62, 24, 74, 36), fill=eye)
    d.ellipse((42, 28, 48, 34), fill=OUT); d.ellipse((66, 28, 72, 34), fill=OUT)
    d.ellipse((48, 48, 64, 62), fill='#ff6b9a')
    return image
add('storm0', half(storm(0))); add('storm1', half(storm(1)))
def owl(frame):
    image, d = canvas(84, 84)
    d.ellipse((6, 10, 78, 82), fill='#4a3a6a'); d.ellipse((14, 26, 70, 80), fill='#6c5a92')
    d.polygon([(10, 14), (20, 0), (28, 14)], fill='#4a3a6a'); d.polygon([(56, 14), (64, 0), (74, 14)], fill='#4a3a6a')
    eye = '#fff36b' if frame == 1 else '#ffd27a'
    for cx in (28, 56):
        d.ellipse((cx - 13, 22, cx + 13, 48), fill='#e8dcff'); d.ellipse((cx - 8, 27, cx + 8, 43), fill=eye)
        d.ellipse((cx - 3, 32, cx + 3, 38), fill=OUT)
    d.polygon([(38, 44), (46, 44), (42, 54)], fill='#ffa63d')
    if frame == 2:
        d.line((8, 52, 0, 70), fill='#4a3a6a', width=8); d.line((76, 52, 84, 70), fill='#4a3a6a', width=8)
    d.ellipse((34, 62, 50, 76), fill='#ff6b9a')
    return image
for i in range(3): add('owl%d' % i, half(owl(i)))
def king(frame):
    image, d = canvas(112, 96)
    base = cloud(112, 70, '#3c3552' if not frame else '#6a4fa0', '#26213a' if not frame else '#463270')
    image.paste(base, (0, 26), base)
    d.polygon([(30, 34), (36, 8), (46, 26), (56, 0), (66, 26), (76, 8), (82, 34)], fill='#ffd23f', outline='#b88a00')
    for cx in (42, 70):
        d.ellipse((cx - 9, 48, cx + 9, 62), fill='#ff5d5d' if frame else '#ffffff'); d.ellipse((cx - 3, 52, cx + 3, 58), fill=OUT)
    d.ellipse((48, 76, 64, 90), fill='#ff6b9a')
    return image
add('king0', half(king(0))); add('king1', half(king(1)))

# ---------------------------------------------------------------- scenery
hill, d = canvas(128, 40)
d.ellipse((-20, 8, 80, 80), fill='#79c26b'); d.ellipse((40, 0, 150, 90), fill='#5fae5a')
for x in (16, 52, 96): d.ellipse((x, 18, x + 8, 26), fill='#ffe36b')
add('hill', hill)
moon, d = canvas(40, 40)
d.ellipse((0, 0, 39, 39), fill='#fff6c8'); d.ellipse((12, -4, 46, 30), fill=(0, 0, 0, 0))
add('moon', moon)
sun, d = canvas(40, 40)
d.ellipse((2, 2, 37, 37), fill='#ffe59a'); d.ellipse((7, 7, 32, 32), fill='#fff2c8')
add('sun', sun)
twinkle, d = canvas(8, 8)
d.line((4, 0, 4, 7), fill='#ffffff'); d.line((0, 4, 7, 4), fill='#ffffff')
add('twinkle', twinkle)

# ---------------------------------------------------------------- digits (HUD numbers stay pixel art)
FONT = 'C:/Windows/Fonts/msyhbd.ttc'
def text(name, label, size, color):
    font = ImageFont.truetype(FONT, size)
    box = font.getbbox(label)
    image = Image.new('RGBA', (box[2] - box[0] + 4, box[3] - box[1] + 4))
    d = ImageDraw.Draw(image)
    d.text((2 - box[0], 2 - box[1]), label, font=font, fill='#2e2440')
    d.text((1 - box[0], 1 - box[1]), label, font=font, fill=color)
    add(name, image)
for d_ in range(10): text('d%d' % d_, str(d_), 22, '#ffffff')
text('d_x', '×', 18, '#ffffff')
text('d_slash', '/', 18, '#ffffff')
text('d_m', 'm', 16, '#ffffff')
lock, d = canvas(20, 24)
d.rectangle((2, 10, 17, 23), fill='#c8ccd8'); d.arc((4, 0, 15, 18), 180, 360, fill='#c8ccd8', width=3)
add('lock', lock)

# ---------------------------------------------------------------- pack (tallest first, shelf rows)
sheet = Image.new('RGBA', (ATLAS_W, ATLAS_H))
entries = []
x = y = row = 0
for name, image in sorted(pending, key=lambda item: -item[1].height):
    if x + image.width > ATLAS_W:
        y += row + 1; x = row = 0
    assert y + image.height <= ATLAS_H, name
    sheet.paste(image, (x, y))
    entries.append((name, x, y, image.width, image.height))
    x += image.width + 1; row = max(row, image.height)
USED_H = y + row + 1
print('atlas rows used', USED_H, 'of', ATLAS_H)
sheet = sheet.crop((0, 0, ATLAS_W, USED_H))

# ---------------------------------------------------------------- glyph atlas (texture 1)
# Device fonts only cover firmware UI text, so the game draws its own: every
# character used by main.c / puff_game.h, at 18 px and 28 px, white on clear.
import re
SRC = (APP / 'main.c').read_text(encoding='utf-8')
GAME = (APP / 'puff_game.h').read_text(encoding='utf-8')
STRINGS = re.findall(r'"([^"\\]*)"', SRC) + re.findall(r'^\s*\{"([^"]+)",\d,PG_KIND', GAME, re.M)
CHARS = sorted({c for s_ in STRINGS for c in s_ if ord(c) > 127} | set('PUF0123456789/-:. '))
GLYPH_SIZES = (20,)          # larger text scales these glyphs up at draw time
TEXT_W = 512
glyphs = []
text_sheet = Image.new('RGBA', (TEXT_W, 512))
tx = ty = trow = 0
for size in GLYPH_SIZES:
    font = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', size)
    ascent, descent = font.getmetrics()
    cell_h = ascent + descent
    for ch in CHARS:
        adv = int(round(font.getlength(ch))) or size // 3
        image = Image.new('RGBA', (adv + 1, cell_h))
        ImageDraw.Draw(image).text((0, 0), ch, font=font, fill='#ffffff')
        if tx + image.width > TEXT_W: ty += trow + 1; tx = trow = 0
        assert ty + image.height <= 512, ch
        text_sheet.paste(image, (tx, ty))
        glyphs.append((ord(ch), size, tx, ty, image.width, image.height, adv))
        tx += image.width + 1; trow = max(trow, image.height)
TEXT_H = ty + trow + 1
print('glyph atlas rows', TEXT_H, 'chars', len(CHARS))
text_indices = bytes(1 if a >= 100 else 0 for a in text_sheet.crop((0, 0, TEXT_W, TEXT_H)).getchannel('A').tobytes())

# ---------------------------------------------------------------- export INDEX8 atlas + palette
rgb = sheet.convert('RGB').quantize(colors=255, method=Image.Quantize.MEDIANCUT)
indices = bytearray(color + 1 if alpha >= 96 else 0
                    for color, alpha in zip(rgb.tobytes(), sheet.getchannel('A').tobytes()))
colors = rgb.getpalette() + [0] * 765
palette = [0] + [((colors[o] >> 3) << 11) | ((colors[o + 1] >> 2) << 5) | (colors[o + 2] >> 3) for o in range(0, 765, 3)]

# ---------------------------------------------------------------- sounds (16 kHz PCM16 mono, synthesized)
RATE = 16000
def tone(parts, volume=0.35):
    samples = []
    for freq, ms, kind in parts:
        n = RATE * ms // 1000
        for i in range(n):
            t = i / RATE
            env = min(1, i / 80) * (1 - i / n) ** 1.5
            if kind == 'sq': v = 1 if math.sin(2 * math.pi * freq * t) > 0 else -1
            elif kind == 'noise':
                seed = (i * 1103515245 + 12345) & 0x7fffffff; v = (seed / 0x3fffffff) - 1
            else: v = math.sin(2 * math.pi * freq * t)
            samples.append(int(max(-1, min(1, v * env * volume)) * 32767))
    return struct.pack('<%dh' % len(samples), *samples)
SOUNDS = {
    'snd_coin': tone([(1320, 50, 'sq'), (1760, 70, 'sq')], 0.18),
    'snd_star': tone([(880, 70, 'sine'), (1175, 70, 'sine'), (1568, 140, 'sine')]),
    'snd_hit': tone([(220, 60, 'noise'), (160, 160, 'sq')], 0.3),
    'snd_item': tone([(660, 60, 'sine'), (990, 100, 'sine')]),
    'snd_dash': tone([(400, 40, 'sine'), (800, 60, 'sine'), (1200, 80, 'sine')]),
    'snd_boss': tone([(300, 80, 'sq'), (200, 160, 'noise')], 0.3),
    'snd_win': tone([(784, 110, 'sine'), (988, 110, 'sine'), (1175, 110, 'sine'), (1568, 260, 'sine')]),
    'snd_lose': tone([(523, 160, 'sine'), (392, 160, 'sine'), (262, 320, 'sine')]),
    'snd_bounce': tone([(300, 40, 'sine'), (600, 80, 'sine')]),
}

blob = bytearray(indices)
palette_offset = len(blob); blob += struct.pack('<256H', *palette)
text_offset = len(blob); blob += text_indices
sound_offsets = {}
for name, data in SOUNDS.items():
    while len(blob) % 4: blob.append(0)
    sound_offsets[name] = (len(blob), len(data)); blob += data
(APP / 'resources.bin').write_bytes(bytes(blob))

header = ['/* Generated by prepare_assets.py. Original art and sound, drawn in code. */',
          'typedef struct { int u,v,w,h; const uint32_t *mask; } sprite_t;',
          '#define ATLAS_WIDTH %d' % ATLAS_W, '#define ATLAS_HEIGHT %d' % USED_H,
          '#define PALETTE_OFFSET %d' % palette_offset, '#define TEXT_ATLAS_OFFSET %d' % text_offset,
          '#define TEXT_ATLAS_WIDTH %d' % TEXT_W, '#define TEXT_ATLAS_HEIGHT %d' % TEXT_H,
          'typedef struct { uint32_t code; uint8_t px; uint16_t u, v; uint8_t w, h, adv; } glyph_t;']
for name, (off, size) in sound_offsets.items():
    header.append('#define %s_OFFSET %d\n#define %s_BYTES %d' % (name.upper(), off, name.upper(), size))
for name, x, y, w, h in entries:
    mask = '0'
    base = 0
    if name in MASKED and w <= 32:
        rows = []
        for row in range(h):
            bits = 0
            for col in range(w):
                if indices[(base + y + row) * ATLAS_W + x + col]: bits |= 1 << col
            rows.append('0x%xu' % bits)
        mask = name + '_mask'
        header.append('static const uint32_t %s[] = {%s};' % (mask, ','.join(rows)))
    header.append('static const sprite_t %s = {%d,%d,%d,%d,%s};' % (name, x, y, w, h, mask))
header.append('static const glyph_t glyphs[] = {')
for code, size, x, y, w, h, adv in sorted(glyphs):
    header.append('    {%d,%d,%d,%d,%d,%d,%d},' % (code, size, x, y, w, h, adv))
header.append('};')
header.append('#define GLYPH_COUNT %d' % len(glyphs))
# Right-edge progress arc, sampled once here instead of using per-pixel atan2.
gauge = [(round(218 * math.cos(math.radians(300 + i))),
          round(218 * math.sin(math.radians(300 + i)))) for i in range(121)]
header.append('static const int16_t gauge_points[][2] = {' +
              ','.join('{%d,%d}' % point for point in gauge) + '};')
header.append('static const uint16_t circle_width[] = {' +
              ','.join(str(round(1024 * math.sqrt(1 - (i / 64) ** 2))) for i in range(65)) + '};')
(APP / 'assets_generated.h').write_text('\n'.join(header) + '\n', encoding='utf-8')
print('resources', len(blob), 'bytes')
