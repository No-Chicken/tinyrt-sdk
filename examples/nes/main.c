#include "tinyrt.h"
#include "engine.h"
static uint32_t a_latched;
static char text[64];
static unsigned used;
static void append(const char *s) {while(*s&&used<63)text[used++]=*s++;}
static void number(uint32_t n) {
    char digits[10];unsigned len=0;
    do{digits[len++]=(char)('0'+n%10u);n/=10u;}while(n);
    while(len&&used<63)text[used++]=digits[--len];
}
static int line_at(int y) {text[used]=0;return draw_text(12,y,text,used,0xe8eef2);}
int32_t tinyrt_init(int32_t width,int32_t height) {
    if(width<160||height<220)return -1;
    a_latched=0;n1_start();return 0;
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    (void)x;(void)y;(void)arg;
    if(kind==TINYRT_CLOCK_EVENT){n1_step();return 0;}
    if(kind==TINYRT_TOUCH_RELEASE){a_latched^=1u;n1_button(a_latched);return 0;}
    return -1;
}
int32_t tinyrt_render(void) {
    n1_status_t s=n1_status();
    if(draw_clear(0x122029))return -1;
    used=0;append("NES N1 - CPU/PPU experiment");if(line_at(16))return -1;
    used=0;append("FRAME ");number(s.frame);if(line_at(52))return -1;
    used=0;append("CRC ");
    for(int shift=28;shift>=0;shift-=4)text[used++]="0123456789ABCDEF"[(s.crc>>shift)&15];
    if(line_at(88))return -1;
    used=0;append("NMI ");number(s.nmi);append(" A ");number(s.button);append(" SIG ");number(s.signature);if(line_at(124))return -1;
    used=0;append("READY ");number(s.ready);append(" LINE ");number(s.scanline);if(line_at(160))return -1;
    used=0;append("Tap toggles A; no video/audio");return line_at(196);
}
