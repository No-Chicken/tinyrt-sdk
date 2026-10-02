"""Sky Trail: original MIT-licensed 6502 program, scrolling level and pixel art.

No external ROM input or downloads. Output is a standard mapper-0 iNES image.
Zero page: y=2, scroll=3/7, pad=4, score=5/10, frame=6, previous=8,
velocity=9, immunity=11, enemy_x=12, coin_x=13, collected=14.
"""
from pathlib import Path
import argparse, hashlib, json

class Program:
    def __init__(self): self.data=bytearray();self.labels={};self.fixups=[]
    def emit(self,*values): self.data.extend(values)
    def label(self,name):
        if name in self.labels:raise ValueError(name)
        self.labels[name]=0x8000+len(self.data)
    def absolute(self,opcode,name):
        self.emit(opcode,0,0);self.fixups.append((len(self.data)-2,name,False))
    def branch(self,opcode,name):
        self.emit(opcode,0);self.fixups.append((len(self.data)-1,name,True))
    def when(self,opcode,name):
        self.emit(opcode^0x20,3);self.absolute(0x4c,name)
    def finish(self):
        for pos,name,relative in self.fixups:
            address=self.labels[name]
            if relative:
                delta=address-(0x8000+pos+1)
                if not -128<=delta<=127:raise ValueError(f'branch too far: {name}')
                self.data[pos]=delta&255
            else:self.data[pos:pos+2]=address.to_bytes(2,'little')
        return self.data

FONT={
 'A':[14,17,17,31,17,17,17],'C':[14,17,16,16,16,17,14],
 'E':[31,16,16,30,16,16,31],'I':[31,4,4,4,4,4,31],
 'K':[17,18,20,24,20,18,17],'L':[16,16,16,16,16,16,31],
 'O':[14,17,17,17,17,17,14],'R':[30,17,17,30,20,18,17],
 'S':[15,16,16,14,1,1,30],'T':[31,4,4,4,4,4,4],
 'Y':[17,17,10,4,4,4,4],
}
DIGITS=[[14,17,19,21,25,17,14],[4,12,4,4,4,4,14],
        [14,17,1,2,4,8,31],[30,1,1,14,1,1,30],
        [2,6,10,18,31,2,2],[31,16,16,30,1,1,30],
        [14,16,16,30,17,17,14],[31,1,2,4,8,8,8],
        [14,17,17,14,17,17,14],[14,17,17,15,1,1,14]]
def text(value):return bytes(0 if c==' ' else 16+ord(c)-65 for c in value)
def background(page):
    data=bytearray(1024)
    for y in range(30):
        for x in range(32):
            tile=0
            if y>=28:tile=1 if y==28 else 2
            # Alternating mountain silhouettes and clouds cross both nametables.
            world=x+page*32
            if 15<=y<28 and y>=27-abs((world%24)-12):tile=3
            if y==10 and world%13 in (0,1,2,3):tile=4
            if y==27 and world%9==3:tile=5
            data[y*32+x]=tile
    data[2*32+3:2*32+12]=text('SKY TRAIL')
    data[4*32+3:4*32+9]=text('SCORE ');data[4*32+9:4*32+11]=bytes([48,48])
    return data

