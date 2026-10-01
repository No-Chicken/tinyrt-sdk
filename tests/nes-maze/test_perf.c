/* Measure actual interpreter dispatch, not time thresholds that depend on the host. */
#include "../../examples/nes-maze/engine.c"
#include <stdio.h>
#include <windows.h>
static unsigned long long instructions, idle_instructions;
static LARGE_INTEGER frequency, started[NES_PROF_COUNT];
static double elapsed[NES_PROF_COUNT];
static unsigned region_calls[NES_PROF_COUNT];
void nes_test_trace(nes_t *n,uint16_t pc) {
    instructions++;
    if(pc>=0x8000u && pc<0xfffeu) {
        const uint8_t *p=n->nes_rom.prg_rom;
        if(p[(pc-0x8000u)&0x3fffu]==0x4c &&
           p[(pc-0x7fffu)&0x3fffu]==(uint8_t)pc &&
           p[(pc-0x7ffeu)&0x3fffu]==(uint8_t)(pc>>8))idle_instructions++;
    }
}
void nes_test_rlog(nes_t*n,uint16_t a,uint8_t v,uint16_t p){(void)n;(void)a;(void)v;(void)p;}
void nes_test_wlog(nes_t*n,uint16_t a,uint8_t v,uint16_t p){(void)n;(void)a;(void)v;(void)p;}
void nes_test_profile_region_begin(nes_t*n,int r){(void)n;region_calls[r]++;QueryPerformanceCounter(&started[r]);}
void nes_test_profile_region_end(nes_t*n,int r){
    (void)n;LARGE_INTEGER end;QueryPerformanceCounter(&end);
    elapsed[r]+=(double)(end.QuadPart-started[r].QuadPart)/(double)frequency.QuadPart;
}
int main(void) {
    QueryPerformanceFrequency(&frequency);maze_start();
    while(maze_status().frame<8)maze_step();
    instructions=idle_instructions=0;
    for(unsigned r=0;r<NES_PROF_COUNT;r++){elapsed[r]=0;region_calls[r]=0;}
    unsigned nmi=maze_status().nmi;
    while(maze_status().frame<68)maze_step();
    printf("PERF frames=60 instructions=%llu idle_jmp=%llu cpu_ms=%.3f background_ms=%.3f background_lines=%u nmi=%u\n",
           instructions,idle_instructions,elapsed[NES_PROF_CPU]*1000,elapsed[NES_PROF_BG]*1000,region_calls[NES_PROF_BG],maze_status().nmi-nmi);
    if(maze_status().nmi!=((nmi+60)&255u))return 1;
    /* The NMI's final slice can still return to idle inside the interpreter. */
    if(idle_instructions*2>instructions){puts("FAIL: inert JMP dispatch dominates real game work");return 1;}
    if(region_calls[NES_PROF_BG]>240){puts("FAIL: unchanged background is redrawn every frame");return 1;}
    puts("PERF PASS");return 0;
}
