/* Integration oracle: execute original 6502 game logic and PPU, not a game clone. */
#include "engine.h"
#include "tinyrt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t image[40976];static uint32_t image_size;
int32_t asset_read(uint32_t offset,void *dst,uint32_t count) {
    if(count>4096||offset>image_size||count>image_size-offset)return -1;
    memcpy(dst,image+offset,count);return (int32_t)count;
}
static unsigned checks;
#define CHECK(condition) do{checks++;if(!(condition)){fprintf(stderr,"FAIL %d %s x=%u y=%u frame=%u\n",__LINE__,#condition,nes_player_status().x,nes_player_status().y,nes_player_status().frame);return 1;}}while(0)
static int frames(unsigned count) {
    unsigned target=nes_player_status().frame+count;
    for(unsigned i=0;i<100000&&nes_player_status().frame<target;i++)if(nes_player_step()==2)return 0;
    return nes_player_status().frame==target;
}
static uint32_t crc(void) {
    uint32_t value=2166136261u;const uint16_t *p=nes_player_pixels();
    for(unsigned i=0;i<256*240;i++){value^=p[i];value*=16777619u;}return value;
}
int main(int argc,char**argv) {
    if(argc<2)return 2;
    FILE*f=fopen(argv[1],"rb");if(!f)return 2;
    image_size=(uint32_t)fread(image,1,sizeof(image),f);fclose(f);
    if(argc>2 && !strcmp(argv[2],"--trace")) {
        if(nes_player_start())return 2;
        for(unsigned frame=0;frame<240;frame++) {
            unsigned pad=frame<12?0:frame<48?NES_PAD_RIGHT:frame<56?NES_PAD_RIGHT|NES_PAD_A:
                         frame<85?0:frame<145?NES_PAD_LEFT:frame<170?NES_PAD_START:
                         (frame&7)==0?NES_PAD_A:NES_PAD_RIGHT;
            nes_player_buttons(pad);if(!frames(1))return 2;
            printf("%u %08x %08x\n",frame,nes_player_state_hash(),crc());
        }
        return 0;
    }
    CHECK(image_size==24592&&nes_player_start()==0&&frames(8));
    CHECK(nes_player_status().ready&&nes_player_status().y==208&&nes_player_status().x==0);
    uint32_t first=crc();nes_player_buttons(NES_PAD_RIGHT);CHECK(frames(20));
    CHECK(nes_player_status().x==40&&crc()!=first);
    nes_player_buttons(NES_PAD_RIGHT|NES_PAD_A);CHECK(frames(10));
    CHECK(nes_player_status().y<190&&nes_player_status().x==60);
    nes_player_buttons(0);CHECK(frames(25));CHECK(nes_player_status().y==208);
    CHECK(nes_player_status().moves==1); /* Coin collection happened in the ROM. */
    CHECK(nes_player_status().x==60);first=crc();CHECK(frames(12));
    CHECK(nes_player_status().x==60&&crc()!=first); /* Enemy keeps patrolling. */
    nes_player_buttons(NES_PAD_START);CHECK(frames(2));nes_player_buttons(0);CHECK(frames(1));
    CHECK(nes_player_status().x==0&&nes_player_status().moves==0);
    image[6]=0x10;CHECK(nes_player_start()==-1);image[6]=1;
    image[7]=8;CHECK(nes_player_start()==-1);image[7]=0;
    image[4]=3;CHECK(nes_player_start()==-1);image[4]=1;
    image_size=24;CHECK(nes_player_start()==0&&nes_player_step()==2);
    printf("NES_PLAYER checks=%u; scroll, run+jump, coin, patrol, reset, invalid/truncated ROM passed\n",checks);
    return 0;
}