def make_rom():
    p=Program();p.label('reset');p.emit(0x78,0xd8,0xa2,255,0x9a)
    p.emit(0xa9,0,0x8d,0,0x20,0x8d,1,0x20,0xa2,31)
    p.label('clear');p.emit(0x95,0,0xca);p.branch(0x10,'clear')
    p.emit(0xa9,208,0x85,2,0xa9,255,0xa2,0)
    p.label('hide');p.emit(0x9d,0,2,0xe8);p.branch(0xd0,'hide')
    p.emit(0x2c,2,0x20)
    for name in ('warm1','warm2'):
        p.label(name);p.emit(0x2c,2,0x20);p.branch(0x10,name)
    # Two distinct nametables (vertical mirroring), written with rendering off.
    for screen in range(2):
        p.emit(0x2c,2,0x20,0xa9,0x20+screen*4,0x8d,6,0x20,0xa9,0,0x8d,6,0x20)
        for page in range(4):
            name=f'copy{screen}_{page}';p.emit(0xa2,0);p.label(name)
            p.absolute(0xbd,f'bg{screen}_{page}');p.emit(0x8d,7,0x20,0xe8);p.branch(0xd0,name)
    p.emit(0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20)
    palette=[0x21,0x19,0x07,0x30]*4+[0x21,0x28,0x12,0x0f]*4
    for col in palette:p.emit(0xa9,col,0x8d,7,0x20)
    p.absolute(0x20,'scroll');p.emit(0xa9,0x80,0x8d,0,0x20,0xa9,0x1e,0x8d,1,0x20)
    # Actual polling work, not a JMP-to-self shortcut; all scanlines execute CPU.
    p.label('idle');p.absolute(0x4c,'idle')
    p.label('nmi');p.emit(0x48,0x8a,0x48,0x98,0x48,0xe6,6)
    p.emit(0xa9,1,0x8d,0x16,0x40,0xa9,0,0x8d,0x16,0x40,0x85,4,0xa2,8)
    p.label('pad');p.emit(0xad,0x16,0x40,0x4a,0x26,4,0xca);p.branch(0xd0,'pad')
    p.emit(0xa5,4,0x29,16);p.branch(0xf0,'movement')
    for address in (3,5,7,9,10,11,14):p.emit(0xa9,0,0x85,address)
    p.emit(0xa9,208,0x85,2)
    p.label('movement');p.emit(0xa5,4,0x29,1);p.branch(0xf0,'left')
    p.emit(0xa5,3,0x18,0x69,2,0x85,3);p.branch(0x90,'left')
    p.emit(0xa5,7,0x49,1,0x85,7,0xa9,0,0x85,14)
    p.label('left');p.emit(0xa5,4,0x29,2);p.branch(0xf0,'jump')
    p.emit(0xa5,3,0x38,0xe9,2,0x85,3);p.branch(0xb0,'jump')
    p.emit(0xa5,7,0x49,1,0x85,7,0xa9,0,0x85,14)
    p.label('jump');p.emit(0xa5,2,0xc9,208);p.branch(0xd0,'gravity')
    p.emit(0xa5,8,0x49,255,0x25,4,0x29,128);p.branch(0xf0,'gravity')
    p.emit(0xa9,249,0x85,9) # -7 px/frame
    p.label('gravity');p.emit(0xa5,6,0x29,1);p.branch(0xd0,'vertical')
    p.emit(0xa5,9,0xc9,7);p.branch(0xf0,'vertical');p.emit(0xe6,9)
    p.label('vertical');p.emit(0xa5,2,0x18,0x65,9,0x85,2,0xc9,208)
    p.branch(0x90,'enemy');p.emit(0xa9,208,0x85,2,0xa9,0,0x85,9)
    # Enemy patrol independently; camera motion changes its screen position.
    p.label('enemy');p.emit(0xa5,6,0x29,31,0x18,0x69,192,0x38,0xe5,3,0x85,12)
    p.emit(0xa5,11);p.branch(0xf0,'collision');p.emit(0xc6,11);p.absolute(0x4c,'coin')
    p.label('collision');p.emit(0xa5,12,0xc9,94);p.branch(0x90,'coin')
    p.emit(0xc9,119);p.branch(0xb0,'coin');p.emit(0xa5,2,0xc9,195);p.branch(0x90,'coin')
    p.emit(0xa9,0,0x85,3,0x85,7,0xa9,60,0x85,11)
    # One coin per nametable lap; jump to collect. Score is real 6502 state.
    p.label('coin');p.emit(0xa9,160,0x38,0xe5,3,0x85,13,0xa5,14);p.branch(0xd0,'sprites')
    p.emit(0xa5,13,0xc9,98);p.branch(0x90,'sprites');p.emit(0xc9,120);p.branch(0xb0,'sprites')
    p.emit(0xa5,2,0xc9,187);p.branch(0xb0,'sprites')
    p.emit(0xa9,1,0x85,14,0xe6,5,0xa5,5,0xc9,10);p.branch(0x90,'sprites')
    p.emit(0xa9,0,0x85,5,0xe6,10,0xa5,10,0xc9,10);p.branch(0x90,'sprites')
    p.emit(0xa9,0,0x85,10)
    p.label('sprites')
    # Player 16x16 (four 8x8 sprites) and patrolling enemy; no Host game rules.
    for offset,tile,xdelta,ydelta,source in ((0,64,0,0,2),(4,65,8,0,2),(8,66,0,8,2),(12,67,8,8,2),
                                         (16,68,0,0,None),(20,69,8,0,None),(24,70,0,8,None),(28,71,8,8,None)):
        if source is None:p.emit(0xa9,208+ydelta)
        else:p.emit(0xa5,source,0x18,0x69,ydelta)
        p.emit(0x8d,offset,2,0xa9,tile,0x8d,offset+1,2,0xa9,0,0x8d,offset+2,2)
        if source is None:p.emit(0xa5,12,0x18,0x69,xdelta)
        else:p.emit(0xa9,104+xdelta)
        p.emit(0x8d,offset+3,2)
    p.emit(0xa5,14);p.branch(0xf0,'coin_show');p.emit(0xa9,255);p.absolute(0x4c,'coin_oam')
    p.label('coin_show');p.emit(0xa9,179)
    p.label('coin_oam');p.emit(0x8d,32,2,0xa9,72,0x8d,33,2,0xa9,0,0x8d,34,2,0xa5,13,0x8d,35,2)
    p.emit(0xa9,0,0x8d,3,0x20,0xa9,2,0x8d,0x14,0x40)
    # Score is written in both nametables; scroll registers restore afterwards.
    for high in (0x20,0x24):
        p.emit(0x2c,2,0x20,0xa9,high,0x8d,6,0x20,0xa9,0x89,0x8d,6,0x20,
               0xa5,10,0x18,0x69,48,0x8d,7,0x20,0xa5,5,0x18,0x69,48,0x8d,7,0x20)
    p.emit(0xa5,4,0x85,8);p.absolute(0x20,'scroll');p.emit(0x68,0xa8,0x68,0xaa,0x68,0x40)
    p.label('scroll');p.emit(0x2c,2,0x20,0xa5,7,0x09,0x80,0x8d,0,0x20,
                           0xa5,3,0x8d,5,0x20,0xa9,0,0x8d,5,0x20,0x60)
    p.label('irq');p.emit(0x40)
    for screen in range(2):
        data=background(screen)
        for page in range(4):p.label(f'bg{screen}_{page}');p.emit(*data[page*256:(page+1)*256])
    code=p.finish();assert len(code)<0x3ffa
    prg=bytearray([0xea])*16384;prg[:len(code)]=code
    for off,name in ((0x3ffa,'nmi'),(0x3ffc,'reset'),(0x3ffe,'irq')):prg[off:off+2]=p.labels[name].to_bytes(2,'little')
    art=bytearray(8192)
    def tile(number,rows):
        for y,row in enumerate(rows):
            for x,pixel in enumerate(row):
                for bit in range(2):
                    if pixel&(1<<bit):art[number*16+bit*8+y]|=1<<(7-x)
    tile(1,[[1 if y<2 else 2 for x in range(8)] for y in range(8)])
    tile(2,[[2 if (x+y)%5 else 3 for x in range(8)] for y in range(8)])
    tile(3,[[1]*8 for y in range(8)]);tile(4,[[3 if 1<=y<=6 else 0 for x in range(8)] for y in range(8)])
    tile(5,[[1 if x in (3,4) or (y==3 and 1<x<6) else 0 for x in range(8)] for y in range(8)])
    for char,rows in FONT.items():tile(16+ord(char)-65,[[3 if row&(1<<(7-x)) else 0 for x in range(8)] for row in [r<<2 for r in rows]+[0]])
    for n,rows in enumerate(DIGITS):tile(48+n,[[3 if (row<<2)&(1<<(7-x)) else 0 for x in range(8)] for row in rows+[0]])
    # Our blue explorer wears a yellow helmet. Redesignable original bitmaps.
    player=["0000111111110000","0001111111111000","0011111111111100","0013333333333100",
            "0013322222233100","0013323223233100","0003322222233000","0000333333330000",
            "0002222222220000","0022222222222200","0222222222222220","0022222222222200",
            "0002222222220000","0002220002220000","0033330003333000","0033330003333000"]
    enemy=["0000333333330000","0003311111133000","0031111111113300","0031133113313300",
           "0031133113313300","0031111111113300","0003333333333000","0000033333300000",
           "0003311111330000","0031111111113000","0031111111113000","0003333333330000",
           "0003300000330000","0033300000333000","0333300000333300","0000000000000000"]
    for base,shape in ((64,player),(68,enemy)):
        for q in range(4):tile(base+q,[[int(v) for v in row[q%2*8:q%2*8+8]] for row in shape[q//2*8:q//2*8+8]])
    tile(72,[[int(c) for c in row] for row in ["00111100","01133110","11311311","11311311","11311311","11311311","01133110","00111100"]])
    return b'NES\x1a'+bytes((1,1,1,0))+bytes(8)+prg+art

def generate(out):
    out.mkdir(parents=True,exist_ok=True);rom=make_rom();path=out/'game.nes';path.write_bytes(rom)
    return {'rom':str(path),'bytes':len(rom),'sha256':hashlib.sha256(rom).hexdigest(),'license':'MIT','original':True}
if __name__=='__main__':
    cli=argparse.ArgumentParser();cli.add_argument('--out',type=Path,required=True)
    print(json.dumps(generate(cli.parse_args().out)))

