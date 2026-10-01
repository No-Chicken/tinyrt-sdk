"""Original MIT NROM game, assembler, map and art. Gameplay runs on the 6502.
Zero page: state,x,y,previous_pad,held_pad,moves,nmi,spare,next_x,next_y,
paint_x,paint_y,address_lo,address_hi,tile,spare,edges,displayed_state.
States: 0 title, 1 playing, 2 won. Controller bits: R,L,D,U,Start,Select,B,A.
"""
from pathlib import Path
import argparse, hashlib, json
MAP = (
    '############', '#....#.....#', '#.##.#.###.#', '#..#...#...#',
    '##.#####.#.#', '#..#.....#.#', '#.##.###.#.#', '#....#...#.#',
    '#.####.#..G#', '############',
)
START, GOAL = (1,1), (10,8)
FONT = {
 'A':(14,17,17,31,17,17,17), 'D':(30,17,17,17,17,17,30),
 'E':(31,16,16,30,16,16,31), 'F':(31,16,16,30,16,16,16),
 'H':(17,17,17,31,17,17,17), 'I':(14,4,4,4,4,4,14),
 'M':(17,27,21,21,17,17,17), 'N':(17,25,25,21,19,19,17),
 'O':(14,17,17,17,17,17,14), 'P':(30,17,17,30,16,16,16),
 'R':(30,17,17,30,20,18,17), 'S':(15,16,16,14,1,1,30),
 'T':(31,4,4,4,4,4,4), 'U':(17,17,17,17,17,17,14),
 'W':(17,17,17,21,21,27,17), 'X':(17,17,10,4,10,17,17),
 'Y':(17,17,10,4,4,4,4), 'Z':(31,1,2,4,8,16,31),
}
class Program:
 def __init__(self): self.data=bytearray();self.labels={};self.fixups=[]
 def emit(self,*values): self.data.extend(values)
 def label(self,name):
  assert name not in self.labels
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
    delta=address-(0x8000+pos+1);assert -128<=delta<=127,name
    self.data[pos]=delta&255
   else:self.data[pos:pos+2]=address.to_bytes(2,'little')
  return self.data

def text_tiles(text):
 return bytes(0 if ch==' ' else 16+ord(ch)-ord('A') for ch in text)

def background():
 data=bytearray(1024);title=text_tiles('TINY MAZE')
 data[43:43+len(title)]=title
 for y,row in enumerate(MAP):
  for x,ch in enumerate(row):
   tile=1 if ch=='#' else 2 if ch=='G' else 0
   if (x,y)==START:tile=6
   pos=(5+y*2)*32+4+x*2
   data[pos:pos+2]=bytes([tile,tile+1] if tile>=2 else [tile,tile])
   data[pos+32:pos+34]=bytes([tile+2,tile+3] if tile>=2 else [tile,tile])
 return data

