#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "wave_balls.h"

/* 预生成带高光的球形图集；渲染提交快照命令，任务和 DMA 由宿主管理。 */
enum {DEPTHS=8,SPEEDS=64,TONES=4,SHADES=7,TILE=15,ATLAS_W=DEPTHS*TILE,ATLAS_H=TONES*TILE};
static const uint32_t bases[5]={0x1e8cf0,0xc82ef0,0x22c43c,0xe8501a,0x7a8292};
static const char *names[5]={"OCEAN","NEON","TOXIC","LAVA","MONO"};
static wave_balls_t world;
typedef struct {int16_t x,y,z;} position_t;
static position_t snapshots[2][WAVE_BALL_COUNT],view_positions[WAVE_BALL_COUNT];
static unsigned snapshot_index,snapshot_valid;
static uint8_t snapshot_tones[WAVE_BALL_COUNT];
static uint32_t snapshot_ms,snapshot_span,physics_started,physics_steps,display_frames;
static unsigned capture_pending,capture_cursor;
static uint32_t capture_time;
/* Sorting never changes the identity indexing these immutable snapshots. */
static void capture_range(unsigned first,unsigned count){
    unsigned end=first+count;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    for(unsigned i=first;i<end;i++){
        wave_ball_t *p=&world.p[i];
        snapshots[snapshot_index^1][p->id]=(position_t){p->x,p->y,p->z};
        float speed=__builtin_sqrtf((float)p->vx*p->vx+(float)p->vy*p->vy+(float)p->vz*p->vz)/32.0f;
        unsigned tone=(unsigned)(speed*(TONES-1)/180);
        snapshot_tones[p->id]=(uint8_t)(tone>=TONES?TONES-1:tone);
    }
}
static void finish_snapshot(uint32_t now){
    snapshot_index^=1;
    if(!snapshot_valid){
        for(unsigned i=0;i<WAVE_BALL_COUNT;i++)snapshots[snapshot_index^1][i]=snapshots[snapshot_index][i];
        snapshot_span=33;snapshot_valid=1;
    }else {snapshot_span=now-snapshot_ms;if(snapshot_span<16)snapshot_span=16;if(snapshot_span>100)snapshot_span=100;}
    snapshot_ms=now;
}
static void capture_snapshot(uint32_t now){capture_range(0,WAVE_BALL_COUNT);finish_snapshot(now);}
static void sample_range(uint32_t now,unsigned first,unsigned count){
    uint32_t age=now-snapshot_ms,alpha=age>=snapshot_span?256:age*256/snapshot_span;
    unsigned end=first+count;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    for(unsigned i=first;i<end;i++){
        position_t a=snapshots[snapshot_index^1][i],b=snapshots[snapshot_index][i];
        view_positions[i]=(position_t){(int16_t)(a.x+((int32_t)b.x-a.x)*(int32_t)alpha/256),
            (int16_t)(a.y+((int32_t)b.y-a.y)*(int32_t)alpha/256),
            (int16_t)(a.z+((int32_t)b.z-a.z)*(int32_t)alpha/256)};
    }
}

static void sample_positions(uint32_t now){sample_range(now,0,WAVE_BALL_COUNT);}
static uint16_t body[2][DEPTHS][SPEEDS],shine[2][DEPTHS][SPEEDS];
static uint16_t palette[256];
static uint8_t atlas[ATLAS_W*ATLAS_H];

