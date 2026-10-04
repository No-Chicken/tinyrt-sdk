#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../../examples/wave_sim/main.c"
#include "../graphics/reference_mock.h"
static uint32_t clock_ms;
static unsigned images, skips;
uint32_t now_ms(void) { return clock_ms; }
int32_t runtime_backend(void) { return 1; }
int32_t clock_interval(int32_t ms) { assert(ms==16);return 0; }
int32_t input_events(uint32_t mask) { assert(mask==248);return 0; }
int32_t draw_clear(uint32_t rgb) { (void)rgb;return 0; }
int32_t draw_skip(void) { skips++;return 0; }
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *s,uint32_t n,uint32_t c,int32_t px,int32_t a) {
    (void)s;assert(px==18||px==24||px==36||px==48);assert(a>=0&&a<=2);assert(n && x>=0 && y>=0 && x+w<=466 && y+h<=466);
    test_fill(x,y,w,h,COLOR(c));return 0;
}
int32_t draw_rgb565_scaled(int32_t x,int32_t y,int32_t w,int32_t h,int32_t sw,int32_t sh,const uint8_t *p,uint32_t n) {
    assert(x==0&&y==0&&w==466&&h==466&&sw==233&&sh==233&&p==pixels&&n==sizeof(pixels));images++;return 0;
}
int32_t gfx_tex_upload(uint32_t slot,uint32_t format,uint32_t w,uint32_t h,const void *p,uint32_t n,uint32_t flags){
    (void)slot;(void)format;(void)w;(void)h;(void)p;(void)n;(void)flags;return -1;
}
static void advance(unsigned ms) { clock_ms+=ms;assert(tinyrt_event(2,0,0,0)==0);assert(tinyrt_render()==0); }
int main(void) {
    int32_t ax=0,ay=0,bx=32768,by=0;
    wave_separate(&ax,&ay,&bx,&by);
    assert(ax==-16384&&bx==49152&&ay==0&&by==0);
    ax=0;ay=0;bx=65536;by=0;wave_separate(&ax,&ay,&bx,&by);
    assert(ax==0&&bx==65536);
    ax=0;ay=0;bx=0;by=0;wave_separate(&ax,&ay,&bx,&by);
    assert(ax==0&&bx==0&&ay==0&&by==0);
    ax=0;ay=0;bx=32768;by=32768;wave_separate(&ax,&ay,&bx,&by);
    assert(ax+bx==32768&&ay+by==32768);
    float dx=(float)(bx-ax)/65536,dy=(float)(by-ay)/65536;
    assert(fabsf(dx*dx+dy*dy-1)<0.002f);
    /* 填色路径必须逐字节保留小端 RGB565，含非 8 倍数宽度和奇数行起点。 */
    rectangle(0,0,233,233,0x1234);
    rectangle(17,17,4,4,0xabcd);
    for(int y=0;y<233;y++)for(int x=0;x<233;x++){
        uint16_t want=(x>=17&&x<21&&y>=17&&y<21)?0xabcd:0x1234;
        unsigned at=(unsigned)(y*233+x)*2;
        assert(pixels[at]==(uint8_t)want&&pixels[at+1]==(uint8_t)(want>>8));
    }
    int32_t sx,sy;
    motion_to_screen(700,300,&sx,&sy);
#if defined(WAVE_IMU_MOUNT_DEG) && WAVE_IMU_MOUNT_DEG == 0
    assert(sx==-700&&sy==300);
#else
    assert(sx==700&&sy==-300);
#endif
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
    /* 复用背景必须与每帧重新清空的整张图逐字节一致，覆盖主题和外环移动。 */
    static uint8_t cached[sizeof(pixels)];
    for(unsigned t=0;t<5;t++)for(unsigned angle=0;angle<36;angle++){
        theme=t;fluid.gx=cosf((float)angle*0.174532925f);fluid.gy=sinf((float)angle*0.174532925f);
        compose();for(unsigned i=0;i<sizeof(pixels);i++)cached[i]=pixels[i];
        frame--;background_valid=0;compose();
        for(unsigned i=0;i<sizeof(pixels);i++)assert(cached[i]==pixels[i]);
        frame--;grid_backend=1;compose();assert(!gfx_failed);
        assert(graphics_length==1084u+WAVE_CELLS);
        assert(graphics.bytes[0]==TINYRT_GFX_CLEAR&&graphics.bytes[2]==8);
        assert(gfx_begin(0)==0&&gfx_submit(graphics.bytes,graphics_length)==0&&gfx_end()==0);
        test_equal_scaled(cached);grid_backend=0;
    }
    assert(tinyrt_init(466,466)==0);
    /* Replay conservative damage onto a persistent panel, merging superseded frames. */
    static uint16_t panel[466*466];damage_t pending={0};unsigned replay_skips=skips;
    grid_backend=1;submitted_valid=0;clock_ms=2000;
    for(unsigned scene=0;scene<240;scene++){
        if(scene%40==0){theme=(scene/40)%5;toast_ms=clock_ms;}
        fluid.gx=cosf((float)scene*.27f);fluid.gy=sinf((float)scene*.27f);
        if(scene%7==0){fluid.gx=0;fluid.gy=1;}
        compose();assert(tinyrt_render()==0);
        if(skips==replay_skips)damage_add(&pending,test_damage_x,test_damage_y,test_damage_w,test_damage_h);
        replay_skips=skips;
        /* An unchanged composed scene must preserve the existing panel. */
        if(scene%11==0){unsigned before=skips;frame--;compose();assert(tinyrt_render()==0);
#ifndef WAVE_METRICS
            assert(skips==before+1);
#else
            (void)before;
#endif
            replay_skips=skips;
        }
        if(scene%3==2||scene==239){
            if(pending.valid)for(int y=pending.y;y<pending.bottom;y++)for(int x=pending.x;x<pending.right;x++)panel[y*466+x]=test_canvas[y*466+x];
            assert(memcmp(panel,test_canvas,sizeof(panel))==0);pending=(damage_t){0};
        }
        clock_ms+=100;
    }
    assert(tinyrt_init(466,466)==0);
    advance(16);assert(accumulator==960u);assert(fluid.px[0]==fluid.ox[0]);
    advance(1);assert(accumulator==20u);assert(fluid.dt==1.0f/30.0f);
    advance(16);advance(17);assert(images);
    assert(tinyrt_event(7,700,0,700)==0);assert(fluid.tx*WAVE_X_SIGN>0.99f);
    assert(tinyrt_event(7,0,0,1000)==0);assert(fluid.tx*WAVE_X_SIGN>0.99f);
    assert(tinyrt_event(7,0,700,700)==0);assert(fluid.ty*WAVE_Y_SIGN>0.99f);
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
        wave_step(&fluid,WAVE_STEP_DT);
        for(int j=0;j<WAVE_N;j++) {
            assert(isfinite(fluid.px[j])&&isfinite(fluid.py[j]));
            float x=fluid.px[j]-WAVE_CENTER,y=fluid.py[j]-WAVE_CENTER;
            assert(x*x+y*y<=WAVE_R*WAVE_R+0.01f);
        }
    }
    clock_ms=0xfffffff0u;last_ms=clock_ms;advance(33);assert(tinyrt_stop()==0);
    assert(tinyrt_init(466,466)==0&&!touch_down&&!key_down&&theme==0);
    for(unsigned i=0;i<5;i++){tinyrt_event(3,200,200,0);advance(500);tinyrt_event(1,200,200,0);}
    assert(theme==0);
    test_caps=TINYRT_GFX_CAP_RECT|TINYRT_GFX_CAP_GRID;
    assert(tinyrt_init(466,466)==0&&grid_backend);
    assert(tinyrt_render()==0&&!frame_ready);
    assert(tinyrt_render()==0);
    tinyrt_event(3,200,200,0);advance(500);assert(theme==1);
    tinyrt_event(5,0,0,0);assert(!touch_down&&!key_down);
    printf("PASS input, clock wrap, GRID/legacy pixel equality (180 scenes), RGB565 bytes/bounds, 36000-step shake, %dx%d / %d particles\n",WAVE_GRID,WAVE_GRID,WAVE_N);
    return 0;
}
