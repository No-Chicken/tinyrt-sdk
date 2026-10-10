#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "wave_physics.h"
/* 性能级别 1：60 个真实步/秒，单步 1/30 秒，模拟仍为真实时间两倍。 */
#define WAVE_STEP_HZ 60u
#define WAVE_STEP_DT (2.0f/(float)WAVE_STEP_HZ)
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
#define WAVE_PIXEL_BYTES (233u*233u*2u)
#if defined(__wasm__)
static struct {uint8_t *bytes;uint16_t *words;} framebuffer;
extern unsigned char __heap_base;
static int allocate_legacy_frame(void){
    uintptr_t start=((uintptr_t)&__heap_base+3u)&~(uintptr_t)3u;
    uint32_t pages=(uint32_t)((start+WAVE_PIXEL_BYTES+65535u)/65536u);
    uint32_t current=(uint32_t)__builtin_wasm_memory_size(0);
    if(pages>current && __builtin_wasm_memory_grow(0,pages-current)==(size_t)-1)return -1;
    framebuffer.bytes=(uint8_t *)start;framebuffer.words=(uint16_t *)start;return 0;
}
#else
static union {uint8_t bytes[WAVE_PIXEL_BYTES];uint16_t words[233*233];} framebuffer;
static int allocate_legacy_frame(void){return 0;}
#endif
#define pixels framebuffer.bytes
static int grid_backend,gfx_failed;
#ifndef WAVE_LEGACY_GRAPHICS
#define WAVE_LEGACY_GRAPHICS 0
#endif
static int palette_valid;
static unsigned palette_theme;
#define WAVE_LED_CELL (200/WAVE_GRID)
static uint8_t grid_cells[WAVE_CELLS];
static uint8_t submitted_cells[WAVE_CELLS];
static int submitted_valid,submitted_marker_x,submitted_marker_y,submitted_toast;
static unsigned submitted_theme;
typedef struct {int x,y,right,bottom,valid;} damage_t;
static void damage_add(damage_t *d,int x,int y,int w,int h){
    if(!d->valid){d->x=x;d->y=y;d->right=x+w;d->bottom=y+h;d->valid=1;return;}
    if(x<d->x)d->x=x;if(y<d->y)d->y=y;
    if(x+w>d->right)d->right=x+w;if(y+h>d->bottom)d->bottom=y+h;
}
static union {uint32_t align;uint8_t bytes[4096];} graphics;
static uint32_t graphics_length;
static void append_record(const void *record,uint32_t size){
    if(size>sizeof(graphics.bytes)-graphics_length){gfx_failed=1;return;}
    const uint8_t *p=record;for(uint32_t i=0;i<size;i++)graphics.bytes[graphics_length+i]=p[i];
    graphics_length+=size;
}
static uint16_t counts[WAVE_CELLS];
static float fastest[WAVE_CELLS];
static unsigned theme,frame;
static int touch_down,key_down,theme_changed,frame_ready;
static uint32_t last_ms,render_ms,touch_ms,key_ms,toast_ms,accumulator;
static int fast_backend,physics_active,compose_stage,compose_row;
static unsigned render_theme;
static int background_valid,last_marker_x,last_marker_y;
static unsigned background_theme;
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
/* 旧模型的轴向覆盖仅供回归测试。当前 BSP 已归一化安装方向，
 * 正常应用统一使用默认 X 正向、Y 反向，无需按芯片重复旋转。 */
