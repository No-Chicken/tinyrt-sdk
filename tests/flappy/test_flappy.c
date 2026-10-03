#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../examples/flappy/main.c"

static uint32_t time_ms;
static int stored_best, commands, saves;
uint32_t now_ms(void) { return time_ms; }
int32_t input_events(uint32_t mask) { assert(mask==120); return 0; }
int32_t clock_interval(int32_t ms) { assert(ms==8); return 0; }
int32_t kv_get(uint32_t key,int32_t fallback) { assert(key==0); (void)fallback; return stored_best; }
int32_t kv_set(uint32_t key,int32_t value) { assert(key==0); stored_best=value;saves++;return 0; }
int32_t draw_clear(uint32_t rgb) { (void)rgb;commands=1;return 0; }
int32_t draw_skip(void) { return 0; }
int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb) {
    (void)rgb;assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=466&&y+h<=466);
    assert(++commands<=128);return 0;
}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *s,uint32_t n,uint32_t rgb,int32_t px,int32_t align) {
    (void)px;(void)align;assert(n==strlen(s)&&n>0&&n<=63);
    return draw_rect(x,y,w,h,rgb);
}
static void step(void) { time_ms+=33;assert(tinyrt_event(2,0,0,0)==0);assert(tinyrt_render()==0); }
static void press_tap(void) { assert(tinyrt_event(3,233,260,0)==0);assert(tinyrt_event(1,233,260,0)==0); }
static unsigned char resources[400000];
static unsigned resource_length;
int32_t asset_read(uint32_t offset,void *out,uint32_t length) {
    assert(length<=4096 && offset<=resource_length && length<=resource_length-offset);
    memcpy(out,resources+offset,length);return (int32_t)length;
}
int32_t audio_play(uint32_t offset,uint32_t length,uint32_t rate) {
    assert(rate==16000 && length && length<=32000 && !(length&1));
    assert(offset<=resource_length && length<=resource_length-offset);return 0;
}
int32_t draw_rgb565_scaled(int32_t x,int32_t y,int32_t w,int32_t h,int32_t sw,int32_t sh,const uint8_t *p,uint32_t length) {
    assert(x==0 && y==0 && w==466 && h==466 && sw==233 && sh==233 && p==pixels && length==233*233*2);
    assert(++commands<=128);return 0;
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    resource_length=(unsigned)fread(resources,1,sizeof(resources),file);fclose(file);assert(resource_length>100000);
    assert(tinyrt_init(320,240)!=0);assert(tinyrt_init(466,466)==0);
    assert(state==READY);assert(tinyrt_render()==0);
    press_tap();assert(state==RUNNING&&velocity<0);
    int v=velocity;assert(tinyrt_event(4,233,100,0)==0);assert(velocity==v);
    for(int i=0;i<100&&state==RUNNING;i++) step();
    assert(state==GAME_OVER);press_tap();assert(state==GAME_OVER);
    time_ms+=900;assert(tinyrt_event(3,233,384,0)==0);assert(tinyrt_event(1,233,384,0)==0);assert(state==RUNNING&&score==0);
    /* Each press has one impulse; queue cancellation clears touch and KEY1. */
    tinyrt_event(6,1,1,0);step();v=velocity;tinyrt_event(6,1,1,0);assert(velocity==v);
    tinyrt_event(5,0,0,0);assert(!key_down);tinyrt_event(6,1,0,0);assert(!key_down);
    /* Autopilot follows the next real gap, exercising collisions and scoring. */
    reset_game();state=RUNNING;
    for(int i=0;i<2200&&state==RUNNING;i++) {
        int target=218,nearest=10000;
        for(int j=0;j<3;j++) if(pipes[j].x+PIPE_W>BIRD_X-12 && pipes[j].x<nearest) {
            nearest=pipes[j].x;target=pipes[j].gap;
        }
        if(bird_y/256>target+12 && velocity>0) press_tap();
        step();
    }
    assert(score>=5);int earned=score;assert(tinyrt_stop()==0);assert(stored_best>=earned);
    int count=saves;assert(tinyrt_stop()==0&&saves==count);
    assert(tinyrt_init(466,466)==0&&best>=earned);
    time_ms=0xfffffff0u;last_ms=time_ms;press_tap();step();assert(state==RUNNING);
    puts("PASS input lifecycle, retry guard, round viewport bounds, scoring, persistence and clock wrap");
    return 0;
}
