#include "tinyrt.h"
#include "tinyrt_gfx.h"

/* Round-native scene: Host clips the full 466-square canvas to the display. */
#define BIRD_X 160
#define PIPE_W 58
#define GAP_HALF 82
#define GROUND_Y 396
typedef struct { uint16_t x,width; uint32_t offset; } span_t;
typedef struct { int width,height; const uint16_t *rows; const span_t *spans; int atlas_x,atlas_y,opaque; } sprite_t;
#include "assets_generated.h"
#define SFX(name) do { if(audio_play(SOUND_##name##_OFFSET,SOUND_##name##_LENGTH,16000)<0) return -1; } while(0)
static int compose_strip(void);
#define BIRD_PIXEL_BYTES (233u*233u*2u)
#if defined(__wasm__)
static uint8_t *pixels;
extern unsigned char __heap_base;
static int allocate_legacy_frame(void){
    uintptr_t start=((uintptr_t)&__heap_base+3u)&~(uintptr_t)3u;
    uint32_t pages=(uint32_t)((start+BIRD_PIXEL_BYTES+65535u)/65536u);
    uint32_t current=(uint32_t)__builtin_wasm_memory_size(0);
    if(pages>current&&__builtin_wasm_memory_grow(0,pages-current)==(size_t)-1)return -1;
    pixels=(uint8_t *)start;return 0;
}
#else
static uint8_t pixels[BIRD_PIXEL_BYTES];
static int allocate_legacy_frame(void){return 0;}
#endif
static int sprite_backend;
#ifndef BIRD_LEGACY_GRAPHICS
#define BIRD_LEGACY_GRAPHICS 0
#endif
static union {uint32_t align;uint8_t bytes[4096];} graphics;
static uint32_t graphics_length;
static int stripe,frame_ready,die_sound,run_best;
#define D(call) do { if ((call) != 0) return -1; } while (0)
enum { READY, RUNNING, GAME_OVER };
typedef struct { int x, gap, scored; } pipe_t;
static pipe_t pipes[3];
static int state, bird_y, velocity, score, best, saved_best, touch_down, key_down;
static uint32_t last_ms, remainder, died_ms, seed, scenery;

