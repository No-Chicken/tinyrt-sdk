#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../examples/wave_sim/balls_main.c"
static uint32_t clock_ms,sent,depth_before;
static uint16_t test_palette[256],canvas[466*466];
static const uint8_t *test_atlas;
uint32_t now_ms(void){return clock_ms;}
int32_t runtime_backend(void){return 1;}
uint32_t gfx_caps(void){return TINYRT_GFX_CAPS;}
int32_t clock_interval(int32_t ms){assert(ms==16||ms==17);return 0;}
int32_t input_events(uint32_t mask){assert(mask==248);return 0;}
int32_t draw_skip(void){return 0;}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *p,uint32_t n,uint32_t c,int32_t px,int32_t a){
    (void)x;(void)y;(void)w;(void)h;(void)p;(void)n;(void)c;(void)px;(void)a;return 0;
}
int32_t gfx_pal_upload(uint32_t slot,uint32_t first,uint32_t count,const uint16_t *p){
    assert(slot==0&&first==0&&count==256);memcpy(test_palette,p,512);return 0;
}
int32_t gfx_tex_upload(uint32_t slot,uint32_t format,uint32_t w,uint32_t h,const void *p,uint32_t n,uint32_t flags){
    assert(slot==0&&format==1&&w==ATLAS_W&&h==ATLAS_H&&n==sizeof(atlas)&&flags==0);test_atlas=p;return 0;
}
int32_t gfx_begin(uint32_t flags){assert(flags==0);sent=0;depth_before=16;return 0;}
int32_t gfx_end(void){assert(sent==WAVE_BALL_COUNT);return 0;}
int32_t gfx_damage(int32_t x,int32_t y,int32_t w,int32_t h){
    assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=466&&y+h<=466);
    assert(x%16==0&&(x+w==466||(x+w)%16==0));
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
        wave_ball_view_t p=wave_balls_read(&world,i);float scale=160/(160+p.z);
        int px=233+(int)(p.x*scale)-7,py=233+(int)(p.y*scale)-7;
        assert(px>=x&&py>=y&&px+TILE<=x+w&&py+TILE<=y+h);
    }
    return 0;
}
int32_t gfx_submit(const void *records,uint32_t bytes){
    const tinyrt_gfx_record_t *r=records;assert(bytes<=TINYRT_GFX_MAX_SUBMIT&&r->size==bytes);
    if(r->op==TINYRT_GFX_CLEAR){assert(bytes==8);memset(canvas,0,sizeof(canvas));return 0;}
    assert(r->op==TINYRT_GFX_SPRITE_BATCH);const uint32_t *p=(const void *)(r+1);
    assert(p[0]==0&&p[1]==TILE&&p[2]==TILE&&p[3]==1&&p[4]==0&&p[5]<=1000);
    const item_t *items=(const void *)(p+6);assert(bytes==28+p[5]*16);
    for(unsigned i=0;i<p[5];i++){
        const item_t *a=items+i;assert(a->u+TILE<=ATLAS_W&&a->v+TILE<=ATLAS_H);
        assert(a->u/TILE<=depth_before);depth_before=a->u/TILE;sent++;
        for(int y=0;y<TILE;y++)for(int x=0;x<TILE;x++){
            int xx=a->x+x,yy=a->y+y;assert(xx>=0&&xx<466&&yy>=0&&yy<466);
            uint8_t k=test_atlas[(a->v+(unsigned)y)*ATLAS_W+a->u+(unsigned)x];
            if(k)canvas[yy*466+xx]=test_palette[k];
        }
    }
    return 0;
}
int main(int argc,char **argv){
    assert(tinyrt_init(466,466)==0&&tinyrt_render()==0);
    for(unsigned n=1;n<=180;n++){clock_ms=n*1000/60;assert(tinyrt_event(TINYRT_CLOCK_EVENT,0,0,0)==0);assert(tinyrt_render()==0);}
    assert(body[0][0][0]!=body[0][15][0]&&shine[0][0][0]!=body[0][0][0]);
    if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f);assert(fwrite(canvas,2,466*466,f)==466*466);assert(fclose(f)==0);}
    uint32_t before=world.random;assert(tinyrt_event(TINYRT_TOUCH_PRESS,200,200,0)==0);
    clock_ms+=501;assert(tinyrt_event(TINYRT_CLOCK_EVENT,0,0,0)==0);assert(theme==1&&world.random==before);
    clock_ms+=501;assert(tinyrt_event(TINYRT_CLOCK_EVENT,0,0,0)==0);assert(theme==1);
    assert(tinyrt_event(TINYRT_TOUCH_CANCEL,0,0,0)==0);assert(!pressed&&!key_down);
    assert(tinyrt_stop()==0);
    puts("3D projection, depth order, atlas, bounded batches, long press and cancel passed");return 0;
}
