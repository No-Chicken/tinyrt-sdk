#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../examples/pixel_quest/main.c"

/* 原生回归：校验记录格式与资源边界，并用真实物理搜索证明八关都能无伤通关。 */
static uint32_t time_ms;
static int32_t stored[16];
static int audio_count,haptic_count,frames,texts_in_frame,frame_open;
static unsigned char resources[200000];
static uint32_t resource_length;
uint32_t now_ms(void){return time_ms;}
int32_t input_events(uint32_t mask){assert(mask==(TINYRT_INPUT_LIFECYCLE_MASK|TINYRT_IN_MOTION|TINYRT_IN_GAMEPAD));return 0;}
int32_t clock_interval(int32_t ms){assert(ms==33);return 0;}
int32_t kv_get(uint32_t key,int32_t fallback){assert(key<16);return stored[key]?stored[key]:fallback;}
int32_t kv_set(uint32_t key,int32_t value){assert(key<=KV_UNLOCKED);stored[key]=value;return 0;}
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
    assert(slot<THEME_COUNT&&first==0&&count==PALETTE_COUNT&&p[0]==0);return 0;
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
static void pad(uint32_t buttons,int lx){
    assert(tinyrt_event(TINYRT_EV_GAMEPAD,(int32_t)buttons,(int32_t)(uint8_t)(int8_t)lx,1<<16)==0);
}
static void tap(int x){assert(tinyrt_event(TINYRT_TOUCH_PRESS,x,233,0)==0);assert(tinyrt_event(TINYRT_TOUCH_RELEASE,x,233,0)==0);}

/* 求解器：每 4 步（约 130 ms）换一次输入，最优先扩展离终点最近的状态。动作只走
 * 手柄事件这条真实输入路径，不允许受伤，所以找到的解不靠护盾也不靠无敌时间。 */
typedef struct {
    int32_t px,py,pvx,pvy;
    int on_ground,facing_left,coyote,jump_buffer,jump_held,anim,invuln,riding,spring_lift,shield;
    int state,state_ticks,lives,gems,score,cam_x;
    uint32_t stage_ticks,ticks,pad_buttons;
    enemy_t enemies[MAX_ENEMIES];
    boss_t boss;
} snapshot_t;
typedef struct { int32_t node;int64_t key; } heap_t;
#define MAX_NODES 120000
#define HASH_BITS 22
static snapshot_t *nodes;
static heap_t *heap;
static uint64_t *visited;
static int heap_count,furthest;
static void save_state(snapshot_t *n){
#define S(f) n->f=f;
    S(px)S(py)S(pvx)S(pvy)S(on_ground)S(facing_left)S(coyote)S(jump_buffer)S(jump_held)S(anim)S(invuln)S(riding)S(spring_lift)S(shield)
    S(state)S(state_ticks)S(lives)S(gems)S(score)S(cam_x)S(stage_ticks)S(ticks)S(pad_buttons)
#undef S
    memcpy(n->enemies,enemies,sizeof(enemies));n->boss=boss;
}
static void load_state(const snapshot_t *n){
#define L(f) f=n->f;
    L(px)L(py)L(pvx)L(pvy)L(on_ground)L(facing_left)L(coyote)L(jump_buffer)L(jump_held)L(anim)L(invuln)L(riding)L(spring_lift)L(shield)
    L(state)L(state_ticks)L(lives)L(gems)L(score)L(cam_x)L(stage_ticks)L(ticks)L(pad_buttons)
#undef L
    memcpy(enemies,n->enemies,sizeof(enemies));boss=n->boss;
}
static void heap_push(int node,int64_t key){
    int i=heap_count++;
    while(i&&heap[(i-1)/2].key<key){heap[i]=heap[(i-1)/2];i=(i-1)/2;}
    heap[i].node=node;heap[i].key=key;
}
static int heap_pop(void){
    int top=heap[0].node;heap_t last=heap[--heap_count];int i=0;
    for(;;){int c=2*i+1;if(c>=heap_count)break;
        if(c+1<heap_count&&heap[c+1].key>heap[c].key)c++;
        if(last.key>=heap[c].key)break;
        heap[i]=heap[c];i=c;}
    heap[i]=last;return top;
}
static int seen(void){
    int moving=plat_count||has_boss||enemy_count;
    uint64_t k=(uint64_t)(px>>5)*1000003u+(uint64_t)((py>>5)+64)*10007u+(uint64_t)((pvx>>4)+16)*101u+(uint64_t)((pvy>>5)+16);
    k=k*31u+(uint64_t)on_ground*2u+(uint64_t)jump_held+(uint64_t)(riding+1)*5u;
    if(moving)k=k*131u+(stage_ticks>>4);
    if(has_boss)k=k*7u+(uint64_t)boss.hp;
    k=k*0x9e3779b97f4a7c15ull+1u;
    uint32_t slot=(uint32_t)(k>>(64-HASH_BITS));
    while(visited[slot]){if(visited[slot]==k)return 1;slot=(slot+1)&((1u<<HASH_BITS)-1);}
    visited[slot]=k;return 0;
}
static int64_t rank_state(int depth){
    int64_t progress=px/SUB;
    if(has_boss&&boss.hp>0&&px>=boss.left) {
        /* 进入竞技场后目标是贴近 Boss 头顶，而不是继续向右。 */
        int gap=(px-boss.x-7*SUB)/SUB;if(gap<0)gap=-gap;
        progress=boss.left/SUB+2000-gap+(GOLEM_HP-boss.hp)*4000;
    } else if(has_boss&&!boss.hp)progress+=20000;
    return progress*4-depth;
}
static const uint32_t solver_actions[6]={TINYRT_PAD_RIGHT|TINYRT_PAD_A,TINYRT_PAD_RIGHT,TINYRT_PAD_A,0,
                                         TINYRT_PAD_LEFT|TINYRT_PAD_A,TINYRT_PAD_LEFT};
