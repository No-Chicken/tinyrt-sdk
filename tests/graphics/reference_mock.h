/* Independent full-canvas record interpreter for application pixel regression. */
#include <assert.h>
#include <string.h>
static uint32_t test_caps;
static uint16_t test_palette[256],test_canvas[466*466];
static const uint8_t *test_texture;
static uint32_t test_texture_width,test_texture_height;
static int test_damage_x,test_damage_y,test_damage_w,test_damage_h;
int32_t gfx_damage(int32_t x,int32_t y,int32_t w,int32_t h){
    assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=466&&y+h<=466);
    test_damage_x=x;test_damage_y=y;test_damage_w=w;test_damage_h=h;return 0;
}
uint32_t gfx_caps(void){return test_caps;}
int32_t gfx_pal_upload(uint32_t slot,uint32_t first,uint32_t count,const uint16_t *p){
    assert(slot==0&&first+count<=256);memcpy(test_palette+first,p,count*2);return 0;
}
int32_t gfx_begin(uint32_t flags){assert(flags==0);memset(test_canvas,0,sizeof(test_canvas));return 0;}
int32_t gfx_end(void){return 0;}
static void test_fill(int x,int y,int w,int h,uint16_t color){
    assert(x>=0&&y>=0&&x+w<=466&&y+h<=466);
    for(int row=y;row<y+h;row++)for(int col=x;col<x+w;col++)test_canvas[row*466+col]=color;
}
int32_t gfx_submit(const void *data,uint32_t length){
    const uint8_t *p=data;uint32_t at=0;
    while(at<length){
        const tinyrt_gfx_record_t *head=(const void *)(p+at);
        assert(head->size&&head->size<=length-at);
        if(head->op==TINYRT_GFX_CLEAR){
            assert(head->size==8);
            const uint8_t *c=(const uint8_t *)head+4;
            test_fill(0,0,466,466,(uint16_t)(c[0]|c[1]<<8));
        }else if(head->op==TINYRT_GFX_RECT){
            const tinyrt_gfx_rect_t *r=(const void *)head;assert(r->alpha==255);
            test_fill(r->x,r->y,r->w,r->h,(uint16_t)r->color);
        }else if(head->op==TINYRT_GFX_GRID){
            const tinyrt_gfx_grid_t *g=(const void *)head;
            const uint8_t *cells=(const uint8_t *)(g+1);
            assert(g->pal==0&&g->flags==0&&head->size==sizeof(*g)+g->cols*g->rows);
            for(uint32_t y=0;y<g->rows;y++)for(uint32_t x=0;x<g->cols;x++){
                uint8_t index=cells[y*g->cols+x];if(index)test_fill(g->x+(int)(x*g->cell_w),g->y+(int)(y*g->cell_h),
                    (int)(g->cell_w-g->gap),(int)(g->cell_h-g->gap),test_palette[index]);
            }
        }else if(head->op==TINYRT_GFX_SPRITE){
            const tinyrt_gfx_sprite_t *s=(const void *)head;
            assert(s->tex==0&&s->pal==0&&s->u+s->sw<=test_texture_width&&s->v+s->sh<=test_texture_height);
            for(int y=0;y<s->h;y++)for(int x=0;x<s->w;x++){
                uint32_t sx=(uint32_t)x*s->sw/(uint32_t)s->w,sy=(uint32_t)y*s->sh/(uint32_t)s->h;
                if(s->flags&TINYRT_GFX_FLIP_Y)sy=s->sh-1-sy;
                uint8_t index=test_texture[(s->v+sy)*test_texture_width+s->u+sx];
                if(index||!(s->flags&TINYRT_GFX_TRANSPARENT))test_canvas[(s->y+y)*466+s->x+x]=test_palette[index];
            }
        }else assert(!"unexpected record");
        at+=head->size;
    }
    assert(at==length);return 0;
}
static void test_equal_scaled(const uint8_t *legacy){
    for(unsigned y=0;y<466;y++)for(unsigned x=0;x<466;x++){
        unsigned offset=((y/2)*233+x/2)*2;
        assert(test_canvas[y*466+x]==(uint16_t)(legacy[offset]|legacy[offset+1]<<8));
    }
}
