#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../examples/pixel_quest/main.c"

/* 原生回归：校验记录格式与资源边界，并用确定性自动驾驶走完整关。 */
static uint32_t time_ms;
static int32_t stored[16];
static int audio_count,haptic_count,frames,texts_in_frame,frame_open;
static unsigned char resources[200000];
static uint32_t resource_length;
uint32_t now_ms(void){return time_ms;}
int32_t input_events(uint32_t mask){assert(mask==(TINYRT_INPUT_LIFECYCLE_MASK|TINYRT_IN_MOTION));return 0;}
int32_t clock_interval(int32_t ms){assert(ms==33);return 0;}
int32_t kv_get(uint32_t key,int32_t fallback){assert(key<16);return stored[key]?stored[key]:fallback;}
int32_t kv_set(uint32_t key,int32_t value){assert(key==0);stored[key]=value;return 0;}
int32_t asset_read(uint32_t offset,void *out,uint32_t length){
    assert(length<=4096&&offset<=resource_length&&length<=resource_length-offset);
    memcpy(out,resources+offset,length);return (int32_t)length;
}
int32_t audio_play(uint32_t offset,uint32_t length,uint32_t rate){
    assert(rate==16000&&length>=2&&length<=32000&&!(length&1)&&offset+length<=resource_length);
    audio_count++;return 0;
}
int32_t haptic_pulse(uint32_t strength,uint32_t ms){assert(strength>=1&&strength<=100&&ms>=1&&ms<=2000);haptic_count++;return 0;}
int32_t draw_skip(void){assert(!frame_open);return 0;}
uint32_t gfx_caps(void){return TINYRT_GFX_CAPS;}
int32_t gfx_pal_upload(uint32_t slot,uint32_t first,uint32_t count,const uint16_t *p){
    assert(slot==0&&first==0&&count==PALETTE_COUNT&&p[0]==0);return 0;
}
int32_t gfx_tex_upload(uint32_t slot,uint32_t format,uint32_t w,uint32_t h,const void *p,uint32_t n,uint32_t flags){
    assert(slot==0&&format==TINYRT_GFX_INDEX8&&w==128&&h==128&&n==w*h&&flags==TINYRT_GFX_FROM_ASSET);
    assert((uintptr_t)p+n<=resource_length);
    for(uint32_t i=0;i<n;i++)assert(resources[(uintptr_t)p+i]<PALETTE_COUNT);
    return 0;
}
int32_t gfx_begin(uint32_t flags){assert(!frame_open&&flags==(2u<<TINYRT_GFX_SCALE_SHIFT));frame_open=1;texts_in_frame=0;return 0;}
int32_t gfx_end(void){assert(frame_open);frame_open=0;frames++;return 0;}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int32_t gfx_submit(const void *data,uint32_t length){
    assert(frame_open&&length>0&&length<=TINYRT_GFX_MAX_SUBMIT);
    const uint8_t *p=data;uint32_t at=0,records=0;
    while(at<length){
        uint16_t op=(uint16_t)(p[at]|p[at+1]<<8),size=(uint16_t)(p[at+2]|p[at+3]<<8);
        const uint8_t *f=p+at+4;
        assert(size>=8&&!(size&3)&&size<=length-at);
        if(op==TINYRT_GFX_CLEAR)assert(size==8&&records==0);
        else if(op==TINYRT_GFX_RECT)assert(size==28&&get32(f+20)<=255);
        else if(op==TINYRT_GFX_SPRITE){
            assert(size==52&&get32(f+16)==0);
            uint32_t u=get32(f+20),v=get32(f+24),sw=get32(f+28),sh=get32(f+32);
            assert(sw&&sh&&u+sw<=128&&v+sh<=128&&get32(f+8)&&get32(f+12));
        }else if(op==TINYRT_GFX_SPRITE_BATCH){
            uint32_t count=get32(f+20);
            assert(count>=1&&count<=1024&&size==28+count*16&&get32(f+4)==8&&get32(f+8)==8);
            for(uint32_t i=0;i<count;i++){uint32_t u=get32(f+24+i*16+8),v=get32(f+24+i*16+12);assert(u+8<=128&&v+8<=128);}
        }else if(op==TINYRT_GFX_TILEMAP){
            uint32_t cols=get32(f+8),rows=get32(f+12);
            assert(size==48+((cols*rows+3)&~3u)&&get32(f+16)==16&&get32(f+20)==16&&get32(f+24)==0);
            for(uint32_t i=0;i<cols*rows;i++)assert(f[44+i]<64);
        }else assert(!"unexpected record");
        at+=size;records++;
    }
    return 0;
}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *s,uint32_t n,uint32_t rgb,int32_t px,int32_t align){
    (void)rgb;assert(!frame_open&&frames>0);
    assert(n==strlen(s)&&n>0&&n<=63&&x>=0&&y>=0&&w>0&&h>0&&x+w<=466&&y+h<=466);
    for(uint32_t i=0;i<n;i++)assert(s[i]>=32&&s[i]<127);  /* 设备字体子集只保证 ASCII */
    assert((px==18||px==24||px==36||px==48)&&align==1&&++texts_in_frame<=4);return 0;
}
static void step(void){time_ms+=33;assert(tinyrt_event(TINYRT_CLOCK_EVENT,0,0,0)==0);assert(tinyrt_render()==0);}
static void tap(int x){assert(tinyrt_event(TINYRT_TOUCH_PRESS,x,233,0)==0);assert(tinyrt_event(TINYRT_TOUCH_RELEASE,x,233,0)==0);}