static int solve_stage(void){
    memset(visited,0,sizeof(uint64_t)<<HASH_BITS);
    heap_count=0;furthest=0;int count=1,depth_limit=0;
    save_state(&nodes[0]);heap_push(0,0);
    int shield_before=shield;
    while(heap_count){
        int at=heap_pop();
        for(unsigned a=0;a<6;a++){
            load_state(&nodes[at]);
            if(state!=ST_PLAY)continue;
            uint32_t buttons=solver_actions[a];pad(buttons,0);
            int hurt=0;
            for(int t=0;t<4&&state==ST_PLAY;t++){
                assert(tick()==0);
                if(invuln||state==ST_DYING||shield<shield_before)hurt=1;
            }
            if(state==ST_CLEAR)return count;
            if(hurt||state!=ST_PLAY||seen())continue;
            if(count>=MAX_NODES)return 0;
            if(px/SUB/TILE>furthest)furthest=px/SUB/TILE;
            save_state(&nodes[count]);
            int depth=(int)(stage_ticks/4);if(depth>depth_limit)depth_limit=depth;
            heap_push(count++,rank_state(depth));
        }
    }
    return 0;
}
int main(int argc,char **argv){
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);
    resource_length=(uint32_t)fread(resources,1,sizeof(resources),file);fclose(file);
    assert(resource_length==SOUND_BOSS_OFFSET+SOUND_BOSS_LENGTH);
    for(int n=0;n<STAGE_COUNT;n++){
        assert(stages[n].width<=MAX_W&&stages[n].width*TILE>VIEW&&stages[n].theme<(int)THEME_COUNT);
        for(int i=0;i<stages[n].run_count;i++){const run_t *run=&stages[n].runs[i];
            assert(run->row<MAP_H&&run->len>0&&run->col+run->len<=stages[n].width);}
    }
    nodes=malloc(sizeof(snapshot_t)*MAX_NODES);heap=malloc(sizeof(heap_t)*MAX_NODES);
    visited=calloc((size_t)1<<HASH_BITS,sizeof(uint64_t));assert(nodes&&heap&&visited);

    /* 标题 → 开始：八关全部无伤可解，进度和最高分写入 KV。 */
    time_ms=5000;assert(tinyrt_init(466,466)==0);assert(state==ST_TITLE&&stage==0);
    for(int i=0;i<20;i++)step();
    assert(frames>0);tap(233);assert(state==ST_INTRO&&lives==3&&score==0);
    pad(0,0);assert(tinyrt_event(TINYRT_EV_GAMEPAD,0,0,1<<16)==0);
    for(int n=0;n<STAGE_COUNT;n++){
        assert(stage==n&&state==ST_INTRO);
        for(int i=0;i<4;i++)step();                /* 片头帧也要提交合法记录 */
        while(state==ST_INTRO)step();
        int explored=solve_stage();
        printf("stage %d %-14s states=%d ticks=%u furthest_col=%d\n",n,stages[n].name,explored,stage_ticks,furthest);fflush(stdout);
        assert(explored>0&&state==ST_CLEAR);
        pad(0,0);jump_held=0;jump_buffer=0;
        if(n<STAGE_COUNT-1)assert(stored[KV_UNLOCKED]==n+1&&unlocked==n+1);
        assert(stored[KV_BEST]==score);
        for(int i=0;i<80&&state==ST_CLEAR;i++)step();
    }
    assert(state==ST_WIN);
    assert(tinyrt_event(TINYRT_EV_GAMEPAD,0,0,0)==0);paused=0;
    for(int i=0;i<10;i++)step();
    tap(233);assert(state==ST_WIN);               /* 结算 1 秒内不响应误触 */
    for(int i=0;i<25;i++)step();
    tap(233);assert(state==ST_TITLE&&stage==0);

    /* 选关：只能选已解锁的关，下次启动恢复进度。 */
    tap(420);assert(stage==1);tap(40);assert(stage==0);
    assert(tinyrt_init(466,466)==0);assert(stage==STAGE_COUNT-1&&unlocked==STAGE_COUNT-1);
    for(int i=0;i<STAGE_COUNT;i++)tap(40);
    assert(stage==0);
    tap(233);assert(state==ST_INTRO&&score==0&&lives==3&&gems==0);
    while(state==ST_INTRO)step();
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
    enemy_t *e=&enemies[0];e->state=ES_ALIVE;e->x=px+2*SUB;e->y=py-30*SUB;e->vx=0;
    int s0=score;pvy=100;py=e->y-PH*SUB+2*SUB;
    step();assert(e->state==ES_SQUASHED&&score==s0+200&&pvy<0);

    /* 手柄：十字键与左摇杆移动、倾斜失效；A 按住更高、轻点更低。 */
    for(int i=0;i<30;i++)step();
    pad(0,0);assert(pad_on&&tilt==0);
    assert(tinyrt_event(TINYRT_EV_MOTION,-900,0,1000)==0);assert(tilt==0&&input_dir()==0);
    int gx=px;pad(TINYRT_PAD_RIGHT,0);for(int i=0;i<10;i++)step();
    assert(px>gx&&!facing_left);
    pad(0,-127);assert(input_dir()==-RUN_MAX);
    pad(0,20);assert(input_dir()==0);
    pad(0,0);for(int i=0;i<20;i++)step();
    assert(on_ground&&pvx==0);int pad_ground=py;
    pad(TINYRT_PAD_A,0);pad(0,0);int pad_tap=py;
    for(int i=0;i<30;i++){step();if(py<pad_tap)pad_tap=py;}
    pad(TINYRT_PAD_A,0);int pad_hold=py;
    for(int i=0;i<30;i++){step();if(py<pad_hold)pad_hold=py;}
    pad(0,0);
    assert(pad_ground-pad_hold>pad_ground-pad_tap+16*SUB);

    /* 游戏中拔掉手柄：全部松开并暂停，点屏幕继续，之后恢复倾斜。 */
    for(int i=0;i<20;i++)step();
    pad(TINYRT_PAD_A|TINYRT_PAD_LEFT,0);assert(jump_held);
    assert(tinyrt_event(TINYRT_EV_GAMEPAD,0,0,0)==0);
    assert(!pad_on&&paused&&!jump_held&&pad_buttons==0);
    int paused_x=px,paused_y=py;for(int i=0;i<10;i++)step();
    assert(px==paused_x&&py==paused_y);
    tap(233);assert(!paused&&state==ST_PLAY);
    assert(tinyrt_event(TINYRT_EV_MOTION,-900,0,1000)==0);assert(tilt!=0);
    for(int i=0;i<10;i++)assert(tinyrt_event(TINYRT_EV_MOTION,0,0,1000)==0);
    for(int i=0;i<30;i++)step();

    /* 掉坑：失去一条命并在起点或检查点复活；三条命用完结束并存档。 */
    for(int i=0;i<60;i++)step();
    int l0=lives;py=(MAP_H*TILE+20)*SUB;step();assert(state==ST_DYING);
    for(int i=0;i<45;i++)step();
    assert(state==ST_PLAY&&lives==l0-1&&invuln>0);
    while(lives>0){py=(MAP_H*TILE+20)*SUB;invuln=0;step();for(int i=0;i<45;i++)step();}
    assert(state==ST_OVER);
    assert(tinyrt_stop()==0&&stored[KV_BEST]==best);

    /* 机关：护盾挡一次，岩浆不能挡；弹簧弹起；平台带人；每 50 宝石加命。 */
    for(int i=0;i<35;i++)step();
    tap(233);assert(state==ST_INTRO&&lives==3);
    while(state==ST_INTRO)step();
    shield=1;invuln=0;assert(player_hurt()==0&&state==ST_PLAY&&!shield&&invuln==45);
    assert(player_hurt()==0&&state==ST_PLAY);     /* 无敌期间不再受伤 */
    invuln=0;gems=49;assert(add_gem()==0&&gems==50&&lives==4);
    stage=6;checkpoint_on=0;load_level();place_player();state=ST_PLAY;shield=1;
    int lava_c=-1;for(int c=0;c<map_w&&lava_c<0;c++)if(tile_at(c,MAP_H-2)==T_LAVA)lava_c=c;
    assert(lava_c>=0);px=(lava_c*TILE+3)*SUB;py=((MAP_H-2)*TILE)*SUB;pvx=pvy=0;
    step();assert(state==ST_DYING&&shield==1);
    for(int i=0;i<45;i++)step();
    assert(state==ST_PLAY);
    stage=1;load_level();place_player();state=ST_PLAY;
    int sc=-1,sr=-1;for(int c=0;c<map_w&&sc<0;c++)for(int r=0;r<MAP_H;r++)if(tile_at(c,r)==T_SPRING){sc=c;sr=r;}
    assert(sc>=0);px=(sc*TILE+3)*SUB;py=(sr*TILE-PH-2)*SUB;pvy=40;pvx=0;
    step();assert(pvy<JUMP_V&&spring_lift);
    stage=2;load_level();place_player();state=ST_PLAY;assert(plat_count>0);
    plat_t *mover=&plats[0];assert(!mover->vertical);
    stage_ticks=10;px=(plat_x(mover,stage_ticks)+10)*SUB;py=(plat_y(mover,stage_ticks)-PH-1)*SUB;pvx=pvy=0;
    step();step();assert(riding==0&&on_ground);
    int carried=px;for(int i=0;i<5;i++)step();
    assert(px-carried==(plat_x(mover,stage_ticks)-plat_x(mover,stage_ticks-5))*SUB);

    /* Boss：踩三次打开传送门；在此之前门不生效，正面碰撞受伤。 */
    stage=7;load_level();place_player();state=ST_PLAY;shield=0;assert(has_boss&&boss.hp==GOLEM_HP);
    assert(tile_at(portal_col,portal_row)==T_EMPTY);
    boss.awake=1;
    for(int hit=GOLEM_HP-1;hit>=0;hit--){
        boss.hurt=0;boss.vy=0;boss.y=boss.floor;
        px=boss.x+7*SUB;py=boss.y-(PH+3)*SUB;pvy=120;pvx=0;
        step();assert(boss.hp==hit&&pvy<0&&state==ST_PLAY);
    }
    assert(tile_at(portal_col,portal_row)==T_PORTAL);
    stage_finished();assert(state==ST_CLEAR);
    assert(audio_count>20&&haptic_count>=3);
    printf("pixel quest: frames=%d audio=%d haptic=%d best=%d\n",frames,audio_count,haptic_count,best);
    free(nodes);free(heap);free(visited);
    return 0;
}
