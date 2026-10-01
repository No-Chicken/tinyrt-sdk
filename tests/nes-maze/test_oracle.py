"""Independent expected nametable + CHR pixel oracle, with optional real captures."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, struct, unittest, zlib
SDK=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('maze_rom',SDK/'examples/nes-maze/make_rom.py')
rom_source=importlib.util.module_from_spec(spec);spec.loader.exec_module(rom_source)
GRID=('############','#....#.....#','#.##.#.###.#','#..#...#...#',
      '##.#####.#.#','#..#.....#.#','#.##.###.#.#','#....#...#.#',
      '#.####.#..G#','############')
CASES={'title':(0,1,1),'play':(1,3,1),'won':(2,10,8),'restart':(1,1,1)}
CAPTURES=None

def expected_frame(state,px,py):
    rom,_=rom_source.make_rom();chr_data=rom[16+16384:]
    tiles=bytearray(960)
    def text(x,y,value):
        for i,ch in enumerate(value):tiles[y*32+x+i]=0 if ch==' ' else ord(ch)-ord('A')+16
    text(11,1,'TINY MAZE')
    text(8,26,('  PRESS START   ',' FIND THE EXIT  ','    YOU WIN     ')[state])
    for y,row in enumerate(GRID):
        for x,ch in enumerate(row):
            tile=6 if (x,y)==(px,py) else 1 if ch=='#' else 2 if ch=='G' else 0
            for dy in range(2):
                for dx in range(2):tiles[(5+y*2+dy)*32+4+x*2+dx]=tile+(dy*2+dx if tile>=2 else 0)
    palette=(0x0000,0x039d,0x4ee9,0xffff)
    pixels=bytearray()
    for y in range(240):
        for x in range(256):
            address=tiles[(y//8)*32+x//8]*16+y%8
            shift=7-x%8
            color=((chr_data[address]>>shift)&1)|(((chr_data[address+8]>>shift)&1)<<1)
            pixels.extend(struct.pack('<H',palette[color]))
    return bytes(pixels)

class MazeTests(unittest.TestCase):
    def test_original_deterministic_rom_layout(self):
        rom,labels=rom_source.make_rom();self.assertEqual(rom,rom_source.make_rom()[0])
        self.assertEqual(len(rom),24592);self.assertEqual(rom[:16],b'NES\x1a'+bytes((1,1))+bytes(10))
        self.assertEqual(tuple(rom_source.MAP),GRID)
        for pos,name in ((0x3ffa,'nmi'),(0x3ffc,'reset'),(0x3ffe,'irq')):
            self.assertEqual(struct.unpack_from('<H',rom,16+pos)[0],labels[name])
        self.assertEqual(rom[16:100].count(bytes.fromhex('2c 02 20 10 fb')),2)
        self.assertIn(bytes.fromhex('ad 16 40 4a 26 04'),rom) # ROM reads NES controller bits.
        for text in ('TINY MAZE','PRESS START','FIND THE EXIT','YOU WIN'):
            self.assertTrue(set(text)-{' '} <= rom_source.FONT.keys())

    def test_twenty_move_route_and_wall(self):
        x,y=1,1;self.assertEqual(GRID[y-1][x],'#')
        for step in 'RRRDDRRUURRRRDDDDDDD':
            dx,dy={'R':(1,0),'L':(-1,0),'D':(0,1),'U':(0,-1)}[step]
            x,y=x+dx,y+dy;self.assertNotEqual(GRID[y][x],'#')
        self.assertEqual((x,y),(10,8));self.assertEqual(GRID[y][x],'G')

    def test_actual_cpu_ppu_pixels(self):
        if CAPTURES is None:self.skipTest('pass --captures to verify native and WAMR output')
        for name,state in CASES.items():
            expected=expected_frame(*state)
            for backend in ('native','wamr'):
                with self.subTest(backend=backend,screen=name):
                    actual=(CAPTURES/f'{backend}-{name}.rgb565').read_bytes()
                    self.assertEqual(len(actual),122880)
                    if actual!=expected:
                        pos=next(i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b)//2
                        self.fail(f'pixel mismatch x={pos%256} y={pos//256}')

def board_oracle(directory):
    path='RRRDDRRUURRRRDDDDDDD';x,y=1,1
    entries=[]
    def record(label,state,px,py,action):
        expected=expected_frame(state,px,py)
        actual=(directory/f'wamr-{label}.rgb565').read_bytes()
        if actual!=expected:raise AssertionError(f'WAMR frame mismatch: {label}')
        entries.append({'label':label,'touch':action,'state':state,'player':[px,py],
                        'pixel_bytes':len(actual),'crc32':f'{zlib.crc32(actual):08x}'})
    record('title',0,1,1,None)
    record('start',1,1,1,[326,365])
    record('wall',1,1,1,[233,365])
    record('right-once',1,2,1,[283,405])
    record('play',1,3,1,[283,405])
    record('route-00',1,1,1,[326,365])
    for i,step in enumerate(path,1):
        dx,dy={'R':(1,0),'L':(-1,0),'D':(0,1),'U':(0,-1)}[step]
        x,y=x+dx,y+dy
        coordinate={'R':[283,405],'L':[183,405],'D':[233,405],'U':[233,365]}[step]
        record(f'route-{i:02}',2 if i==len(path) else 1,x,y,coordinate)
    record('restart',1,1,1,[326,365])
    result={'scope':'Actual desktop WAMR RGB565LE captures, independently compared to the ROM nametable/CHR oracle. Board measurements remain separate.',
            'wait':'Wait for at least three new complete pixel frames between taps; first wait for the title CRC.',
            'crc':'Standard CRC32 over all 122880 bytes of frame.pixels, excluding host drawing commands.',
            'steps':entries}
    (directory/'board-oracle.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')

def screenshots(directory):
    from PIL import Image
    result={}
    for name in CASES:
        raw=(directory/f'native-{name}.rgb565').read_bytes()
        rgb=bytearray()
        for value, in struct.iter_unpack('<H',raw):
            r=(value>>11)&31;g=(value>>5)&63;b=value&31
            rgb.extend(((r<<3)|(r>>2),(g<<2)|(g>>4),(b<<3)|(b>>2)))
        Image.frombytes('RGB',(256,240),bytes(rgb)).resize((768,720),Image.Resampling.NEAREST).save(directory/f'{name}.png')
        result[name]={'crc32':f'{zlib.crc32(raw):08x}','sha256':hashlib.sha256(raw).hexdigest()}
    (directory/'pixels.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--captures',type=Path);p.add_argument('--png',action='store_true');args,unknown=p.parse_known_args()
    CAPTURES=args.captures
    result=unittest.main(argv=['test_oracle.py']+unknown,exit=False).result
    if not result.wasSuccessful():raise SystemExit(1)
    if args.png:
        if CAPTURES is None:p.error('--png requires --captures')
        screenshots(CAPTURES)
        board_oracle(CAPTURES)
