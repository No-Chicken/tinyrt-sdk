#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "wave_balls.h"

/* 预生成带高光的球形图集；渲染提交快照命令，任务和 DMA 由宿主管理。 */
enum {DEPTHS=16,SPEEDS=64,TONES=7,TILE=15,ATLAS_W=DEPTHS*TILE,ATLAS_H=TONES*TILE};
static const uint32_t bases[5]={0x1e8cf0,0xc82ef0,0x22c43c,0xe8501a,0x7a8292};
static const char *names[5]={"OCEAN","NEON","TOXIC","LAVA","MONO"};
static wave_balls_t world;
static uint16_t body[2][DEPTHS][SPEEDS],shine[2][DEPTHS][SPEEDS];
static uint16_t palette[256];
static uint8_t atlas[ATLAS_W*ATLAS_H];
static uint8_t spans[8][15];
static int16_t bucket[DEPTHS],next_ball[WAVE_BALL_COUNT];
static unsigned theme,table_index,ready,pressed,changed,key_down;
static unsigned fast_backend,init_phase,init_cursor,physics_pending;
static unsigned compose_pending,compose_count,color_pending,color_cursor,requested_theme;
static unsigned splash_pending,splash_cursor;
static int compose_depth,compose_index;
static uint32_t last_ms,press_ms,key_ms,toast_ms,cadence;
static float gx,gy,gz;
static int previous_left,previous_top,previous_right,previous_bottom,previous_valid,previous_toast;
typedef struct {int32_t x,y;uint32_t u,v;} item_t;
static item_t projected[WAVE_BALL_COUNT];
static struct {uint16_t op,size;uint32_t tex,sw,sh,flags,pal,count;item_t item[1000];} batch;
static uint16_t rgb(unsigned r,unsigned g,unsigned b){return (uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3));}
static unsigned clamp(unsigned n,unsigned max){return n>max?max:n;}
static void build_colors_range(unsigned which,unsigned t,unsigned first,unsigned count){
    unsigned br=bases[t]>>16,bg=(bases[t]>>8)&255,bb=bases[t]&255;
    for(unsigned d=first;d<first+count;d++)for(unsigned s=0;s<SPEEDS;s++){
        unsigned dim=255-d*102/15;
        unsigned r=br+(255-br)*s/63,g=bg+(255-bg)*s/63,b=bb+(255-bb)*s/63;
        body[which][d][s]=rgb(r*dim/255,g*dim/255,b*dim/255);
        shine[which][d][s]=rgb((r+(255-r)*55/100)*dim/255,
                             (g+(255-g)*55/100)*dim/255,(b+(255-b)*55/100)*dim/255);
    }
}
static void build_colors(unsigned which,unsigned t){build_colors_range(which,t,0,DEPTHS);}
static int upload_colors(void){
    palette[0]=0;
    for(unsigned d=0;d<DEPTHS;d++)for(unsigned s=0;s<TONES;s++){
        unsigned k=1+(d*TONES+s)*2,level=s*63/(TONES-1);
        palette[k]=body[table_index][d][level];palette[k+1]=shine[table_index][d][level];
    }
    return gfx_pal_upload(0,0,256,palette);
}
static int next_theme(uint32_t now){
    if(!fast_backend){requested_theme=(theme+1)%5;color_pending=1;color_cursor=0;changed=1;return 0;}
    unsigned replacement=table_index^1,new_theme=(theme+1)%5;
    build_colors(replacement,new_theme);table_index=replacement;theme=new_theme;
    toast_ms=now;changed=1;return upload_colors();
}
static void splash(void){
    if(fast_backend)wave_balls_splash(&world);
    else {splash_pending=1;splash_cursor=0;}
}
static void disc(unsigned ox,unsigned oy,int cx,int cy,int r,uint8_t color){
    for(int dy=-r;dy<=r;dy++){
        int half=spans[r][dy+r],y=cy+dy;if(y<0||y>=TILE)continue;
        for(int x=cx-half;x<=cx+half;x++)if(x>=0&&x<TILE)
            atlas[(oy+(unsigned)y)*ATLAS_W+ox+(unsigned)x]=color;
    }
}
static void build_spans(void){
    for(int r=1;r<=7;r++)for(int dy=-r;dy<=r;dy++){
        int x=0;while((x+1)*(x+1)+dy*dy<=r*r)x++;spans[r][dy+r]=(uint8_t)x;
    }
}
static void build_atlas_range(unsigned first,unsigned count){
    for(unsigned d=first;d<first+count;d++)for(unsigned s=0;s<TONES;s++){
        int r=7-(int)d*3/15;uint8_t color=(uint8_t)(1+(d*TONES+s)*2);
        disc(d*TILE,s*TILE,7,7,r,color);
        disc(d*TILE,s*TILE,7-r/3,7-r/3,r/2,(uint8_t)(color+1));
    }
}
static void compose_begin(void){
    for(unsigned d=0;d<DEPTHS;d++)bucket[d]=-1;
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
        unsigned d=clamp((unsigned)(world.p[i].z*15/(WAVE_BALL_DEPTH*128)),15);
        next_ball[i]=bucket[d];bucket[d]=(int16_t)i;
    }
    compose_depth=DEPTHS-1;compose_index=bucket[compose_depth];compose_count=0;compose_pending=1;
}
static int compose_work(unsigned count){
    while(count--&&compose_count<WAVE_BALL_COUNT){
        while(compose_index<0&&compose_depth>0)compose_index=bucket[--compose_depth];
        if(compose_index<0)return -1;
        wave_ball_view_t value=wave_balls_read(&world,(unsigned)compose_index);
        const wave_ball_view_t *p=&value;float scale=160/(160+p->z);
        float speed=__builtin_sqrtf(p->vx*p->vx+p->vy*p->vy+p->vz*p->vz);
        unsigned tone=clamp((unsigned)(speed*(TONES-1)/180),TONES-1);
        item_t *it=&projected[compose_count++];
        it->x=233+(int)(p->x*scale)-7;it->y=233+(int)(p->y*scale)-7;
        it->u=(unsigned)compose_depth*TILE;it->v=tone*TILE;
        compose_index=next_ball[compose_index];
    }
    if(compose_count==WAVE_BALL_COUNT){compose_pending=0;ready=1;return 1;}
    return 0;
}
int32_t tinyrt_init(int32_t w,int32_t h){
    if(w!=466||h!=466||(gfx_caps()&TINYRT_GFX_CAP_SPRITE_BATCH)==0)return -1;
    theme=table_index=cadence=pressed=changed=key_down=physics_pending=compose_pending=color_pending=splash_pending=0;
    fast_backend=runtime_backend();init_phase=fast_backend?0:1;init_cursor=0;
    gx=0;gy=1;gz=0;last_ms=now_ms();toast_ms=last_ms-1500u;ready=1;previous_valid=previous_toast=0;
    build_spans();
    if(fast_backend){
        wave_balls_reset(&world);build_colors(0,0);build_atlas_range(0,DEPTHS);
        if(upload_colors()!=0||gfx_tex_upload(0,TINYRT_GFX_INDEX8,ATLAS_W,ATLAS_H,atlas,sizeof(atlas),0)!=0)return -1;
        compose_begin();if(compose_work(WAVE_BALL_COUNT)!=1)return -1;
    }
    if(input_events(248u)!=0)return -1;
    return clock_interval(fast_backend?17:1);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){
    uint32_t now=now_ms();
    if(kind==TINYRT_EV_MOTION){
        gx=(float)x/1000;gy=-(float)y/1000;gz=(float)arg/1000;
        float m=__builtin_sqrtf(gx*gx+gy*gy+gz*gz);if(m>1){gx/=m;gy/=m;gz/=m;}
    }else if(kind==TINYRT_TOUCH_PRESS&&!pressed){pressed=1;press_ms=now;changed=0;}
    else if(kind==TINYRT_TOUCH_RELEASE){
        if(pressed&&!changed){if(now-press_ms<500u)splash();else if(next_theme(now)!=0)return -1;}pressed=0;
    }else if(kind==TINYRT_TOUCH_CANCEL){pressed=key_down=0;}
    else if(kind==TINYRT_USER_KEY_EVENT&&x==1){
        if(y&&!key_down){key_down=1;key_ms=now;}
        else if(!y){if(key_down&&now-key_ms<500u)splash();key_down=0;}
    }else if(kind==TINYRT_CLOCK_EVENT){
        if(init_phase){
            if(init_phase==1){wave_balls_reset_range(&world,init_cursor,16);init_cursor+=16;
                if(init_cursor>=WAVE_BALL_COUNT){init_phase=2;init_cursor=0;}}
            else if(init_phase==2){build_colors_range(0,0,init_cursor++,1);
                if(init_cursor==DEPTHS){init_phase=3;init_cursor=0;}}
            else if(init_phase==3){build_atlas_range(init_cursor++,1);
                if(init_cursor==DEPTHS)init_phase=4;}
            else {
                if(upload_colors()!=0||gfx_tex_upload(0,TINYRT_GFX_INDEX8,ATLAS_W,ATLAS_H,atlas,sizeof(atlas),0)!=0)return -1;
                init_phase=0;ready=0;last_ms=now;compose_begin();
            }
            return 0;
        }
        if(pressed&&!changed&&now-press_ms>=500u&&next_theme(now)!=0)return -1;
        if(color_pending){
            build_colors_range(table_index^1,requested_theme,color_cursor++,1);
            if(color_cursor==DEPTHS){table_index^=1;theme=requested_theme;toast_ms=now;color_pending=0;if(upload_colors()!=0)return -1;}
        }
        if(splash_pending){
            wave_balls_splash_range(&world,splash_cursor,16);splash_cursor+=16;
            if(splash_cursor>=WAVE_BALL_COUNT)splash_pending=0;
            return 0;
        }
        if(!fast_backend&&compose_pending){if(compose_work(32)<0)return -1;return 0;}
        uint32_t elapsed=now-last_ms;last_ms=now;
        if(fast_backend){if(elapsed)wave_balls_step(&world,gx,gy,gz,(float)(elapsed>50?50:elapsed)*.001f);}
        else {
            if(!physics_pending){wave_balls_begin(&world,gx,gy,gz,1.0f/60.0f);physics_pending=1;}
            if(!wave_balls_work(&world,4))return 0;
            physics_pending=0;compose_begin();return 0;
        }
        compose_begin();if(compose_work(WAVE_BALL_COUNT)!=1)return -1;
        /* 17,17,16 毫秒，均值为 60 Hz；不改变宿主菜单时钟。 */
        cadence=(cadence+1)%3;return clock_interval(cadence==2?16:17);
    }
    return 0;
}
static int flush_batch(void){
    if(!batch.count)return 0;batch.size=(uint16_t)(28+batch.count*sizeof(item_t));
    int result=gfx_submit(&batch,batch.size);batch.count=0;return result;
}
int32_t tinyrt_render(void){
    if(init_phase||!ready)return draw_skip();ready=0;
    struct {uint16_t op,size;uint32_t color;} clear={TINYRT_GFX_CLEAR,8,0};
    if(gfx_begin(0)!=0||gfx_submit(&clear,8)!=0)return -1;
    batch.op=TINYRT_GFX_SPRITE_BATCH;batch.tex=0;batch.sw=batch.sh=TILE;
    batch.flags=TINYRT_GFX_TRANSPARENT;batch.pal=0;batch.count=0;
    int left=466,top=466,right=0,bottom=0;
    /* 深度桶从远到近；图集已包含透视半径、速度颜色和高光。 */
    for(unsigned i=0;i<WAVE_BALL_COUNT;i++){
        item_t *it=&batch.item[batch.count++];*it=projected[i];
        if(it->x<left)left=it->x;if(it->y<top)top=it->y;
        if(it->x+TILE>right)right=it->x+TILE;if(it->y+TILE>bottom)bottom=it->y+TILE;
        if(batch.count==1000&&flush_batch()!=0)return -1;
    }
    int toast=now_ms()-toast_ms<1500u;
    int dl=left,dt=top,dr=right,db=bottom;
    if(previous_valid){
        if(previous_left<dl)dl=previous_left;if(previous_top<dt)dt=previous_top;
        if(previous_right>dr)dr=previous_right;if(previous_bottom>db)db=previous_bottom;
    }else {dl=dt=0;dr=db=466;}
    if(toast||previous_toast){if(dl>133)dl=133;if(dt>126)dt=126;if(dr<333)dr=333;if(db<162)db=162;}
    dl&=~15;dr=(dr+15)&~15;if(dr>466)dr=466;
    if(flush_batch()!=0||gfx_damage(dl,dt,dr-dl,db-dt)!=0||gfx_end()!=0)return -1;
    previous_left=left;previous_top=top;previous_right=right;previous_bottom=bottom;
    previous_valid=1;previous_toast=toast;
    if(toast){unsigned n=0;while(names[theme][n])n++;
        return draw_text_box(133,126,200,36,names[theme],n,0xffffff,24,1);}
    return 0;
}
int32_t tinyrt_stop(void){pressed=key_down=ready=0;return 0;}
