"""Generate Puff sprites and text. Robot, bee and cloud art originate in Pixel Quest."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFont

APP = Path(__file__).resolve().parent

# Original project art copied from Pixel Quest; no sibling generator dependency.
COLORS = {'.': None,
 'k': (34, 25, 43),
 'w': (244, 244, 248),
 'l': (200, 208, 224),
 'c': (63, 208, 255),
 'C': (31, 127, 184),
 'o': (255, 140, 46),
 'O': (194, 83, 26),
 'y': (255, 214, 63),
 'Y': (212, 150, 26),
 'g': (98, 212, 74),
 'G': (47, 143, 58),
 'd': (176, 106, 60),
 'D': (112, 60, 34),
 'b': (204, 92, 52),
 'B': (122, 46, 28),
 's': (154, 163, 181),
 'S': (92, 100, 120),
 'p': (155, 91, 214),
 'P': (93, 47, 143),
 'r': (232, 66, 74),
 'v': (255, 95, 200),
 'V': (176, 42, 138),
 'h': (143, 212, 122),
 'H': (107, 184, 94),
 'u': (214, 236, 255),
 't': (66, 245, 200),
 'T': (26, 165, 138),
 'n': (160, 110, 70),
 'N': (100, 64, 40)}

PLAYER = {24: ['................',
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
 25: ['................',
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
      '...kkkk...kkk...']}

ENEMY = {30: ['................',
      '...kkk....kkk...',
      '..kuuuk..kuuuk..',
      '..kuwuukkuuwuk..',
      '...kuuukkuuuk...',
      '....kkyyyykk....',
      '...kyykkkkyyk...',
      '..kykwkyykkkyk..',
      '..kykkkyykkkyk..',
      '...kyykkkkyyk...',
      '....kkyyyykk....',
      '......kYYk......',
      '.......kk.......',
      '................',
      '................',
      '................'],
 31: ['................',
      '................',
      '................',
      '...kkkk..kkkk...',
      '..kuuuwkkwuuuk..',
      '...kkkyyyykkk...',
      '...kyykkkkyyk...',
      '..kykwkyykkkyk..',
      '..kykkkyykkkyk..',
      '...kyykkkkyyk...',
      '....kkyyyykk....',
      '......kYYk......',
      '.......kk.......',
      '................',
      '................',
      '................']}

CLOUD = ['................................',
 '................................',
 '............wwwwww..............',
 '..........wwwwwwwwww............',
 '........wwwwwwwwwwwww...........',
 '....wwwwwwwwwwwwwwwwwwwww.......',
 '...wwwwwwwwwwwwwwwwwwwwwwww.....',
 '..wwwwwwwwwwwwwwwwwwwwwwwwwww...',
 '.wwwwwwwwwwwwwwwwwwwwwwwwwwwww..',
 '.wwwwwwwwwwwwwwwwwwwwwwwwwwwwww.',
 '.uwwwwwwwwwwwwwwwwwwwwwwwwwwwwu.',
 '..uuwwwwwwwwwwwwwwwwwwwwwwwwuu..',
 '....uuuuuuuuuuuuuuuuuuuuuuuu....',
 '................................',
 '................................',
 '................................']


sheet = Image.new('RGBA', (256, 512))
entries = []
cursor_x = cursor_y = row_height = 0

def add(name, image):
    global cursor_x, cursor_y, row_height
    if cursor_x + image.width > 256:
        cursor_y += row_height + 1
        cursor_x = row_height = 0
    assert cursor_y + image.height <= sheet.height, name
    sheet.paste(image, (cursor_x, cursor_y))
    entries.append((name, cursor_x, cursor_y, image.width, image.height))
    cursor_x += image.width + 1
    row_height = max(row_height, image.height)

def pixels(rows):
    image = Image.new('RGBA', (len(rows[0]), len(rows)))
    for y, row in enumerate(rows):
        for x, key in enumerate(row):
            rgb = COLORS[key]
            if rgb: image.putpixel((x, y), (*rgb, 255))
    return image

def text(name, label, size, color, bold=False):
    font = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc' if bold else 'C:/Windows/Fonts/msyh.ttc', size)
    box = font.getbbox(label)
    image = Image.new('RGBA', (box[2]-box[0]+4, box[3]-box[1]+4))
    ImageDraw.Draw(image).text((2-box[0], 2-box[1]), label, font=font, fill=color)
    add(name, image)

for index in range(2):
    add('robot'+str(index), pixels(PLAYER[24+index]))
    add('bee'+str(index), pixels(ENEMY[30+index]))
add('cloud', pixels(CLOUD))
sun_image=Image.new('RGBA',(32,32));d=ImageDraw.Draw(sun_image)
d.ellipse((1,1,30,30),fill='#ffedc3');d.ellipse((4,3,27,26),fill='#fff4d6')
add('sun',sun_image)
for name, body, dark in [('balloon_red','#fa795d','#be4a51'), ('balloon_yellow','#ffd467','#cb963f')]:
    image = Image.new('RGBA', (20, 27)); d = ImageDraw.Draw(image)
    d.ellipse((1, 0, 18, 22), fill='#554459')
    d.ellipse((2, 1, 17, 20), fill=body)
    d.ellipse((4, 2, 8, 9), fill='#fff4d5')
    d.line((7, 21, 9, 23, 11, 21), fill=dark, width=2)
    d.line((9, 24, 9, 26), fill='#554459')
    add(name, image)

goal = Image.new('RGBA', (88, 52));d = ImageDraw.Draw(goal)
d.ellipse((0, 27, 87, 47), fill='#b9daeb'); d.ellipse((3, 23, 84, 40), fill='#fff8df')
for x, y, r in [(18,25,12),(38,22,16),(62,24,14)]:
    d.ellipse((x-r,y-r,x+r,y+r),fill='#fff8df')
d.rectangle((40,0,42,33), fill='#55556c')
d.polygon([(43,1),(65,1),(59,8),(65,15),(43,15)], fill='#f47b60')
d.rectangle((43,2,46,5), fill='#fff4d6');add('goal',goal)
ground=Image.new('RGBA',(233,35));d=ImageDraw.Draw(ground)
d.rectangle((0,5,232,34),fill='#7eaa92');d.rectangle((0,5,232,10),fill='#b9d1a0')
for x in range(-8,233,14):
    d.rectangle((x,2,x+9,7),fill='#dbe1af')
    d.rectangle((x+4,14,x+10,17),fill='#68998a')
add('ground',ground)
text('title','PUFF',40,'#394a60',True)
text('subtitle','吹一口，去云顶',15,'#657d88')
text('start','开始飞行',18,'#fff8e8',True)
text('mode_fixed','匀速',17,'#455c6c',True)
text('mode_strength','力度',17,'#455c6c',True)
text('ready','准备起飞',16,'#42596b',True)
text('go','出发！',26,'#42596b',True)
text('win','到达云顶！',28,'#394a60',True)
text('lose','气球飞走了',25,'#394a60',True)
text('timeout','时间到了',25,'#394a60',True)
text('retry','再飞一次',17,'#fff8e8',True)
text('time_label','本次用时',14,'#637d85')
text('guide','停一停，等黄蜂飞过',13,'#637d85')
text('microphone_error','麦克风不可用，请重试',13,'#a2504c')
for digit in range(10):text('digit'+str(digit),str(digit),26,'#394a60',True)
text('decimal','.',22,'#394a60',True)

# Index zero is transparent, matching the shared raster sprite contract.
rgb=sheet.convert('RGB').quantize(colors=255, method=Image.Quantize.MEDIANCUT)
indices=bytearray()
for color,alpha in zip(rgb.tobytes(),sheet.getchannel('A').tobytes()):
    indices.append(color+1 if alpha>=96 else 0)
palette=[0]
colors=rgb.getpalette()
colors += [0] * (255*3-len(colors))
for offset in range(0,255*3,3):
    r,g,b=colors[offset:offset+3];palette.append(((r>>3)<<11)|((g>>2)<<5)|(b>>3))
(APP/'resources.bin').write_bytes(bytes(indices)+struct.pack('<256H',*palette))
header=['/* Generated locally from original Pixel Quest assets. */',
        'typedef struct { int u,v,w,h; const uint32_t *opaque_rows; } sprite_t;',
        '#define ATLAS_WIDTH 256', '#define ATLAS_HEIGHT 512', '#define PALETTE_OFFSET 131072']
for name,x,y,w,h in entries:
    mask='0'
    if name in {'robot0','robot1','bee0','bee1','balloon_red','balloon_yellow'}:
        rows=[]
        for row in range(h):
            bits=0
            for col in range(w):
                # The balloon's string and knot do not form its body.
                if name.startswith('balloon') and row>=22:continue
                if indices[(y+row)*256+x+col]:bits|=1<<col
            rows.append(f'0x{bits:x}u')
        mask=name+'_opaque'
        header.append(f'static const uint32_t {mask}[] = {{'+','.join(rows)+'};')
    header.append(f'static const sprite_t {name} = {{{x},{y},{w},{h},{mask}}};')
(APP/'assets_generated.h').write_text('\n'.join(header)+'\n',encoding='utf-8')
print('atlas',cursor_y+row_height,'of',sheet.height,'rows;',len(indices),'pixels')
