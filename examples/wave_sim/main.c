#include "tinyrt.h"
#include "wave_physics.h"
#define RGB565(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define COLOR(v) RGB565(((v)>>16)&255,((v)>>8)&255,(v)&255)
typedef struct {const char *name;uint16_t bg,off,c[6];uint32_t ink;} theme_t;
static const theme_t themes[]={
    {"OCEAN",COLOR(0x060a1a),COLOR(0x0f1630),{COLOR(0x0b2a7a),COLOR(0x1450c8),COLOR(0x1e8cf0),COLOR(0x3fc4ff),COLOR(0xa8f0ff),COLOR(0xffffff)},0xffffff},
    {"NEON",COLOR(0x0a0614),COLOR(0x1a1030),{COLOR(0x3a0a6e),COLOR(0x7a14c8),COLOR(0xc82ef0),COLOR(0xff5ad2),COLOR(0xffb4f0),COLOR(0xffffff)},0xffffff},
    {"TOXIC",COLOR(0x050d08),COLOR(0x0e1e14),{COLOR(0x0a3a1a),COLOR(0x13802c),COLOR(0x22c43c),COLOR(0x7cf05a),COLOR(0xd2ff9a),COLOR(0xffffff)},0xffffff},
    {"LAVA",COLOR(0x100604),COLOR(0x24100a),{COLOR(0x5a0a04),COLOR(0xa8200a),COLOR(0xe8501a),COLOR(0xff9a2e),COLOR(0xffe08a),COLOR(0xffffff)},0xffffff},
    {"MONO",COLOR(0x08090c),COLOR(0x16181e),{COLOR(0x2a2e38),COLOR(0x4a505e),COLOR(0x7a8292),COLOR(0xaab2c2),COLOR(0xdce2ee),COLOR(0xffffff)},0xffffff}
};
static wave_state_t fluid;
static uint8_t pixels[233*233*2];
static uint16_t counts[1600];
static float fastest[1600];
static unsigned theme,frame;
static int touch_down,key_down,theme_changed,frame_ready;
static uint32_t last_ms,render_ms,touch_ms,key_ms,toast_ms,accumulator;
static int fast_backend,physics_active,compose_stage,compose_row;
static unsigned render_theme;
#ifdef WAVE_METRICS
static uint32_t stats_since,physics_ms,compose_ms,physics_count,compose_count,present_count;
static uint32_t measured_fps,measured_physics10,measured_compose10;
static int metrics_ready;
static void metrics_reset(uint32_t now){
    stats_since=now;physics_ms=compose_ms=physics_count=compose_count=present_count=0;
    measured_fps=measured_physics10=measured_compose10=0;metrics_ready=0;
}
static unsigned number(char *s,unsigned n,uint32_t v){
    char digits[10];unsigned k=0;do{digits[k++]=(char)('0'+v%10);v/=10;}while(v);
    while(k)s[n++]=digits[--k];return n;
}
static unsigned timing(char *s,unsigned n,uint32_t v){n=number(s,n,v/10);s[n++]='.';s[n++]=(char)('0'+v%10);return n;}
static int metrics_draw(uint32_t now){
    present_count++;uint32_t span=now-stats_since;
    if(span>=2000u){
        measured_fps=present_count*1000u/span;
        measured_physics10=physics_count?physics_ms*10u/physics_count:0;
        measured_compose10=compose_count?compose_ms*10u/compose_count:0;
        stats_since=now;physics_ms=compose_ms=physics_count=compose_count=present_count=0;metrics_ready=1;
    }
    char s[64];unsigned n=0;
    if(!metrics_ready){const char *p="Measuring...";while(*p)s[n++]=*p++;}
    else {
        const char *p="FPS ";while(*p)s[n++]=*p++;n=number(s,n,measured_fps);
        p="  P ";while(*p)s[n++]=*p++;n=timing(s,n,measured_physics10);
        p="  R ";while(*p)s[n++]=*p++;n=timing(s,n,measured_compose10);
        p=" ms";while(*p)s[n++]=*p++;
    }
    return draw_text_box(43,300,380,36,s,n,0xffffff,18,1);
}
#endif
static void next_theme(uint32_t now){theme=(theme+1)%5;toast_ms=now;theme_changed=1;}
/* 未实测默认值。真机以轴向调试包确认后，通过 app.json defines 配置。 */
#ifndef WAVE_SWAP_AXES
#define WAVE_SWAP_AXES 0
#endif
#ifndef WAVE_X_SIGN
#define WAVE_X_SIGN 1
#endif
#ifndef WAVE_Y_SIGN
#define WAVE_Y_SIGN 1
#endif
static void motion_to_screen(int32_t ax,int32_t ay,int32_t *sx,int32_t *sy) {
    *sx=(WAVE_SWAP_AXES?ay:ax)*WAVE_X_SIGN;
    *sy=(WAVE_SWAP_AXES?ax:ay)*WAVE_Y_SIGN;
}
static void rectangle(int x,int y,int w,int h,uint16_t color) {
    if(x<0||y<0||x+w>233||y+h>233)return;
    for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++) {
        unsigned p=(unsigned)(yy*233+xx)*2;pixels[p]=(uint8_t)color;pixels[p+1]=(uint8_t)(color>>8);
    }
}
static int round_cell(float v) {return (int)(v+0.5f+2.0f)-2;}
static void compose_begin(void){compose_stage=0;compose_row=0;render_theme=theme;}
static int compose_chunk(void) {
    const theme_t *t=&themes[render_theme];
    if(compose_stage==0){
        int rows=233-compose_row;if(rows>4)rows=4;
        rectangle(0,compose_row,233,rows,t->bg);compose_row+=rows;
        if(compose_row==233)compose_stage=1;return 0;
    }
    if(compose_stage==1){
    for(int c=0;c<1600;c++){counts[c]=0;fastest[c]=0;}
    for(int i=0;i<WAVE_N;i++) {
        int x=(int)fluid.px[i],y=(int)fluid.py[i];
        if(x>=0&&x<40&&y>=0&&y<40){int c=y*40+x;counts[c]++;if(fluid.speed[i]>fastest[c])fastest[c]=fluid.speed[i];}
    }
        compose_stage=2;compose_row=0;return 0;
    }
    if(compose_stage==2){
    int ux=-round_cell(fluid.gx*1.4f),uy=-round_cell(fluid.gy*1.4f);
    int end=compose_row+2;if(end>40)end=40;
    for(int y=compose_row;y<end;y++)for(int x=0;x<40;x++) {
        float dx=x+0.5f-20,dy=y+0.5f-20;if(dx*dx+dy*dy>19.8f*19.8f)continue;
        int c=y*40+x;uint16_t color=t->off;
        if(counts[c]) {
            unsigned level=counts[c]<3?counts[c]:3;
            int nx=x+ux,ny=y+uy;
            if(nx<0||nx>=40||ny<0||ny>=40||!counts[ny*40+nx])level=4;
            if(fastest[c]>0.35f)level=5;
            if(level<3 && ((unsigned)(x*7+y*13)+frame)%23u==0)level++;
            color=t->c[level];
        }
        rectangle(16+x*5+1,16+y*5+1,4,4,color);
    }
        compose_row=end;if(end==40)compose_stage=3;return 0;
    }
    /* 每 10 度一个点，半径 108；表为整数像素偏移。 */
    static const int8_t ring[36][2]={{108,0},{106,19},{101,37},{94,54},{83,69},{69,83},{54,94},{37,101},{19,106},{0,108},{-19,106},{-37,101},{-54,94},{-69,83},{-83,69},{-94,54},{-101,37},{-106,19},{-108,0},{-106,-19},{-101,-37},{-94,-54},{-83,-69},{-69,-83},{-54,-94},{-37,-101},{-19,-106},{0,-108},{19,-106},{37,-101},{54,-94},{69,-83},{83,-69},{94,-54},{101,-37},{106,-19}};
    for(int i=0;i<36;i++)rectangle(116+ring[i][0],116+ring[i][1],2,2,t->off);
    float m=wave_sqrt(fluid.gx*fluid.gx+fluid.gy*fluid.gy);
    float x=m>0.001f?fluid.gx/m:0,y=m>0.001f?fluid.gy/m:1;
    rectangle((int)(116+x*108)-1,(int)(116+y*108)-1,3,3,t->c[5]);
    frame++;frame_ready=1;compose_stage=-1;return 1;
}
static int compose_work(void){
#ifdef WAVE_METRICS
    uint32_t started=now_ms();
#endif
    int done=compose_chunk();
#ifdef WAVE_METRICS
    compose_ms+=now_ms()-started;if(done)compose_count++;
#endif
    return done;
}
static void physics_step(void){
#ifdef WAVE_METRICS
    uint32_t started=now_ms();
#endif
    wave_step(&fluid,1.0f/60.0f);
#ifdef WAVE_METRICS
    physics_ms+=now_ms()-started;physics_count++;
#endif
}
static int physics_work(void){
#ifdef WAVE_METRICS
    uint32_t started=now_ms();
#endif
    int done=wave_work(&fluid,16);
#ifdef WAVE_METRICS
    physics_ms+=now_ms()-started;if(done)physics_count++;
#endif
    return done;
}
static void compose(void){compose_begin();while(!compose_work()){} }
int32_t tinyrt_init(int32_t w,int32_t h) {
    if(w!=466||h!=466)return -1;
    wave_reset(&fluid);theme=frame=accumulator=0;touch_down=key_down=theme_changed=frame_ready=0;
    last_ms=render_ms=now_ms();toast_ms=last_ms-1500u;
#ifdef WAVE_METRICS
    metrics_reset(last_ms);
#endif
    if(input_events(248u)!=0)return -1;
    fast_backend=runtime_backend();physics_active=0;compose_begin();
    if(fast_backend)compose();
    return clock_interval(fast_backend?16:1);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    (void)arg;
    uint32_t now=now_ms();
    if(kind==TINYRT_EV_MOTION){int32_t sx,sy;motion_to_screen(x,y,&sx,&sy);wave_tilt(&fluid,sx,sy);}
    else if(kind==TINYRT_TOUCH_PRESS&&!touch_down){touch_down=1;touch_ms=now;theme_changed=0;}
    else if(kind==TINYRT_TOUCH_RELEASE){if(touch_down&&!theme_changed){if((uint32_t)(now-touch_ms)<500u)wave_splash(&fluid);else next_theme(now);}touch_down=0;}
    else if(kind==TINYRT_TOUCH_CANCEL){touch_down=key_down=0;}
    else if(kind==TINYRT_USER_KEY_EVENT&&x==1){
        if(y&&!key_down){key_down=1;key_ms=now;}
        else if(!y){if(key_down&&(uint32_t)(now-key_ms)<500u)wave_splash(&fluid);key_down=0;}
    } else if(kind==TINYRT_CLOCK_EVENT) {
        uint32_t elapsed=now-last_ms;last_ms=now;
        if(elapsed>100u)elapsed=100u;
        accumulator+=elapsed*120u;
        if(fast_backend){
            unsigned steps=0;while(accumulator>=1000u&&steps<8){physics_step();accumulator-=1000u;steps++;}
            if(accumulator>=1000u)accumulator%=1000u;
        } else {
            if(accumulator>8000u)accumulator=8000u;
            if(compose_stage>=0)compose_work();
            else if(physics_active){if(physics_work()){physics_active=0;if((uint32_t)(now-render_ms)>=33u){render_ms=now-(uint32_t)(now-render_ms)%33u;compose_begin();}}}
            else if(accumulator>=1000u){accumulator-=1000u;wave_begin(&fluid,1.0f/60.0f);physics_active=1;}
            else if((uint32_t)(now-render_ms)>=33u){render_ms=now-(uint32_t)(now-render_ms)%33u;compose_begin();}
        }
        if(touch_down&&!theme_changed&&(uint32_t)(now-touch_ms)>=500u)next_theme(now);
        if(fast_backend&&(uint32_t)(now-render_ms)>=33u){render_ms=now-(uint32_t)(now-render_ms)%33u;compose();}
    }
    return 0;
}
int32_t tinyrt_render(void) {
    if(!frame_ready)return draw_skip();frame_ready=0;
    if(draw_clear(0)!=0)return -1;
    if(draw_rgb565_scaled(0,0,466,466,233,233,pixels,sizeof(pixels))!=0)return -1;
    if(render_theme==theme&&(uint32_t)(now_ms()-toast_ms)<1500u) {
        unsigned n=0;while(themes[theme].name[n])n++;
        if(draw_text_box(133,126,200,36,themes[theme].name,n,themes[theme].ink,24,1)!=0)return -1;
    }
#ifdef WAVE_METRICS
    return metrics_draw(now_ms());
#endif
    return 0;
}
int32_t tinyrt_stop(void){touch_down=key_down=0;return 0;}
