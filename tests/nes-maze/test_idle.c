/* Differential CPU state check against the unmodified upstream interpreter. */
#include "../../examples/nes-maze/engine.c"
#include <stdio.h>
#include <string.h>
static nes_t expected;
static unsigned checks;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL line=%d %s\n",__LINE__,#c);return 1;}}while(0)
static unsigned mapper_ticks;
static void tick_mapper(nes_t*n,uint16_t ticks){(void)n;mapper_ticks+=ticks;}
static uint8_t read_mapper(nes_t*n,uint16_t address){(void)n;(void)address;return 0xea;}
static int compare(uint16_t pc,uint16_t carried,uint16_t ticks,unsigned interrupt) {
    memset(&machine,0,sizeof(machine));nrom_attach();
    machine.nes_cpu.PC=pc;machine.nes_cpu.cycles=carried;
    machine.nes_cpu.opcode=0xab;machine.nes_cpu.write_burst=2;
    if(interrupt==1)machine.nes_cpu.irq_nmi=1;
    if(interrupt==2)machine.nes_cpu.irq_nmi_delay=1;
    if(interrupt==3){machine.nes_cpu.irq_pending=1;machine.nes_cpu.I=0;}
    if(interrupt==4)machine.nes_mapper.mapper_cpu_clock=tick_mapper;
    if(interrupt==5)machine.nes_mapper.mapper_read_prg=read_mapper;
    expected=machine;nes_ppu_init(&expected);
    mapper_ticks=0;nes_opcode(&expected,ticks);unsigned wanted_ticks=mapper_ticks;
    mapper_ticks=0;maze_opcode(&machine,ticks);
    CHECK(!memcmp(&machine.nes_cpu,&expected.nes_cpu,sizeof(machine.nes_cpu)));
    CHECK(mapper_ticks==wanted_ticks);
    CHECK(!memcmp(machine.nes_ppu.ppu_vram,expected.nes_ppu.ppu_vram,sizeof(machine.nes_ppu.ppu_vram)));
    return 0;
}
int main(void) {
    uint16_t idle=0;
    for(unsigned offset=0;offset<16382;offset++) {
        unsigned pc=0x8000u+offset;
        if(maze_rom[16+offset]==0x4c && maze_rom[17+offset]==(uint8_t)pc && maze_rom[18+offset]==(uint8_t)(pc>>8)){idle=(uint16_t)pc;break;}
    }
    CHECK(idle>=0x8000u);
    for(unsigned irq=0;irq<6;irq++)for(unsigned carry=0;carry<8;carry++)for(unsigned ticks=0;ticks<=114;ticks++)
        if(compare(idle,(uint16_t)carry,(uint16_t)ticks,irq))return 1;
    /* Non-idle ROM code and the mirrored bank must remain ordinary 6502 code. */
    for(unsigned ticks=1;ticks<=114;ticks++) {
        if(compare(0x8000,0,(uint16_t)ticks,0)||compare((uint16_t)(idle+0x4000),0,(uint16_t)ticks,0))return 1;
    }
    printf("IDLE EQUIVALENCE PASS checks=%u\n",checks);return 0;
}
