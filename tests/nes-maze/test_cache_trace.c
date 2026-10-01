/* Compare every completed CPU/PPU frame with background caching disabled. */
#include "../../examples/nes-maze/engine.c"
#include <stdio.h>
#include <stddef.h>
static uint32_t hash(const void *data,size_t size) {
    const unsigned char *p=data;uint32_t h=2166136261u;
    while(size--)h=(h^*p++)*16777619u;
    return h;
}
static void frame(void) {
    uint32_t target=maze_status().frame+1;
    while(maze_status().frame<target)maze_step();
    printf("%u %08x %08x %08x %04x %04x\n",frames,
           hash(machine.nes_draw_data,sizeof(machine.nes_draw_data)),
           hash(&machine.nes_cpu,offsetof(nes_cpu_t,prg_banks)),
           hash(machine.nes_ppu.ppu_vram,sizeof(machine.nes_ppu.ppu_vram)),
           machine.nes_ppu.v_reg,machine.nes_ppu.t_reg);
}
static void write_ppu(uint16_t address,uint8_t value) {
    nes_read_ppu_register(&machine,0x2002);
    nes_write_ppu_register(&machine,0x2006,(uint8_t)(address>>8));
    nes_write_ppu_register(&machine,0x2006,(uint8_t)address);
    nes_write_ppu_register(&machine,0x2007,value);
    machine.nes_ppu.v_reg=machine.nes_ppu.t_reg=0;
}
int main(void) {
    maze_start();for(unsigned i=0;i<8;i++)frame();
    const unsigned keys[]={MAZE_START,MAZE_UP,MAZE_RIGHT,MAZE_RIGHT,MAZE_DOWN,MAZE_START};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
        maze_buttons(keys[i]);frame();maze_buttons(0);frame();frame();
    }
    /* Change real PPU inputs one at a time; the ROM does not know these fixtures. */
    write_ppu(0x2045,1);frame();frame();
    write_ppu(0x23c0,0x55);frame();frame();
    write_ppu(0x3f01,0x30);frame();frame();
    nes_write_ppu_register(&machine,0x2001,0x08);frame();frame();
    nes_write_ppu_register(&machine,0x2001,0);frame();frame();
    nes_write_ppu_register(&machine,0x2001,0x0a);frame();frame();
    /* Fine scroll and sprites force the ordinary renderer, then invalidate reuse. */
    machine.nes_ppu.x=3;frame();frame();
    machine.nes_ppu.x=0;frame();frame();
    nes_write_ppu_register(&machine,0x2001,0x1e);frame();frame();
    nes_write_ppu_register(&machine,0x2001,0x0a);frame();frame();
    maze_start();for(unsigned i=0;i<8;i++)frame();
    return 0;
}
