"""Build the original TinyRT N1 test ROM. No game ROM or assembler download."""
from pathlib import Path
import argparse, hashlib, json
class Program:
    def __init__(self):self.data=bytearray();self.labels={};self.fixups=[]
    def emit(self,*values):self.data.extend(values)
    def label(self,name):self.labels[name]=0x8000+len(self.data)
    def jump(self,op,name):self.emit(op,0,0);self.fixups.append((len(self.data)-2,name,False))
    def branch(self,op,name):self.emit(op,0);self.fixups.append((len(self.data)-1,name,True))
    def finish(self):
        for pos,name,relative in self.fixups:
            address=self.labels[name]
            if relative:
                offset=address-(0x8000+pos+1)
                assert -128<=offset<=127
                self.data[pos]=offset&255
            else:self.data[pos:pos+2]=address.to_bytes(2,'little')
        return self.data

def make_rom():
    p=Program();p.label('reset')
    p.emit(0x78,0xd8,0xa2,0xff,0x9a) # SEI; CLD; LDX #$ff; TXS
    p.emit(0xa9,0,0x8d,0,0x20,0x8d,1,0x20,0x85,0,0x85,1)
    p.emit(0xa9,0x10,0x18,0x69,0x23,0x85,2) # signature: $10+$23=$33
    p.emit(0x2c,2,0x20,0xa9,0x20,0x8d,6,0x20,0xa9,0,0x8d,6,0x20)
    p.emit(0xa2,4,0xa0,0,0xa9,0);p.label('clear')
    p.emit(0x8d,7,0x20,0xc8);p.branch(0xd0,'clear');p.emit(0xca);p.branch(0xd0,'clear')
    for row in range(8):
        address=0x2108+row*32
        p.emit(0xa9,address>>8,0x8d,6,0x20,0xa9,address&255,0x8d,6,0x20,0xa2,8,0xa9,1)
        p.label(f'block{row}');p.emit(0x8d,7,0x20,0xca);p.branch(0xd0,f'block{row}')
    p.emit(0xa9,0x3f,0x8d,6,0x20,0xa9,0,0x8d,6,0x20,0xa9,0x0f,0x8d,7,0x20,0xa9,1,0x8d,7,0x20)
    p.emit(0xa9,0,0x8d,5,0x20,0x8d,5,0x20,0xa9,0x80,0x8d,0,0x20,0xa9,0x0a,0x8d,1,0x20)
    p.label('idle');p.jump(0x4c,'idle')
    p.label('nmi');p.emit(0x48,0x8a,0x48,0x98,0x48,0xe6,0) # Save A/X/Y; INC $00
    p.emit(0xa9,1,0x8d,0x16,0x40,0xa9,0,0x8d,0x16,0x40,0xad,0x16,0x40,0x29,1,0x85,1)
    p.branch(0xd0,'white');p.emit(0xa5,0,0x29,1);p.branch(0xf0,'sky')
    p.emit(0xa9,1);p.branch(0xd0,'color')
    p.label('sky');p.emit(0xa9,0x21);p.branch(0xd0,'color')
    p.label('white');p.emit(0xa9,0x30)
    p.label('color');p.emit(0x85,3,0x2c,2,0x20,0xa9,0x3f,0x8d,6,0x20,0xa9,1,0x8d,6,0x20,0xa5,3,0x8d,7,0x20)
    p.emit(0xa9,0,0x8d,5,0x20,0x8d,5,0x20,0xa9,0x80,0x8d,0,0x20,0x68,0xa8,0x68,0xaa,0x68,0x40)
    p.label('irq');p.emit(0x40)
    program=p.finish();prg=bytearray([0xea])*16384;prg[:len(program)]=program
    for pos,name in ((0x3ffa,'nmi'),(0x3ffc,'reset'),(0x3ffe,'irq')):prg[pos:pos+2]=p.labels[name].to_bytes(2,'little')
    chr_data=bytearray(8192);chr_data[16:24]=b'\xff'*8 # tile 1: solid color 1, tile 0 transparent
    rom=b'NES\x1a'+bytes((1,1))+bytes(10)+prg+chr_data
    assert len(rom)==24592
    return rom,p.labels

def generate(out):
    out.mkdir(parents=True,exist_ok=True);rom,labels=make_rom()
    (out/'n1.nes').write_bytes(rom)
    lines=[','.join(f'0x{v:02x}' for v in rom[i:i+32]) for i in range(0,len(rom),32)]
    (out/'rom_data.h').write_text('#include <stdint.h>\nstatic const uint8_t n1_rom[] = {\n'+',\n'.join(lines)+'\n};\n')
    (out/'rom.json').write_text(json.dumps({'bytes':len(rom),'sha256':hashlib.sha256(rom).hexdigest(),'labels':labels},indent=2)+'\n')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,default=Path(__file__).parent/'build');generate(p.parse_args().out)