/* 自动驾驶：前方有墙、坑、尖刺或甲虫时起跳；头顶有障碍且甲虫逼近时后退。 */
static int low_ceiling(void){
    int x=px/SUB,y=py/SUB;
    for(int c=floor_div(x,TILE);c<=floor_div(x+PW+12,TILE);c++)
        for(int r=floor_div(y-48,TILE);r<floor_div(y,TILE);r++)if(solid(tile_at(c,r)))return 1;
    return 0;
}
static int enemy_ahead(int range){
    int x=px/SUB,y=py/SUB;
    for(int i=0;i<enemy_count;i++){enemy_t *e=&enemies[i];
        if(e->state!=1)continue;
        int ex=e->x/SUB;
        if(ex>x-8&&ex-x<range&&e->y/SUB+10>y&&e->y/SUB<y+PH+8)return 1;}
    return 0;
}
static int needs_jump(void){
    int x=px/SUB,y=py/SUB,feet=floor_div(y+PH,TILE),body=floor_div(y+PH-1,TILE);
    int c=floor_div(x+PW+6,TILE);
    if(solid(tile_at(c,body))||solid(tile_at(c,body-1)))return 1;
    if(!solid(tile_at(c,feet))&&tile_at(c,feet)!=T_PLANK&&!solid(tile_at(c+1,feet))&&tile_at(c+1,feet)!=T_PLANK)return 1;
    if(tile_at(c,body)==T_SPIKE)return 1;
    return enemy_ahead(40)&&!low_ceiling();
}
static int autopilot(int max_ticks){
    int held=0,deaths=0;
    for(int t=0;t<max_ticks&&state!=ST_CLEAR&&state!=ST_OVER;t++){
        int tilt_mg=state==ST_PLAY&&enemy_ahead(64)&&low_ceiling()?-450:450;
        assert(tinyrt_event(TINYRT_EV_MOTION,tilt_mg,0,1000)==0);
        if(state==ST_PLAY){
            if(needs_jump()&&on_ground&&!held){assert(tinyrt_event(TINYRT_TOUCH_PRESS,233,200,0)==0);held=12;}
            if(held&&!--held)assert(tinyrt_event(TINYRT_TOUCH_RELEASE,233,200,0)==0);
        }
        int before=state;step();
        if(before==ST_PLAY&&state==ST_DYING)deaths++;
    }
    return deaths;
}
int main(int argc,char **argv){
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    resource_length=(uint32_t)fread(resources,1,sizeof(resources),file);fclose(file);
    assert(resource_length==SOUND_CHECK_OFFSET+SOUND_CHECK_LENGTH);
    for(int r=0;r<MAP_H;r++)assert(strlen(level_rows[r])==MAP_W);

    /* 标题 → 开始；倾斜通关且不掉命，最高分写入 KV。 */
    time_ms=5000;assert(tinyrt_init(466,466)==0);assert(state==ST_TITLE);
    for(int i=0;i<20;i++)step();
    assert(frames>0);tap(233);assert(state==ST_PLAY&&lives==3&&score==0);
    int deaths=autopilot(30*180);
    assert(state==ST_CLEAR&&deaths==0&&gems>=10);
    assert(score==gems*100+1000+lives*500&&stored[0]==score&&best==score);
    for(int i=0;i<10;i++)step();
    tap(233);assert(state==ST_CLEAR);             /* 结算 1 秒内不响应误触 */
    for(int i=0;i<25;i++)step();
    tap(233);assert(state==ST_PLAY&&score==0&&lives==3&&gems==0);
    for(int i=0;i<10;i++)assert(tinyrt_event(TINYRT_EV_MOTION,0,0,1000)==0);
    assert(input_dir()==0);

    /* 按住左右侧移动；取消清除按住状态。 */
    int x0=px;assert(tinyrt_event(TINYRT_TOUCH_PRESS,420,233,0)==0);
    for(int i=0;i<15;i++)step();
    assert(px>x0&&touch_zone==3&&!facing_left);
    assert(tinyrt_event(TINYRT_TOUCH_CANCEL,0,0,0)==0);assert(touch_zone==0&&!jump_held);
    for(int i=0;i<15;i++)step();
    int x1=px;for(int i=0;i<5;i++)step();assert(px==x1&&pvx==0);
    assert(tinyrt_event(TINYRT_TOUCH_PRESS,40,233,0)==0);
    for(int i=0;i<10;i++)step();
    assert(px<x1&&facing_left);assert(tinyrt_event(TINYRT_TOUCH_RELEASE,40,233,0)==0);
    for(int i=0;i<15;i++)step();

    /* 跳：按住更高、轻点更低。 */
    tap(233);int top_tap=py;for(int i=0;i<30;i++){step();if(py<top_tap)top_tap=py;}
    assert(on_ground);int ground=py;
    assert(tinyrt_event(TINYRT_TOUCH_PRESS,233,233,0)==0);int top_hold=py;
    for(int i=0;i<30;i++){step();if(py<top_hold)top_hold=py;}
    assert(tinyrt_event(TINYRT_TOUCH_RELEASE,233,233,0)==0);
    assert(ground-top_hold>ground-top_tap+16*SUB&&ground-top_hold<=80*SUB);

    /* 踩甲虫：从上方落下得分并反弹。 */
    for(int i=0;i<30;i++)step();
    enemy_t *e=&enemies[0];e->state=1;e->x=px+2*SUB;e->y=py-30*SUB;e->vx=0;
    int s0=score;pvy=100;py=e->y-PH*SUB+2*SUB;
    step();assert(e->state==2&&score==s0+200&&pvy<0);

    /* 掉坑：失去一条命并在起点或检查点复活；三条命用完结束并存档。 */
    for(int i=0;i<60;i++)step();
    int l0=lives;py=(MAP_H*TILE+20)*SUB;step();assert(state==ST_DYING);
    for(int i=0;i<45;i++)step();
    assert(state==ST_PLAY&&lives==l0-1&&invuln>0);
    while(lives>0){py=(MAP_H*TILE+20)*SUB;invuln=0;step();for(int i=0;i<45;i++)step();}
    assert(state==ST_OVER);
    assert(tinyrt_stop()==0&&stored[0]==best);
    assert(audio_count>20&&haptic_count>=3);
    printf("pixel quest: frames=%d audio=%d haptic=%d best=%d\n",frames,audio_count,haptic_count,best);
    return 0;
}
