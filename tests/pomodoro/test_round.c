/* Public guest ABI: round-screen geometry and input/lifecycle behavior. */
#include "tinyrt.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned checks, failures, writes, commands, large_time, arcs;
static int32_t value;
static int present;
static uint32_t clock_ms;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)
static void point(int32_t x, int32_t y) {
    int64_t dx=x-233,dy=y-233;
    CHECK(dx*dx+dy*dy<=215*215);
    CHECK(!(x>=203 && x<=263 && y>=24 && y<=76));
}
static void box(int32_t x,int32_t y,int32_t w,int32_t h) {
    CHECK(w>0 && h>0);
    point(x,y);point(x+w-1,y);point(x,y+h-1);point(x+w-1,y+h-1);
}
int32_t kv_get(uint32_t key,int32_t fallback){CHECK(key==0);return present?value:fallback;}
int32_t kv_set(uint32_t key,int32_t v){CHECK(key==0);value=v;present=1;writes++;return 0;}
uint32_t now_ms(void){return clock_ms;}
int32_t draw_clear(uint32_t rgb){CHECK(rgb==0);commands++;return 0;}
int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb){(void)rgb;box(x,y,w,h);commands++;return 0;}
int32_t draw_text(int32_t x,int32_t y,const char *text,uint32_t len,uint32_t rgb){(void)text;(void)len;(void)rgb;point(x,y);commands++;return 0;}
int32_t draw_round_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t r,uint32_t rgb){(void)rgb;CHECK(r>0 && r<=w/2 && r<=h/2);box(x,y,w,h);commands++;return 0;}
int32_t draw_arc(int32_t x,int32_t y,int32_t r,int32_t t,int32_t start,int32_t end,uint32_t rgb){
    (void)rgb;CHECK(r>0 && t>0 && t<=r);CHECK(start>=0 && end<=360 && start<end);
    /* Stronger than sample points: the entire outer circle is inside the safe circle. */
    int32_t dx=x-233,dy=y-233;int32_t room=215-r;
    CHECK(room>=0 && dx*dx+dy*dy<=room*room);
    CHECK(y-r>76 || x+r<203 || x-r>263);arcs++;commands++;return 0;
}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *text,uint32_t len,uint32_t rgb,int32_t font,int32_t align){
    (void)rgb;box(x,y,w,h);CHECK(align>=0 && align<=2);CHECK(len>0 && len<=63);
    CHECK(font==18 || font==24 || font==36 || font==48);
    if(len==5 && text[2]==':'){CHECK(font==48 && align==1);large_time++;}
    commands++;return 0;
}
static void frame(void){commands=large_time=arcs=0;CHECK(tinyrt_render()==0);CHECK(commands<=128);CHECK(large_time==1);CHECK(arcs>=1);}
int main(void){
    const int32_t bad[][2]={{0,0},{160,240},{465,466},{466,465},{480,480},{-1,466},{466,0}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)CHECK(tinyrt_init(bad[i][0],bad[i][1])!=0);
    CHECK(tinyrt_init(466,466)==0);frame();CHECK(writes==0);
    /* The host's return region and corners must never be action hit targets. */
    for(int y=24;y<=76;y+=13)for(int x=203;x<=263;x+=15)CHECK(tinyrt_event(1,x,y,0)==0);
    CHECK(tinyrt_event(1,10,400,0)==0);CHECK(tinyrt_event(1,455,400,0)==0);CHECK(writes==0);
    CHECK(tinyrt_event(1,130,358,0)==0);CHECK(tinyrt_event(1,294,358,0)==0);CHECK(writes==0);
    CHECK(tinyrt_event(1,200,384,0)==0);CHECK(writes==1);frame();
    clock_ms=1000;CHECK(tinyrt_event(2,0,0,0)==0);frame();
    CHECK(tinyrt_event(1,320,384,0)==0);CHECK(writes==2);frame();
    printf("round checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
