/* ROM behavior is checked through the real emulated CPU and PPU. */
#include "engine.h"
#include <stdio.h>
#include <stdint.h>
static unsigned checks;
static const char *output;
#define CHECK(condition) do{checks++;if(!(condition)){printf("FAIL line=%d %s state=%u player=%u,%u held=%u frames=%u\n",__LINE__,#condition,maze_status().state,maze_status().x,maze_status().y,maze_status().held,maze_status().frame);return 1;}}while(0)
static int frames(unsigned n){unsigned target=maze_status().frame+n;for(unsigned i=0;i<90000&&maze_status().frame<target;i++)maze_step();return maze_status().frame==target;}
static int press(unsigned key){maze_buttons(key);if(!frames(1))return 0;maze_buttons(0);return frames(2);}
static int dump(const char *name){
 if(!output)return 1;char path[512];snprintf(path,sizeof(path),"%s/native-%s.rgb565",output,name);
 FILE*f=fopen(path,"wb");if(!f)return 0;const uint16_t*p=maze_pixels();
 for(unsigned i=0;i<256*240;i++){unsigned char b[2]={(unsigned char)p[i],(unsigned char)(p[i]>>8)};if(fwrite(b,1,2,f)!=2){fclose(f);return 0;}}
 return fclose(f)==0;
}
int main(int argc,char**argv){
 if(argc>1)output=argv[1];
 maze_start();CHECK(frames(8));CHECK(maze_status().state==0);CHECK(maze_status().x==1&&maze_status().y==1);CHECK(dump("title"));
 CHECK(press(MAZE_START));CHECK(maze_status().state==1);
 CHECK(press(MAZE_UP));CHECK(maze_status().x==1&&maze_status().y==1&&maze_status().moves==0);
 CHECK(press(MAZE_RIGHT));CHECK(maze_status().x==2&&maze_status().y==1&&maze_status().moves==1);
 CHECK(press(MAZE_RIGHT));CHECK(maze_status().x==3&&maze_status().y==1&&maze_status().moves==2);CHECK(dump("play"));
 maze_buttons(MAZE_RIGHT);CHECK(frames(4));CHECK(maze_status().x==4&&maze_status().moves==3);maze_buttons(0);CHECK(frames(2));
 CHECK(press(MAZE_START));CHECK(maze_status().x==1&&maze_status().y==1&&maze_status().moves==0);
 const char*path="RRRDDRRUURRRRDDDDDDD";unsigned x=1,y=1;
 for(unsigned i=0;path[i];i++){
  unsigned key=0;if(path[i]=='R'){key=MAZE_RIGHT;x++;}else if(path[i]=='L'){key=MAZE_LEFT;x--;}else if(path[i]=='D'){key=MAZE_DOWN;y++;}else{key=MAZE_UP;y--;}
  CHECK(press(key));CHECK(maze_status().x==x&&maze_status().y==y&&maze_status().moves==i+1&&maze_status().held==0);
 }
 CHECK(maze_status().state==2&&x==10&&y==8);CHECK(dump("won"));
 CHECK(press(MAZE_LEFT));CHECK(maze_status().x==10&&maze_status().y==8&&maze_status().moves==20);
 CHECK(press(MAZE_START));CHECK(maze_status().state==1&&maze_status().x==1&&maze_status().y==1&&maze_status().moves==0);CHECK(dump("restart"));
 unsigned nmi=maze_status().nmi;CHECK(frames(260));CHECK(maze_status().nmi==((nmi+260)&255u));
 maze_start();CHECK(frames(8));CHECK(maze_status().state==0&&maze_status().x==1&&maze_status().y==1);
 printf("NATIVE PASS checks=%u frame=%u state=%u player=%u,%u\n",checks,maze_status().frame,maze_status().state,maze_status().x,maze_status().y);return 0;
}
