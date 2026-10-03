#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../examples/wave_sim/main.c"
static uint32_t clock_ms;
static unsigned images, skips;
uint32_t now_ms(void) { return clock_ms; }
int32_t runtime_backend(void) { return 1; }
int32_t clock_interval(int32_t ms) { assert(ms==16);return 0; }
int32_t input_events(uint32_t mask) { assert(mask==248);return 0; }
int32_t draw_clear(uint32_t rgb) { (void)rgb;return 0; }
int32_t draw_skip(void) { skips++;return 0; }
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *s,uint32_t n,uint32_t c,int32_t px,int32_t a) {
    (void)s;(void)c;assert(px==18||px==24||px==36||px==48);assert(a>=0&&a<=2);assert(n && x>=0 && y>=0 && x+w<=466 && y+h<=466);return 0;
}
int32_t draw_rgb565_scaled(int32_t x,int32_t y,int32_t w,int32_t h,int32_t sw,int32_t sh,const uint8_t *p,uint32_t n) {
    assert(x==0&&y==0&&w==466&&h==466&&sw==233&&sh==233&&p==pixels&&n==sizeof(pixels));images++;return 0;
}
static void advance(unsigned ms) { clock_ms+=ms;assert(tinyrt_event(2,0,0,0)==0);assert(tinyrt_render()==0); }
int main(void) {
#ifdef WAVE_METRICS
    metrics_reset(0);physics_ms=240;physics_count=240;compose_ms=120;compose_count=60;present_count=59;
    assert(metrics_draw(2000)==0&&metrics_ready&&measured_fps==30);
    assert(measured_physics10==10&&measured_compose10==20&&present_count==0);
#endif
    static wave_state_t sliced,whole;
    wave_reset(&sliced);wave_reset(&whole);
    for(int n=0;n<100;n++) {
        wave_tilt(&sliced,700,-700);wave_tilt(&whole,700,-700);
        wave_begin(&sliced,1.0f/60.0f);while(!wave_work(&sliced,16)){}
        wave_step(&whole,1.0f/60.0f);
        for(int i=0;i<WAVE_N;i++){assert(sliced.px[i]==whole.px[i]);assert(sliced.py[i]==whole.py[i]);}
    }
    assert(tinyrt_init(466,466)==0);
    assert(tinyrt_init(240,240)!=0);
    advance(16);advance(17);assert(images);
    assert(tinyrt_event(7,700,0,700)==0);assert(fluid.tx>0.99f);
    assert(tinyrt_event(7,0,0,1000)==0);assert(fluid.tx>0.99f);
    unsigned before=theme;
    tinyrt_event(3,200,200,0);advance(499);assert(theme==before);
    advance(1);assert(theme==(before+1)%5);advance(600);assert(theme==(before+1)%5);
    tinyrt_event(1,200,200,0);assert(!touch_down);
    tinyrt_event(3,200,200,0);tinyrt_event(5,0,0,0);advance(600);assert(theme==(before+1)%5);
    tinyrt_event(3,200,200,0);clock_ms+=500;tinyrt_event(1,200,200,0);assert(theme==(before+2)%5);
    tinyrt_event(6,1,1,0);advance(600);float old=fluid.ox[0];tinyrt_event(6,1,0,0);assert(old==fluid.ox[0]);
    tinyrt_event(6,1,1,0);advance(10);tinyrt_event(6,1,0,0);assert(fluid.ox[0]!=fluid.px[0]);
    for(int i=0;i<36000;i++) {
        if(i%60==0)wave_tilt(&fluid,(i/60)%2?700:-700,(i/120)%2?700:-700);
        if(i%120==0)wave_splash(&fluid);
        wave_step(&fluid,1.0f/60.0f);
        for(int j=0;j<WAVE_N;j++) {
            assert(isfinite(fluid.px[j])&&isfinite(fluid.py[j]));
            float x=fluid.px[j]-20,y=fluid.py[j]-20;
            assert(x*x+y*y<=WAVE_R*WAVE_R+0.01f);
        }
    }
    clock_ms=0xfffffff0u;last_ms=clock_ms;advance(33);assert(tinyrt_stop()==0);
    assert(tinyrt_init(466,466)==0&&!touch_down&&!key_down&&theme==0);
    for(unsigned i=0;i<5;i++){tinyrt_event(3,200,200,0);advance(500);tinyrt_event(1,200,200,0);}
    assert(theme==0);
    printf("PASS input, clock wrap, RGB565 bounds, 5-minute shake, %d particles\n",WAVE_N);
    return 0;
}
