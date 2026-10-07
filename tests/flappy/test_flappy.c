#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../examples/flappy/main.c"
#include "../graphics/reference_mock.h"

static uint32_t time_ms;
static int stored_best, commands, saves;
uint32_t now_ms(void) { return time_ms; }
int32_t input_events(uint32_t mask) { assert(mask==120); return 0; }
int32_t clock_interval(int32_t ms) { assert(ms==((test_caps&TINYRT_GFX_CAP_SPRITE)?33:8)); return 0; }
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
static unsigned char resources[600000];
static unsigned resource_length;
int32_t asset_read(uint32_t offset,void *out,uint32_t length) {
    assert(length<=4096 && offset<=resource_length && length<=resource_length-offset);
    memcpy(out,resources+offset,length);return (int32_t)length;
}
int32_t audio_play(uint32_t offset,uint32_t length,uint32_t rate) {
    assert(rate==16000 && length && length<=32000 && !(length&1));
    assert(offset<=resource_length && length<=resource_length-offset);return 0;
}
int32_t gfx_tex_upload(uint32_t slot,uint32_t format,uint32_t w,uint32_t h,const void *p,uint32_t n,uint32_t flags){
    assert(slot==0&&format==TINYRT_GFX_INDEX8&&flags==TINYRT_GFX_FROM_ASSET&&n==w*h);
    uintptr_t offset=(uintptr_t)p;assert(offset+n<=resource_length);
    test_texture=resources+offset;test_texture_width=w;test_texture_height=h;return 0;
}
int32_t draw_rgb565_scaled(int32_t x,int32_t y,int32_t w,int32_t h,int32_t sw,int32_t sh,const uint8_t *p,uint32_t length) {
    assert(x==0 && y==0 && w==466 && h==466 && sw==233 && sh==233 && p==pixels && length==233*233*2);
    assert(++commands<=128);return 0;
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    resource_length=(unsigned)fread(resources,1,sizeof(resources),file);fclose(file);assert(resource_length>100000);
    /* Every resident scene equals the complete legacy RGB565 composition. */
    assert(tinyrt_init(466,466)==0);
    uint16_t palette[256];assert(asset_read(RESIDENT_PALETTE_OFFSET,palette,RESIDENT_PALETTE_COUNT*2)>0);
    assert(gfx_pal_upload(0,0,RESIDENT_PALETTE_COUNT,palette)==0);
    assert(gfx_tex_upload(0,TINYRT_GFX_INDEX8,RESIDENT_ATLAS_WIDTH,RESIDENT_ATLAS_HEIGHT,
        (const void *)(uintptr_t)RESIDENT_ATLAS_OFFSET,RESIDENT_ATLAS_LENGTH,TINYRT_GFX_FROM_ASSET)==0);
    for(unsigned mode=0;mode<3;mode++)for(unsigned variant=0;variant<36;variant++){
        state=(int)mode;bird_y=(29+(int)variant*10)*256;score=(int)variant;best=1234;run_best=0;
        scenery=variant*7;time_ms=10000+variant*100;died_ms=time_ms-900;
        pipes[0]=(pipe_t){-60+(int)variant*15,200+(int)variant,0};
        pipes[1]=(pipe_t){225,220,0};pipes[2]=(pipe_t){450,240,0};
        stripe=0;sprite_backend=0;for(unsigned i=0;i<4;i++)assert(compose_strip()==0);
        sprite_backend=1;stripe=0;for(unsigned i=0;i<4;i++)assert(compose_strip()==0);
        assert(gfx_begin(0)==0&&gfx_submit(graphics.bytes,graphics_length)==0&&gfx_end()==0);
        test_equal_scaled(pixels);
    }
    sprite_backend=0;
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
    test_caps=TINYRT_GFX_CAP_SPRITE;assert(tinyrt_init(466,466)==0&&sprite_backend);
    /* Sprite backend publishes every 33 ms clock; legacy keeps four strips. */
    for(unsigned i=0;i<4;i++) {
        time_ms+=33;assert(tinyrt_event(TINYRT_CLOCK_EVENT,0,0,0)==0);
        assert(frame_ready && stripe==0 && graphics_length);
        assert(tinyrt_render()==0 && !frame_ready);
    }
    puts("PASS resident/legacy pixel equality (108 scenes), input lifecycle, retry guard, round viewport bounds, scoring, persistence and clock wrap");
    return 0;
}