def make_rom():
 p=Program();p.label('reset')
 p.emit(0x78,0xd8,0xa2,0xff,0x9a) # SEI; CLD; LDX #$ff; TXS
 p.emit(0xa9,0,0x8d,0,0x20,0x8d,1,0x20,0xa2,31)
 p.label('clear_zp');p.emit(0x95,0,0xca);p.branch(0x10,'clear_zp')
 p.emit(0xa9,1,0x85,1,0x85,2,0xa9,255,0x85,17)
 # Wait two vblanks before touching PPU memory, as a standalone NES ROM does.
 p.emit(0x2c,2,0x20)
 for wait in ('warmup_one','warmup_two'):
  p.label(wait);p.emit(0x2c,2,0x20);p.branch(0x10,wait)
 # Upload 1024 nametable/attribute bytes with rendering disabled.
 p.emit(0x2c,2,0x20,0xa9,0x20,0x8d,6,0x20,0xa9,0,0x8d,6,0x20)
 for page in range(4):
  p.emit(0xa2,0);p.label(f'upload{page}');p.absolute(0xbd,f'background{page}')
  p.emit(0x8d,7,0x20,0xe8);p.branch(0xd0,f'upload{page}')
 p.emit(0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20)
 for color in (0x0f,0x11,0x2a,0x30):p.emit(0xa9,color,0x8d,7,0x20)
 p.absolute(0x20,'scroll');p.emit(0xa9,0x80,0x8d,0,0x20,0xa9,0x0a,0x8d,1,0x20)
 p.label('idle');p.absolute(0x4c,'idle')
 p.label('nmi');p.emit(0x48,0x8a,0x48,0x98,0x48,0xe6,6)
 p.emit(0xa9,1,0x8d,0x16,0x40,0xa9,0,0x8d,0x16,0x40,0x85,4,0xa2,8)
 p.label('controller');p.emit(0xad,0x16,0x40,0x4a,0x26,4,0xca);p.branch(0xd0,'controller')
 p.emit(0xa5,3,0x49,255,0x25,4,0x85,16,0xa5,4,0x85,3)
 p.emit(0xa5,16,0x29,16);p.when(0xd0,'restart')
 p.emit(0xa5,0,0xc9,1);p.when(0xd0,'banner')
 p.emit(0xa5,1,0x85,8,0xa5,2,0x85,9)
 for name,bit,opcode,address in (('up',8,0xc6,9),('down',4,0xe6,9),('left',2,0xc6,8),('right',1,0xe6,8)):
  p.emit(0xa5,16,0x29,bit);p.branch(0xf0,f'not_{name}')
  p.emit(opcode,address);p.absolute(0x4c,'try_move');p.label(f'not_{name}')
 p.absolute(0x4c,'banner')
 p.label('try_move');p.emit(0xa6,9);p.absolute(0xbd,'row_index');p.emit(0x18,0x65,8,0xaa)
 p.absolute(0xbd,'map');p.emit(0xc9,1);p.when(0xf0,'banner')
 p.absolute(0x20,'restore_cell');p.emit(0xa5,8,0x85,1,0xa5,9,0x85,2,0xe6,5)
 p.absolute(0x20,'player_cell')
 p.emit(0xa5,1,0xc9,GOAL[0]);p.when(0xd0,'banner')
 p.emit(0xa5,2,0xc9,GOAL[1]);p.when(0xd0,'banner')
 p.emit(0xa9,2,0x85,0);p.absolute(0x4c,'banner')
 p.label('restart');p.absolute(0x20,'restore_cell')
 p.emit(0xa9,1,0x85,0,0x85,1,0x85,2,0xa9,0,0x85,5);p.absolute(0x20,'player_cell')
 p.label('banner');p.emit(0xa5,0,0xc5,17);p.branch(0xf0,'nmi_done')
 p.emit(0x85,17,0x0a,0x0a,0x0a,0x0a,0xaa)
 p.emit(0x2c,2,0x20,0xa9,0x23,0x8d,6,0x20,0xa9,0x48,0x8d,6,0x20,0xa0,16)
 p.label('banner_loop');p.absolute(0xbd,'messages');p.emit(0x8d,7,0x20,0xe8,0x88);p.branch(0xd0,'banner_loop')
 p.label('nmi_done');p.absolute(0x20,'scroll');p.emit(0x68,0xa8,0x68,0xaa,0x68,0x40)
 p.label('restore_cell');p.emit(0xa6,2);p.absolute(0xbd,'row_index');p.emit(0x18,0x65,1,0xaa)
 p.absolute(0xbd,'map');p.emit(0x85,14);p.absolute(0x4c,'current_cell')
 p.label('player_cell');p.emit(0xa9,6,0x85,14)
 p.label('current_cell');p.emit(0xa5,1,0x85,10,0xa5,2,0x85,11)
 p.label('paint_cell');p.emit(0xa6,11);p.absolute(0xbd,'row_low');p.emit(0x85,12)
 p.absolute(0xbd,'row_high');p.emit(0x85,13,0xa5,10,0x0a,0x18,0x65,12,0x85,12)
 p.branch(0x90,'paint_top');p.emit(0xe6,13)
 p.label('paint_top');p.absolute(0x20,'two_tiles')
 p.emit(0xa5,12,0x18,0x69,32,0x85,12);p.branch(0x90,'paint_bottom');p.emit(0xe6,13)
 p.label('paint_bottom');p.absolute(0x4c,'two_tiles')
 p.label('two_tiles');p.emit(0x2c,2,0x20,0xa5,13,0x8d,6,0x20,0xa5,12,0x8d,6,0x20,
                           0xa5,14,0x8d,7,0x20,0xc9,0)
 p.branch(0xf0,'paint_zero');p.emit(0x18,0x69,1,0x8d,7,0x20,0x18,0x69,1,0x85,14,0x60)
 p.label('paint_zero');p.emit(0x8d,7,0x20,0x60)
 p.label('scroll');p.emit(0x2c,2,0x20,0xa9,0,0x8d,5,0x20,0x8d,5,0x20,0x60)
 p.label('irq');p.emit(0x40)
 p.label('row_index');p.emit(*(y*12 for y in range(10)))
 p.label('row_low');p.emit(*((0x20a4+y*64)&255 for y in range(10)))
 p.label('row_high');p.emit(*((0x20a4+y*64)>>8 for y in range(10)))
 p.label('map');p.emit(*(1 if ch=='#' else 2 if ch=='G' else 0 for row in MAP for ch in row))
 p.label('messages')
 for text in ('  PRESS START   ',' FIND THE EXIT  ','    YOU WIN     '):
  assert len(text)==16;p.emit(*text_tiles(text))
 for page in range(4):p.label(f'background{page}');p.emit(*background()[page*256:(page+1)*256])
 program=p.finish();assert len(program)<0x3ffa
 prg=bytearray([0xea])*16384;prg[:len(program)]=program
 for pos,name in ((0x3ffa,'nmi'),(0x3ffc,'reset'),(0x3ffe,'irq')):prg[pos:pos+2]=p.labels[name].to_bytes(2,'little')
 chr_data=bytearray(8192)
 # A single 16x16 face and exit door, split into four ordinary background tiles.
 chr_data[16:24]=bytes((255,129,129,255,136,136,136,255))
 for base,rows,color in (
  (2,(0,0x1ff8,0x1008,0x1008,0x1048,0x1028,0x1ffc,0x1028,0x1048,0x1008,0x1008,0x1008,0x1008,0x1ff8,0,0),2),
  (6,(0,0x0ff0,0x3ffc,0x3ffc,0x7ffe,0x7ffe,0x733e,0x733e,0x7ffe,0x77ee,0x381c,0x1ff8,0x0ff0,0x0420,0x0c30,0),3),
 ):
  for quadrant in range(4):
   part=[(row>>(8 if quadrant%2==0 else 0))&255 for row in rows[(quadrant//2)*8:(quadrant//2+1)*8]]
   for bit in range(2):chr_data[(base+quadrant)*16+bit*8:(base+quadrant)*16+bit*8+8]=bytes(part if color&(1<<bit) else [0]*8)
 for ch,rows in FONT.items():
  tile=16+ord(ch)-ord('A');pixels=bytes([value<<2 for value in rows]+[0]);chr_data[tile*16:tile*16+16]=pixels*2
 return b'NES\x1a'+bytes((1,1))+bytes(10)+prg+chr_data,p.labels

def generate(out):
 out.mkdir(parents=True,exist_ok=True);rom,labels=make_rom();(out/'maze.nes').write_bytes(rom)
 rows=[','.join(f'0x{v:02x}' for v in rom[i:i+32]) for i in range(0,len(rom),32)]
 (out/'rom_data.h').write_text('#include <stdint.h>\nstatic const uint8_t maze_rom[] = {\n'+',\n'.join(rows)+'\n};\n',encoding='utf-8')
 (out/'rom.json').write_text(json.dumps({'bytes':len(rom),'sha256':hashlib.sha256(rom).hexdigest(),'labels':labels},indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--out',type=Path,default=Path(__file__).parent/'build');generate(parser.parse_args().out)