#ifndef WAVE_IMU_MOUNT_DEG
#define WAVE_IMU_MOUNT_DEG 180
#endif
#if WAVE_IMU_MOUNT_DEG != 0 && WAVE_IMU_MOUNT_DEG != 180
#error "WAVE_IMU_MOUNT_DEG must be 0 or 180"
#endif
#ifndef WAVE_SWAP_AXES
#define WAVE_SWAP_AXES 0
#endif
#ifndef WAVE_X_SIGN
#define WAVE_X_SIGN (WAVE_IMU_MOUNT_DEG == 180 ? 1 : -1)
#endif
#ifndef WAVE_Y_SIGN
#define WAVE_Y_SIGN (WAVE_IMU_MOUNT_DEG == 180 ? -1 : 1)
#endif
static void motion_to_screen(int32_t ax,int32_t ay,int32_t *sx,int32_t *sy) {
    *sx=(WAVE_SWAP_AXES?ay:ax)*WAVE_X_SIGN;
    *sy=(WAVE_SWAP_AXES?ax:ay)*WAVE_Y_SIGN;
}
static void rectangle(int x,int y,int w,int h,uint16_t color) {
    if(x<0||y<0||x+w>233||y+h>233)return;
    if(grid_backend){
        tinyrt_gfx_rect_t r={TINYRT_GFX_RECT,sizeof(r),x*2,y*2,w*2,h*2,color,255};
        append_record(&r,sizeof(r));return;
    }
    for(int yy=y;yy<y+h;yy++) {
        uint16_t *row=framebuffer.words+yy*233+x;
        int xx=0;
        /* 一次写一个 RGB565 像素，展开背景循环以减少 AOT 循环轮询。 */
        for(;xx+8<=w;xx+=8){
            row[xx]=color;row[xx+1]=color;row[xx+2]=color;row[xx+3]=color;
            row[xx+4]=color;row[xx+5]=color;row[xx+6]=color;row[xx+7]=color;
        }
        for(;xx<w;xx++)row[xx]=color;
    }
}
static int round_cell(float v) {return (int)(v+0.5f+2.0f)-2;}
static void compose_begin(void){
    compose_stage=0;compose_row=0;render_theme=theme;
    if(grid_backend){
        graphics_length=0;gfx_failed=0;
        if(!palette_valid||palette_theme!=theme){
            uint16_t palette[8]={0,themes[theme].off};
            for(unsigned i=0;i<6;i++)palette[i+2]=themes[theme].c[i];
            if(gfx_pal_upload(0,0,8,palette)!=0)gfx_failed=1;
            else {palette_valid=1;palette_theme=theme;}
        }
        for(unsigned i=0;i<WAVE_CELLS;i++)grid_cells[i]=0;
    }
}
static int compose_chunk(void) {
    const theme_t *t=&themes[render_theme];
    if(compose_stage==0){
        if(grid_backend){
            struct {uint16_t op,size;uint32_t color;} clear={TINYRT_GFX_CLEAR,8,t->bg};
            append_record(&clear,sizeof(clear));compose_stage=1;return 0;
        }
        if(background_valid&&background_theme==render_theme){compose_stage=1;return 0;}
        int rows=233-compose_row;if(rows>4)rows=4;
        rectangle(0,compose_row,233,rows,t->bg);compose_row+=rows;
        if(compose_row==233)compose_stage=1;return 0;
    }
    if(compose_stage==1){
    for(int c=0;c<WAVE_CELLS;c++){counts[c]=0;fastest[c]=0;}
    for(int i=0;i<WAVE_N;i++) {
        int x=(int)fluid.px[i],y=(int)fluid.py[i];
        if(x>=0&&x<WAVE_GRID&&y>=0&&y<WAVE_GRID){int c=y*WAVE_GRID+x;counts[c]++;if(fluid.speed[i]>fastest[c])fastest[c]=fluid.speed[i];}
    }
        compose_stage=2;compose_row=0;return 0;
    }
    if(compose_stage==2){
    int ux=-round_cell(fluid.gx*1.4f),uy=-round_cell(fluid.gy*1.4f);
    int end=compose_row+2;if(end>WAVE_GRID)end=WAVE_GRID;
    for(int y=compose_row;y<end;y++)for(int x=0;x<WAVE_GRID;x++) {
        float dx=x+0.5f-WAVE_CENTER,dy=y+0.5f-WAVE_CENTER;
        float radius=19.8f*WAVE_RATIO;if(dx*dx+dy*dy>radius*radius)continue;
        int c=y*WAVE_GRID+x;uint16_t color=t->off;
        if(counts[c]) {
            unsigned level=counts[c]<3?counts[c]:3;
            int nx=x+ux,ny=y+uy;
            if(nx<0||nx>=WAVE_GRID||ny<0||ny>=WAVE_GRID||!counts[ny*WAVE_GRID+nx])level=4;
            if(fastest[c]>0.35f*WAVE_RATIO)level=5;
            if(level<3 && ((unsigned)(x*7+y*13)+frame)%23u==0)level++;
            color=t->c[level];
        }
        if(grid_backend){
            unsigned index=1;
            if(counts[c])for(unsigned i=0;i<6;i++)if(color==t->c[i]){index=i+2;break;}
            grid_cells[c]=(uint8_t)index;
        }else rectangle(16+x*WAVE_LED_CELL+1,16+y*WAVE_LED_CELL+1,WAVE_LED_CELL-1,WAVE_LED_CELL-1,color);
    }
        compose_row=end;if(end==WAVE_GRID){
            if(grid_backend){
                tinyrt_gfx_grid_t g={TINYRT_GFX_GRID,sizeof(g)+WAVE_CELLS,34,34,WAVE_GRID,WAVE_GRID,WAVE_LED_CELL*2,WAVE_LED_CELL*2,2,0,0};
                append_record(&g,sizeof(g));append_record(grid_cells,sizeof(grid_cells));
            }
            compose_stage=3;
        }return 0;
    }
    /* 每 10 度一个点，半径 108；表为整数像素偏移。 */
    static const int8_t ring[36][2]={{108,0},{106,19},{101,37},{94,54},{83,69},{69,83},{54,94},{37,101},{19,106},{0,108},{-19,106},{-37,101},{-54,94},{-69,83},{-83,69},{-94,54},{-101,37},{-106,19},{-108,0},{-106,-19},{-101,-37},{-94,-54},{-83,-69},{-69,-83},{-54,-94},{-37,-101},{-19,-106},{0,-108},{19,-106},{37,-101},{54,-94},{69,-83},{83,-69},{94,-54},{101,-37},{106,-19}};
    if(background_valid&&!grid_backend)rectangle(last_marker_x,last_marker_y,3,3,t->bg);
    for(int i=0;i<36;i++)rectangle(116+ring[i][0],116+ring[i][1],2,2,t->off);
    float m=wave_sqrt(fluid.gx*fluid.gx+fluid.gy*fluid.gy);
    float x=m>0.001f?fluid.gx/m:0,y=m>0.001f?fluid.gy/m:1;
    last_marker_x=(int)(116+x*108)-1;last_marker_y=(int)(116+y*108)-1;
    rectangle(last_marker_x,last_marker_y,3,3,t->c[5]);
    background_valid=1;background_theme=render_theme;
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
    wave_step(&fluid,WAVE_STEP_DT);
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
    grid_backend=!WAVE_LEGACY_GRAPHICS&&(gfx_caps()&(TINYRT_GFX_CAP_RECT|TINYRT_GFX_CAP_GRID))==(TINYRT_GFX_CAP_RECT|TINYRT_GFX_CAP_GRID);
    palette_valid=0;submitted_valid=0;
    if(!grid_backend&&allocate_legacy_frame()!=0)return -1;
    wave_reset(&fluid);theme=frame=accumulator=0;touch_down=key_down=theme_changed=frame_ready=0;
    background_valid=0;
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
        accumulator+=elapsed*WAVE_STEP_HZ;
        if(fast_backend){
            unsigned steps=0;while(accumulator>=1000u&&steps<8){physics_step();accumulator-=1000u;steps++;}
            if(accumulator>=1000u)accumulator%=1000u;
        } else {
            if(accumulator>8000u)accumulator=8000u;
            if(compose_stage>=0)compose_work();
            else if(physics_active){if(physics_work()){physics_active=0;if((uint32_t)(now-render_ms)>=33u){render_ms=now-(uint32_t)(now-render_ms)%33u;compose_begin();}}}
            else if(accumulator>=1000u){accumulator-=1000u;wave_begin(&fluid,WAVE_STEP_DT);physics_active=1;}
            else if((uint32_t)(now-render_ms)>=33u){render_ms=now-(uint32_t)(now-render_ms)%33u;compose_begin();}
        }
        if(touch_down&&!theme_changed&&(uint32_t)(now-touch_ms)>=500u)next_theme(now);
        if(fast_backend&&(uint32_t)(now-render_ms)>=33u){render_ms=now-(uint32_t)(now-render_ms)%33u;compose();}
    }
    return 0;
}
int32_t tinyrt_render(void) {
    if(!frame_ready)return draw_skip();frame_ready=0;
    int toast=render_theme==theme&&(uint32_t)(now_ms()-toast_ms)<1500u;
    if(grid_backend){
        damage_t d={0};
        if(!submitted_valid||submitted_theme!=render_theme)damage_add(&d,0,0,466,466);
        else {
            for(unsigned i=0;i<WAVE_CELLS;i++)if(grid_cells[i]!=submitted_cells[i])
                damage_add(&d,34+(int)(i%WAVE_GRID)*WAVE_LED_CELL*2,34+(int)(i/WAVE_GRID)*WAVE_LED_CELL*2,(WAVE_LED_CELL-1)*2,(WAVE_LED_CELL-1)*2);
            if(last_marker_x!=submitted_marker_x||last_marker_y!=submitted_marker_y){
                damage_add(&d,last_marker_x*2,last_marker_y*2,6,6);
                damage_add(&d,submitted_marker_x*2,submitted_marker_y*2,6,6);
            }
            if(toast!=submitted_toast)damage_add(&d,133,126,200,36);
        }
#ifdef WAVE_METRICS
        damage_add(&d,0,0,466,466);
#endif
        if(!d.valid)return draw_skip();
        if(gfx_failed||gfx_begin(0)!=0||gfx_submit(graphics.bytes,graphics_length)!=0||
           gfx_damage(d.x,d.y,d.right-d.x,d.bottom-d.y)!=0||gfx_end()!=0)return -1;
        for(unsigned i=0;i<WAVE_CELLS;i++)submitted_cells[i]=grid_cells[i];
        submitted_valid=1;submitted_theme=render_theme;submitted_marker_x=last_marker_x;
        submitted_marker_y=last_marker_y;submitted_toast=toast;
    }else {
        if(draw_clear(0)!=0)return -1;
        if(draw_rgb565_scaled(0,0,466,466,233,233,pixels,WAVE_PIXEL_BYTES)!=0)return -1;
    }
    if(toast) {
        unsigned n=0;while(themes[theme].name[n])n++;
        if(draw_text_box(133,126,200,36,themes[theme].name,n,themes[theme].ink,24,1)!=0)return -1;
    }
#ifdef WAVE_METRICS
    return metrics_draw(now_ms());
#endif
    return 0;
}
int32_t tinyrt_stop(void){touch_down=key_down=0;return 0;}