static int next_gap(void) {
    seed=seed*1664525u+1013904223u;
    return 200+(int)((seed>>16)%61u);
}
static void reset_game(void) {
    run_best=best;bird_y=220*256;velocity=0;score=0;remainder=0;scenery=0;
    seed=0x51a7u;
    for(int i=0;i<3;i++) pipes[i]=(pipe_t){430+i*225,next_gap(),0};
    last_ms=now_ms();
}
static int save_best(void) {
    if(score>best) best=score;
    if(best==saved_best) return 0;
    if(kv_set(0,best)!=0) return -1;
    saved_best=best;return 0;
}
static int flap(void) {
    if(state==GAME_OVER) {
        if((uint32_t)(now_ms()-died_ms)<800u) return 0;
        if(save_best()!=0) return -1;
        reset_game();
    }
    state=RUNNING;velocity=-1320;SFX(WING);return 0;
}
static int collide(void) {
    int y=bird_y/256;
    if(y<29 || y+12>=GROUND_Y) return 1;
    for(int i=0;i<3;i++) {
        pipe_t *p=&pipes[i];
        if(BIRD_X+13>p->x-5 && BIRD_X-13<p->x+PIPE_W+5 &&
           (y-11<p->gap-GAP_HALF || y+11>p->gap+GAP_HALF)) return 1;
    }
    return 0;
}
/* 每次时钟只合成四分之一画面，完整帧才提交，避免解释器预算超限。 */
typedef struct { pipe_t pipes[3];int state,y,score,best,old_best,animation,scroll,show_panel; } scene_t;
static scene_t scene;
static int strip_top,strip_bottom;
static int blit(const sprite_t *image,int x,int y,int flip) {
    if(sprite_backend){
        int left=x<0?0:x,top=y<strip_top?strip_top:y;
        int right=x+image->width,bottom=y+image->height;
        if(right>233)right=233;if(bottom>strip_bottom)bottom=strip_bottom;
        if(right<=left||bottom<=top)return 0;
        if(graphics_length+sizeof(tinyrt_gfx_sprite_t)>sizeof(graphics.bytes))return -1;
        tinyrt_gfx_sprite_t record={TINYRT_GFX_SPRITE,sizeof(record),left*2,top*2,(right-left)*2,(bottom-top)*2,
            0,(uint32_t)(image->atlas_x+left-x),(uint32_t)(image->atlas_y+(flip?image->height-(bottom-y):top-y)),
            (uint32_t)(right-left),(uint32_t)(bottom-top),(image->opaque?0:TINYRT_GFX_TRANSPARENT)|(flip?TINYRT_GFX_FLIP_Y:0),0,0};
        const uint8_t *p=(const uint8_t *)&record;
        for(unsigned i=0;i<sizeof(record);i++)graphics.bytes[graphics_length+i]=p[i];
        graphics_length+=sizeof(record);return 0;
    }
    int first=strip_top-y,last=strip_bottom-y;
    if(first<0) first=0;if(last>image->height) last=image->height;
    for(int row=first;row<last;row++) {
        int source=flip?image->height-1-row:row;
        for(int j=image->rows[source];j<image->rows[source+1];j++) {
            const span_t *run=&image->spans[j];
            int left=x+run->x,count=run->width,skip=0;
            if(left<0) { skip=-left;count-=skip;left=0; }
            if(left+count>233) count=233-left;
            if(count>0 && asset_read(run->offset+(uint32_t)skip*2,
                pixels+((y+row)*233+left)*2,(uint32_t)count*2)!=count*2) return -1;
        }
    }
    return 0;
}
static const sprite_t *const digits[]={&digit0,&digit1,&digit2,&digit3,&digit4,&digit5,&digit6,&digit7,&digit8,&digit9};
static const sprite_t *const smalls[]={&small0,&small1,&small2,&small3,&small4,&small5,&small6,&small7,&small8,&small9};
static int draw_number(int value,int anchor,int y,int small,int center) {
    int items[4],count=0,width=0;
    const sprite_t *const *font=small?smalls:digits;
    do { items[count]=value%10;width+=font[items[count]]->width+1;count++;value/=10; } while(value && count<4);
    int x=anchor-(center?width/2:width);
    while(count) { const sprite_t *image=font[items[--count]];D(blit(image,x,y,0));x+=image->width+1; }
    return 0;
}
static int compose_strip(void) {
    if(sprite_backend&&stripe){if(++stripe==4){stripe=0;frame_ready=1;}return 0;}
    if(stripe==0) {
        if(sprite_backend)graphics_length=0;
        for(int i=0;i<3;i++) scene.pipes[i]=pipes[i];
        scene.state=state;scene.y=bird_y/512;scene.score=score;scene.best=best;
        scene.old_best=run_best;scene.scroll=(int)(scenery*3u/2u%168u);
        scene.animation=(int)(now_ms()/100u%3u);
        scene.show_panel=state==GAME_OVER && (uint32_t)(now_ms()-died_ms)>=600u;
    }
    strip_top=stripe*59;strip_bottom=strip_top+59;if(strip_bottom>233) strip_bottom=233;
    if(sprite_backend){strip_top=0;strip_bottom=233;D(blit(&background,0,0,0));}
    uint32_t start=(uint32_t)strip_top*466,end=(uint32_t)strip_bottom*466;
    for(uint32_t offset=start;!sprite_backend&&offset<end;) {
        uint32_t count=end-offset;if(count>4096) count=4096;
        if(asset_read(offset,pixels+offset,count)!=(int32_t)count) return -1;
        offset+=count;
    }
    /* 管道在地面后结束；上管道垂直翻转原版精灵。 */
    int ground_clip=strip_bottom;if(strip_bottom>198) strip_bottom=198;
    if(scene.state!=READY) for(int i=0;i<3;i++) {
        pipe_t *p=&scene.pipes[i];
        D(blit(&pipe,(p->x-5)/2,(p->gap-GAP_HALF)/2-198,1));
        D(blit(&pipe,(p->x-5)/2,(p->gap+GAP_HALF)/2,0));
    }
    strip_bottom=ground_clip;
    for(int i=0;i<3;i++) D(blit(&ground,i*168-scene.scroll,198,0));
    const sprite_t *birds[]={&bird0,&bird1,&bird2};
    D(blit(scene.state==GAME_OVER?&bird_dead:birds[scene.animation],
        BIRD_X/2-(scene.state==GAME_OVER?8:12),scene.y-(scene.state==GAME_OVER?12:8),0));
    if(scene.state==READY) {
        D(blit(&ready,49,61,0));D(blit(&tap,87,134,0));
    } else if(scene.show_panel) {
        D(blit(&game_over,45,49,0));D(blit(&panel,33,88,0));
        D(draw_number(scene.score,180,114,1,0));D(draw_number(scene.best,180,144,1,0));
        if(scene.score>scene.old_best) D(blit(&new_best,130,132,0));
        if(scene.score>=10) D(blit(scene.score>=20?&medal_gold:&medal_silver,56,119,0));
        D(blit(&okay,85,181,0));
    } else D(draw_number(scene.score,117,44,0,1));
    stripe++;
    if(stripe==4) { stripe=0;frame_ready=1; }
    return 0;
}
int32_t tinyrt_render(void) {
    if(!frame_ready) return draw_skip();
    frame_ready=0;
    if(sprite_backend){D(gfx_begin(0));D(gfx_submit(graphics.bytes,graphics_length));return gfx_end();}
    D(draw_clear(0x70c5ceu));
    return draw_rgb565_scaled(0,0,466,466,233,233,pixels,BIRD_PIXEL_BYTES);
}
int32_t tinyrt_stop(void) { touch_down=key_down=0;return save_best(); }
static int tick_game(void) {
    scenery++;
    velocity+=94;if(velocity>2100) velocity=2100;
    bird_y+=velocity;
    for(int i=0;i<3;i++) {
        pipe_t *p=&pipes[i];p->x-=3;
        if(p->x+PIPE_W+5<BIRD_X-13 && !p->scored) {
            p->scored=1;if(score<9999) score++;SFX(POINT);
        }
        if(p->x<-PIPE_W-10) {
            int far=0;for(int j=0;j<3;j++) if(pipes[j].x>far) far=pipes[j].x;
            *p=(pipe_t){far+225,next_gap(),0};
        }
    }
    if(collide()) { state=GAME_OVER;died_ms=now_ms();die_sound=0;SFX(HIT);return save_best(); }
    return 0;
}
int32_t tinyrt_init(int32_t width,int32_t height) {
    if(width!=466 || height!=466) return -1;
    sprite_backend=!BIRD_LEGACY_GRAPHICS&&(gfx_caps()&TINYRT_GFX_CAP_SPRITE)!=0;
    if(sprite_backend){
        uint16_t palette[256];
        D(asset_read(RESIDENT_PALETTE_OFFSET,palette,RESIDENT_PALETTE_COUNT*2)!=(int32_t)(RESIDENT_PALETTE_COUNT*2));
        D(gfx_pal_upload(0,0,RESIDENT_PALETTE_COUNT,palette));
        D(gfx_tex_upload(0,TINYRT_GFX_INDEX8,RESIDENT_ATLAS_WIDTH,RESIDENT_ATLAS_HEIGHT,
            (const void *)(uintptr_t)RESIDENT_ATLAS_OFFSET,RESIDENT_ATLAS_LENGTH,TINYRT_GFX_FROM_ASSET));
    }else D(allocate_legacy_frame());
    best=kv_get(0,0);if(best<0 || best>9999) best=0;
    saved_best=best;state=READY;touch_down=key_down=0;reset_game();
    D(input_events(TINYRT_INPUT_LIFECYCLE_MASK|TINYRT_INPUT_USER_KEY_MASK));
    stripe=frame_ready=0;return clock_interval(8);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    (void)arg;
    if(kind==TINYRT_TOUCH_PRESS) {
        if(!touch_down) { touch_down=1;
            if(state!=GAME_OVER || (x>=171 && x<=295 && y>=362 && y<=406)) D(flap());
        }
    } else if(kind==TINYRT_TOUCH_RELEASE) touch_down=0;
    else if(kind==TINYRT_TOUCH_CANCEL) touch_down=key_down=0;
    else if(kind==TINYRT_USER_KEY_EVENT && x==1) {
        if(y && !key_down) { key_down=1;D(flap()); }
        else if(!y) key_down=0;
    } else if(kind==TINYRT_CLOCK_EVENT) {
        uint32_t now=now_ms(),elapsed=now-last_ms;last_ms=now;
        if(elapsed>132u) elapsed=132u;
        if(state==RUNNING) {
            remainder+=elapsed;
            while(remainder>=33u && state==RUNNING) { remainder-=33u;D(tick_game()); }
        } else if(state==GAME_OVER) {
            if(!die_sound && (uint32_t)(now-died_ms)>=120u) { SFX(DIE);die_sound=1; }
            remainder+=elapsed;
            while(remainder>=33u) {
                remainder-=33u;
                if(bird_y/256<GROUND_Y-12) { velocity+=180;bird_y+=velocity; }
                if(bird_y/256>GROUND_Y-12) bird_y=(GROUND_Y-12)*256;
            }
        }
        D(compose_strip());
    }
    return 0;
}