static int16_t bucket[DEPTHS],next_ball[WAVE_BALL_COUNT];
static unsigned theme,table_index,ready,pressed,changed,key_down;
static unsigned fast_backend,init_phase,init_cursor,physics_pending;
static unsigned compose_pending,compose_count,color_pending,color_cursor,requested_theme;
static unsigned splash_pending,splash_cursor;
static int compose_depth,compose_index;
static unsigned compose_stage,compose_cursor;
static uint32_t compose_time;
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
        unsigned dim=255-d*102/(DEPTHS-1);
        unsigned r=br+(255-br)*s/63,g=bg+(255-bg)*s/63,b=bb+(255-bb)*s/63;
        body[which][d][s]=rgb(r*dim/255,g*dim/255,b*dim/255);
        shine[which][d][s]=rgb((r+(255-r)*55/100)*dim/255,
                             (g+(255-g)*55/100)*dim/255,(b+(255-b)*55/100)*dim/255);
    }
}
static void build_colors(unsigned which,unsigned t){build_colors_range(which,t,0,DEPTHS);}
static int upload_colors(void){
    palette[0]=0;
    for(unsigned d=0;d<DEPTHS;d++)for(unsigned t=0;t<TONES;t++){
        uint16_t base=body[table_index][d][t*63/(TONES-1)];
        unsigned r=((base>>11)&31)*255/31,g=((base>>5)&63)*255/63,b=(base&31)*255/31;
        for(unsigned shade=0;shade<SHADES;shade++){
            unsigned light=65+shade*190/(SHADES-1);
            unsigned rr=r*light/255,gg=g*light/255,bb=b*light/255;
            if(shade==SHADES-1){rr+=(255-rr)*3/4;gg+=(255-gg)*3/4;bb+=(255-bb)*3/4;}
            palette[1+(d*TONES+t)*SHADES+shade]=rgb(rr,gg,bb);
        }
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
    splash_pending=1;splash_cursor=0;
}
static void build_atlas_range(unsigned first,unsigned count){
    for(unsigned entry=first;entry<first+count;entry++){
        unsigned d=entry/TONES,t=entry%TONES;
        int r=7-(int)d*2/(DEPTHS-1);
        for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){
            int squared=x*x+y*y;if(squared>r*r)continue;
            float nx=(float)x/r,ny=(float)y/r,nz=__builtin_sqrtf(1.0f-(float)squared/(r*r));
            float light=-.36f*nx-.45f*ny+.81f*nz;
            int shade=(int)(light*(SHADES-1));if(shade<0)shade=0;if(shade>=SHADES-1)shade=SHADES-2;
            if(nx>-.5f&&nx<-.1f&&ny>-.6f&&ny<-.2f&&nz>.7f)shade=SHADES-1;
            atlas[(t*TILE+(unsigned)(y+7))*ATLAS_W+d*TILE+(unsigned)(x+7)]=(uint8_t)(1+(d*TONES+t)*SHADES+(unsigned)shade);
        }
    }
}
static void bucket_range(unsigned first,unsigned count){
    unsigned end=first+count;if(end>WAVE_BALL_COUNT)end=WAVE_BALL_COUNT;
    for(unsigned i=first;i<end;i++){
        unsigned d=clamp((unsigned)(view_positions[i].z*(DEPTHS-1)/(WAVE_BALL_DEPTH*128)),DEPTHS-1);
        next_ball[i]=bucket[d];bucket[d]=(int16_t)i;
    }
}
static void compose_begin(void){
    compose_time=now_ms();compose_count=compose_cursor=0;compose_pending=1;
    for(unsigned d=0;d<DEPTHS;d++)bucket[d]=-1;
    compose_stage=0;
    if(fast_backend){
        sample_positions(compose_time);bucket_range(0,WAVE_BALL_COUNT);compose_stage=2;
        compose_depth=DEPTHS-1;compose_index=bucket[compose_depth];
    }
}
static int compose_work(unsigned count){
    if(compose_stage<2){
        if(compose_stage==0)sample_range(compose_time,compose_cursor,count);
        else bucket_range(compose_cursor,count);
        compose_cursor+=count;
        if(compose_cursor>=WAVE_BALL_COUNT){
            compose_cursor=0;compose_stage++;
            if(compose_stage==2){compose_depth=DEPTHS-1;compose_index=bucket[compose_depth];}
        }
        return 0;
    }
    while(count--&&compose_count<WAVE_BALL_COUNT){
        while(compose_index<0&&compose_depth>0)compose_index=bucket[--compose_depth];
        if(compose_index<0)return -1;
        position_t point=view_positions[compose_index];
        float scale=WAVE_BALL_FOCAL/(WAVE_BALL_FOCAL+point.z/128.0f);
        unsigned tone=snapshot_tones[compose_index];
        item_t *it=&projected[compose_count++];
        it->x=233+(int)(point.x*(scale/128.0f))-7;it->y=233+(int)(point.y*(scale/128.0f))-7;
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
    capture_pending=capture_cursor=0;
    snapshot_valid=snapshot_index=physics_steps=display_frames=0;physics_started=last_ms-33u;
    if(fast_backend){
        wave_balls_reset(&world);build_colors(0,0);build_atlas_range(0,DEPTHS*TONES);
        if(upload_colors()!=0||gfx_tex_upload(0,TINYRT_GFX_INDEX8,ATLAS_W,ATLAS_H,atlas,sizeof(atlas),0)!=0)return -1;
        capture_snapshot(now_ms());compose_begin();if(compose_work(WAVE_BALL_COUNT)!=1)return -1;
    }
    if(input_events(248u)!=0)return -1;
    return clock_interval(fast_backend?33:1);
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
                if(init_cursor==DEPTHS*TONES){init_phase=4;init_cursor=0;}}
            else if(init_phase==4){
                if(upload_colors()!=0||gfx_tex_upload(0,TINYRT_GFX_INDEX8,ATLAS_W,ATLAS_H,atlas,sizeof(atlas),0)!=0)return -1;
                init_phase=5;
            }else {
                capture_range(init_cursor,16);init_cursor+=16;
                if(init_cursor>=WAVE_BALL_COUNT){
                    init_phase=0;ready=0;last_ms=now;finish_snapshot(now);compose_begin();
                }
            }
            return 0;
        }
        if(pressed&&!changed&&now-press_ms>=500u&&next_theme(now)!=0)return -1;
        if(color_pending){
            build_colors_range(table_index^1,requested_theme,color_cursor++,1);
            if(color_cursor==DEPTHS){table_index^=1;theme=requested_theme;toast_ms=now;color_pending=0;if(upload_colors()!=0)return -1;}
        }
        if(splash_pending&&!physics_pending){
            unsigned count=fast_backend?WAVE_BALL_COUNT:16;
            wave_balls_splash_range(&world,splash_cursor,count);splash_cursor+=count;
            if(splash_cursor>=WAVE_BALL_COUNT)splash_pending=0;
            if(!fast_backend)return 0;
        }
        if(capture_pending){
            capture_range(capture_cursor,16);capture_cursor+=16;
            if(capture_cursor>=WAVE_BALL_COUNT){capture_pending=0;finish_snapshot(capture_time);compose_begin();}
            return 0;
        }
        if(!fast_backend&&compose_pending){if(compose_work(32)<0)return -1;return 0;}
        last_ms=now;
        if(!physics_pending && now-physics_started>=33u){
            wave_balls_begin(&world,gx,gy,gz,1.0f/30.0f);physics_pending=1;physics_started=now;
        }
        if(physics_pending){
            uint32_t start=now_ms();unsigned chunks=fast_backend?512u:1u;
            while(chunks--){
                if(wave_balls_work(&world,fast_backend?32u:4u)){
                    physics_pending=0;physics_steps++;
                    if(!fast_backend){capture_pending=1;capture_cursor=0;capture_time=now;return 0;}
                    capture_snapshot(now_ms());break;
                }
                if(fast_backend&&now_ms()-start>=40u)break;
            }
        }
        if(snapshot_valid&&(fast_backend||!physics_pending)){
            compose_begin();
            if(fast_backend&&compose_work(WAVE_BALL_COUNT)!=1)return -1;
            if(!fast_backend)return 0;
        }
        if(!fast_backend)return clock_interval(1);
        /* Finish a useful simulation step before submitting the next frame. */
        cadence=(cadence+1)%3;return clock_interval(cadence==2?34:33);
    }
    return 0;
}
static int flush_batch(void){
    if(!batch.count)return 0;batch.size=(uint16_t)(28+batch.count*sizeof(item_t));
    int result=gfx_submit(&batch,batch.size);batch.count=0;return result;
}
#if WAVE_BALL_METRICS
static unsigned metric_number(char *out,uint32_t value){
    char digits[10];unsigned n=0;do{digits[n++]=(char)('0'+value%10);value/=10;}while(value);
    for(unsigned i=0;i<n;i++)out[i]=digits[n-i-1];return n;
}
#endif
int32_t tinyrt_render(void){
    if(init_phase||!ready)return draw_skip();ready=0;display_frames++;
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
#if WAVE_BALL_METRICS
    if(dl>133)dl=133;if(dt>24)dt=24;if(dr<333)dr=333;if(db<48)db=48;
#endif
    dl&=~15;dr=(dr+15)&~15;if(dr>466)dr=466;
    if(flush_batch()!=0||gfx_damage(dl,dt,dr-dl,db-dt)!=0||gfx_end()!=0)return -1;
    previous_left=left;previous_top=top;previous_right=right;previous_bottom=bottom;
    previous_valid=1;previous_toast=toast;
#if WAVE_BALL_METRICS
    char metric[32];unsigned length=metric_number(metric,physics_steps);metric[length++]=' ';
    length+=metric_number(metric+length,now_ms());
    if(draw_text_box(133,24,200,24,metric,length,0xffffff,18,1)!=0)return -1;
#endif
    if(toast){unsigned n=0;while(names[theme][n])n++;
        return draw_text_box(133,126,200,36,names[theme],n,0xffffff,24,1);}
    return 0;
}
int32_t tinyrt_stop(void){pressed=key_down=ready=0;return 0;}
