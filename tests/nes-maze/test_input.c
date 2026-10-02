#include "tinyrt.h"
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static maze_status_t status;
static unsigned requested,presses,last_button,frames,ms;
#define CHECK(x) do{if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void maze_start(void){memset(&status,0,sizeof(status));status.ready=1;status.frame=5;}
maze_status_t maze_status(void){return status;}
void maze_buttons(uint32_t b){if(b&&b!=last_button)presses++;last_button=b;}
uint32_t maze_step(void){status.frame++;frames++;return 1;}
const uint16_t *maze_pixels(void){static uint16_t pixels[256*240];return pixels;}
int32_t input_events(uint32_t mask){requested=mask;return 0;}
int32_t clock_interval(int32_t n){return n==1?0:-1;}
int32_t draw_skip(void){return 0;}
int32_t draw_clear(uint32_t c){(void)c;return 0;}
int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t c){(void)x;(void)y;(void)w;(void)h;(void)c;return 0;}
int32_t draw_rgb565(int32_t x,int32_t y,int32_t w,int32_t h,const uint8_t*p,uint32_t n){(void)x;(void)y;(void)w;(void)h;(void)p;(void)n;return 0;}
int32_t draw_round_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t r,uint32_t c){(void)r;return draw_rect(x,y,w,h,c);}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char*t,uint32_t n,uint32_t c,int32_t f,int32_t a){(void)t;(void)n;(void)f;(void)a;return draw_rect(x,y,w,h,c);}
static void tick(unsigned n){while(n--){ms+=20;CHECK(tinyrt_event(2,0,0,(int32_t)ms)==0);}}
int main(void){
 CHECK(tinyrt_init(466,466)==0);CHECK(requested==0x38);
 /* A fast tap survives before the next emulated controller read, exactly once. */
 CHECK(tinyrt_event(3,284,407,0)==0);CHECK(tinyrt_event(1,284,407,0)==0);
 tick(20);CHECK(presses==1&&last_button==0);
 /* Holding repeats while the ROM itself remains an edge-triggered controller. */
 CHECK(tinyrt_event(3,284,407,0)==0);tick(50);CHECK(presses>=4);
 unsigned before=presses;CHECK(tinyrt_event(1,284,407,0)==0);tick(25);CHECK(presses==before&&last_button==0);
 CHECK(tinyrt_event(3,284,407,0)==0);tick(2);
 CHECK(tinyrt_event(4,233,365,0)==0);tick(2);CHECK(last_button==MAZE_UP||last_button==0);
 CHECK(tinyrt_event(5,0,0,0)==0);before=presses;tick(25);CHECK(presses==before&&last_button==0);
 /* Start/reset never repeats; old script release-only taps are still usable. */
 CHECK(tinyrt_event(3,326,365,0)==0);tick(50);before=presses;
 CHECK(tinyrt_event(1,326,365,0)==0);tick(25);CHECK(presses==before);
 CHECK(tinyrt_event(1,284,407,0)==0);tick(5);CHECK(presses==before+1);
 CHECK(tinyrt_event(3,284,407,0)==0);CHECK(tinyrt_stop()==0);before=presses;tick(10);CHECK(presses==before&&last_button==0);
 printf("NES INPUT PASS frames=%u presses=%u\n",frames,presses);return 0;
}
