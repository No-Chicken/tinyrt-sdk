/* Breaks caught: no progress; resets between slices; wrong CPU/NMI/input;
 * altered PPU pixels; CRC published before its corresponding frame. */
#include "engine.h"
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do {checks++;if(!(x)){printf("FAIL line=%d: %s\n",__LINE__,#x);return 1;}}while(0)
static int until_frame(unsigned frame) {
    for(unsigned i=0;i<12000;i++){if(n1_status().frame>=frame)return 1;n1_step();}return 0;
}
static int check_pixels(unsigned color) {
    const uint16_t *p=n1_pixels();
    for(unsigned y=0;y<240;y++)for(unsigned x=0;x<256;x++)
        if(p[y*256+x]!=(64<=x&&x<128&&64<=y&&y<128?color:0))return 0;
    return 1;
}
int main(void) {
    n1_start();CHECK(until_frame(8));n1_status_t s=n1_status();
    CHECK(s.ready==1&&s.frame==8&&s.nmi==8&&s.signature==0x33);
    CHECK(s.crc==0x7fdd3027);CHECK(check_pixels(0x20d1));
    for(unsigned i=0;i<240;i++)n1_step();
    CHECK(n1_status().frame==8&&n1_status().crc==s.crc);
    CHECK(until_frame(9));CHECK(n1_status().crc==0xee67c189);CHECK(check_pixels(0x3dff));
    n1_button(1);CHECK(until_frame(11));
    CHECK(n1_status().button==1&&n1_status().crc==0x7ac3eb7f);CHECK(check_pixels(0xffff));
    n1_button(0);CHECK(until_frame(13));
    CHECK(n1_status().button==0&&n1_status().crc==0xee67c189);CHECK(check_pixels(0x3dff));
    n1_start();CHECK(until_frame(8));s=n1_status();
    CHECK(s.nmi==8&&s.signature==0x33&&s.crc==0x7fdd3027);
    printf("NATIVE checks=%u frame=%u crc=%08X nmi=%u signature=%u\n",checks,s.frame,s.crc,s.nmi,s.signature);
    return 0;
}
