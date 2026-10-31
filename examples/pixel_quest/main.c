#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "assets_generated.h"

/* Pixel Quest：原创横版平台跳跃，四个主题八个关卡，终关对战熔核魔像。
 * 逻辑画布 233×233，gfx 整数放大 2 倍到 466 圆屏。33 ms 固定物理步长，
 * 单位 1/16 像素；关卡用 TILEMAP 提交可见窗口，主题只切换调色板。 */
#define D(call) do { if ((call) != 0) return -1; } while (0)
#define RGB565(r,g,b) ((uint32_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define VIEW 233
#define TILE 16
#define MAP_H 14
#define MAX_W 190
#define STAGE_COUNT 8
#define VIEW_Y (-8)          /* 地面顶（第 11 行）落在逻辑 y=168 */
#define WIN_COLS 16
#define WIN_ROWS (MAP_H+1)  /* 末行重复最底层地面，盖住画布底边 */
#define SUB 16
#define STEP_MS 33u
#define RUN_MAX 48          /* 3 px/步 */
#define JUMP_V (-168)
#define SPRING_V (-220)

typedef uint32_t __attribute__((may_alias)) word_t;

/* 关卡按“同字符横向行程”存放：解释器每回调只有 10 万条指令，不能逐格扫描。 */
typedef struct { uint8_t col,row,len; char ch; } run_t;
typedef struct { const run_t *runs; int run_count,width,theme; const char *name; } stage_t;
#include "levels_generated.h"

enum { T_EMPTY=0,T_GRASS=1,T_DIRT=2,T_BRICK=3,T_QBOX=4,T_USED=5,T_STONE=6,T_SPIKE=7,
       T_PLANK=8,T_BUSH=9,T_FLOWER=10,T_FLAG=11,T_FLAG_ON=12,T_PORTAL_TOP=13,T_PORTAL=14,
       T_QBOX_SHINE=15,T_GEM=16,T_LAVA=20,T_LAVA2=21,T_SPRING=22,T_SPRING_UP=23,
       T_SHIELD=29,T_SHIELD_BOX=47 /* 只存在于 map，绘制成宝石箱 */ };
enum { CELL_PLAYER=24,CELL_WASP=30,CELL_BEETLE=32,CELL_HOPPER=35,CELL_HOPPER_UP=48,CELL_SPIKER=49,CELL_GOLEM=52 };
enum { ST_TITLE,ST_INTRO,ST_PLAY,ST_DYING,ST_OVER,ST_CLEAR,ST_WIN };
enum { E_BEETLE,E_WASP,E_HOPPER,E_SPIKER };
enum { ES_ASLEEP,ES_ALIVE,ES_SQUASHED,ES_FALLING,ES_GONE };
#define MAX_ENEMIES 24
#define MAX_PLATS 8
#define MAX_POPS 6
#define PLAT_W 48
#define PLAT_RANGE 48
#define GOLEM_HP 3
#define KV_BEST 0
#define KV_UNLOCKED 1

typedef struct { int32_t x,y,vx,vy,home; uint8_t kind,state,frame; uint16_t timer; } enemy_t;
typedef struct { int16_t x,y; uint8_t vertical; } plat_t;
typedef struct { int32_t x,y; uint16_t timer; uint8_t cell; } pop_t;
typedef struct { int32_t x,y,vx,vy,left,right,floor; int hp,hurt,timer,awake; } boss_t;

static uint8_t map[MAP_H][MAX_W] __attribute__((aligned(4)));
static enemy_t enemies[MAX_ENEMIES];
static plat_t plats[MAX_PLATS];
static pop_t pops[MAX_POPS];
static boss_t boss;
static int enemy_count,plat_count,has_boss;
static int stage,unlocked,saved_unlocked,map_w,theme;
static int start_col,start_row,check_col,check_row,portal_col,portal_row;

/* 玩家碰撞盒 10×14，精灵 16×16 在盒左上 (-3,-2)。 */
#define PW 10
#define PH 14
static int32_t px,py,pvx,pvy;
static int on_ground,facing_left,coyote,jump_buffer,jump_held,anim,invuln,riding,spring_lift,shield;
static int state,lives,gems,score,best,saved_best,run_score_base,run_gems_base,run_shield_base;
static int checkpoint_on,state_ticks;
static uint32_t stage_ticks;
static int cam_x;
static int bump_col=-1,bump_row,bump_ticks,spring_col=-1,spring_row,spring_ticks;
static uint32_t last_ms,remainder,ticks;
/* 输入：倾斜 IMU 或按住屏幕左/右侧为方向，点中间跳；KEY1 留给宿主返回大厅。
 * 接上外设手柄后改用十字键/左摇杆和 A，不再读倾斜；游戏中拔掉手柄自动暂停。 */
static int touch_zone,tilt;   /* zone: 0 无 1 左 2 跳 3 右 */
static int pad_on,pad_lx,paused;
static uint32_t pad_buttons;
static int frame_ready;

#define SFX(name) do { if(audio_play(SOUND_##name##_OFFSET,SOUND_##name##_LENGTH,16000)<0) return -1; } while (0)

/* ---------- 地图 ---------- */
static int tile_at(int c,int r) {
    if(c<0||c>=map_w) return T_STONE;          /* 左右边界视为墙 */
    if(r<0||r>=MAP_H) return T_EMPTY;          /* 上方开阔、下方是坑 */
    return map[r][c];
}
static int solid(int t) {
    return t==T_GRASS||t==T_DIRT||t==T_BRICK||t==T_QBOX||t==T_SHIELD_BOX||t==T_USED||t==T_STONE;
}
static int floor_div(int v,int d) { return v>=0 ? v/d : -((-v+d-1)/d); }
static int triangle(int t,int range) { t%=range*2;return t<range?t:range*2-t; }
static int plat_x(const plat_t *p,uint32_t t) { return p->x+(p->vertical?0:triangle((int)t,PLAT_RANGE*2)-PLAT_RANGE); }
static int plat_y(const plat_t *p,uint32_t t) { return p->y+(p->vertical?triangle((int)t,PLAT_RANGE*2)-PLAT_RANGE:0); }

static void spawn_enemy(int kind,int c,int r) {
    if(enemy_count>=MAX_ENEMIES) return;
    enemy_t *e=&enemies[enemy_count++];
    e->x=c*TILE*SUB+2*SUB;e->y=(r*TILE+6)*SUB;e->vx=kind==E_SPIKER?-6:-10;e->vy=0;e->home=e->y;
    e->kind=(uint8_t)kind;e->state=ES_ASLEEP;e->frame=0;e->timer=(uint16_t)(c&31);
}
static void load_level(void) {
    const stage_t *s=&stages[stage];
    map_w=s->width;theme=s->theme;enemy_count=plat_count=has_boss=0;
    portal_col=-1;check_col=-1;
    word_t *words=(word_t *)(void *)map;
    for(uint32_t i=0;i<sizeof(map)/4;i++) words[i]=0;
    for(int i=0;i<s->run_count;i++) {
        const run_t *run=&s->runs[i];
        int c=run->col,r=run->row;uint8_t t=T_EMPTY;
        switch(run->ch) {
        case 'G': t=T_GRASS; break;
        case 'D': t=T_DIRT; break;
        case 'B': t=T_BRICK; break;
        case 'Q': t=T_QBOX; break;
        case 'H': t=T_SHIELD_BOX; break;
        case 'S': t=T_STONE; break;
        case '=': t=T_PLANK; break;
        case '^': t=T_SPIKE; break;
        case 'L': t=T_LAVA; break;
        case 'u': t=T_SPRING; break;
        case 'o': t=T_GEM; break;
        case 'b': t=T_BUSH; break;
        case 'f': t=T_FLOWER; break;
        case 'C': t=T_FLAG; check_col=c; check_row=r; break;
        case 'P': t=T_PORTAL; portal_col=c; portal_row=r; if(r>0) map[r-1][c]=T_PORTAL_TOP; break;
        case 'p': portal_col=c; portal_row=r; break;       /* 击败 Boss 后出现 */
        case '@': start_col=c; start_row=r; break;
        case 'e': spawn_enemy(E_BEETLE,c,r); break;
        case 'a': spawn_enemy(E_WASP,c,r); break;
        case 'h': spawn_enemy(E_HOPPER,c,r); break;
        case 'x': spawn_enemy(E_SPIKER,c,r); break;
        case 'M': case 'V':
            if(plat_count<MAX_PLATS) { plat_t *p=&plats[plat_count++];p->x=(int16_t)(c*TILE);p->y=(int16_t)(r*TILE);p->vertical=run->ch=='V'; }
            break;
        case 'K':
            has_boss=1;boss.x=c*TILE*SUB;boss.y=boss.floor=((r+1)*TILE-24)*SUB;boss.vx=-12;boss.vy=0;
            boss.left=(c-11)*TILE*SUB;boss.right=(c+11)*TILE*SUB;boss.hp=GOLEM_HP;boss.hurt=0;boss.timer=0;boss.awake=0;
            break;
        default: break;
        }
        if(t!=T_EMPTY) for(int n=0;n<run->len;n++) map[r][c+n]=t;
    }
    if(checkpoint_on&&check_col>=0) map[check_row][check_col]=T_FLAG_ON;
    for(int i=0;i<MAX_POPS;i++) pops[i].timer=0;
    bump_col=spring_col=-1;stage_ticks=0;
}
static void place_player(void) {
    int on_check=checkpoint_on&&check_col>=0;
    int col=on_check?check_col:start_col,row=on_check?check_row:start_row;
    px=(col*TILE+3)*SUB;py=(row*TILE+2)*SUB;pvx=pvy=0;
    on_ground=1;facing_left=0;coyote=jump_buffer=0;anim=0;riding=-1;spring_lift=0;
}
static void update_camera(void) {
    int target=px/SUB+PW/2-VIEW/2+8;
    if(target<0) target=0;
    if(target>map_w*TILE-VIEW) target=map_w*TILE-VIEW;
    cam_x=target;
}
static void begin_stage(void) {
    checkpoint_on=0;run_score_base=score;run_gems_base=gems;run_shield_base=shield;
    load_level();place_player();update_camera();
    state=ST_INTRO;state_ticks=0;touch_zone=0;invuln=0;
}
static void new_game(int first) {
    stage=first;lives=3;gems=0;score=0;shield=0;
    begin_stage();
}

/* ---------- 存档 ---------- */
static int save_progress(void) {
    if(score>best) best=score;
    if(best!=saved_best) { D(kv_set(KV_BEST,best));saved_best=best; }
    if(unlocked!=saved_unlocked) { D(kv_set(KV_UNLOCKED,unlocked));saved_unlocked=unlocked; }
    return 0;
}

/* ---------- 物理 ---------- */
static int pop_spawn(int x,int y,int cell) {
    for(int i=0;i<MAX_POPS;i++) if(!pops[i].timer) { pops[i].x=x;pops[i].y=y;pops[i].timer=14;pops[i].cell=(uint8_t)cell;return 0; }
    return 0;
}
static int add_gem(void) {
    gems++;score+=100;
    if(!(gems%50)&&lives<9) lives++;   /* 每 50 宝石奖励一条命 */
    SFX(GEM);return 0;
}
static int collect(int c,int r) { map[r][c]=T_EMPTY;return add_gem(); }
static int hit_block(int c,int r) {
    int t=tile_at(c,r);
    if(t==T_QBOX||t==T_SHIELD_BOX) {
        map[r][c]=T_USED;bump_col=c;bump_row=r;bump_ticks=6;
        if(t==T_QBOX) { pop_spawn(c*TILE,r*TILE-TILE,T_GEM);D(add_gem()); }
        else { shield=1;score+=100;pop_spawn(c*TILE,r*TILE-TILE,T_SHIELD);SFX(POWER); }
        /* 顶起箱子同时顶掉上方甲虫 */
        for(int i=0;i<enemy_count;i++) {
            enemy_t *e=&enemies[i];
            if(e->state==ES_ALIVE && e->x/SUB/TILE==c && (e->y/SUB+10)/TILE==r-1) { e->state=ES_FALLING;e->vy=-120;score+=200; }
        }
    } else if(t==T_BRICK||t==T_USED||t==T_STONE) {
        if(t==T_BRICK) { bump_col=c;bump_row=r;bump_ticks=6; }
        SFX(BUMP);
    }
    return 0;
}
static int player_die(void) {
    state=ST_DYING;state_ticks=0;pvx=0;pvy=-150;riding=-1;
    (void)haptic_pulse(70,180);SFX(HURT);return 0;
}
/* 敌人、尖刺这类可挡的伤害：护盾抵消一次并给无敌时间。 */
static int player_hurt(void) {
    if(invuln) return 0;
    if(shield) { shield=0;invuln=45;(void)haptic_pulse(50,60);SFX(BUMP);return 0; }
    return player_die();
}
static int move_player_x(void) {
    int32_t nx=px+pvx;
    int top=floor_div(py/SUB,TILE),bottom=floor_div((py/SUB)+PH-1,TILE);
    if(pvx>0) {
        int edge=(nx/SUB)+PW-1,c=floor_div(edge,TILE);
        for(int r=top;r<=bottom;r++) if(solid(tile_at(c,r))) { nx=(c*TILE-PW)*SUB;pvx=0;break; }
    } else if(pvx<0) {
        int edge=floor_div(nx,SUB),c=floor_div(edge,TILE);
        for(int r=top;r<=bottom;r++) if(solid(tile_at(c,r))) { nx=((c+1)*TILE)*SUB;pvx=0;break; }
    }
    px=nx;return 0;
}
static int land_on_plats(int32_t *ny) {
    int x=px/SUB,bottom=py/SUB+PH,next=*ny/SUB+PH;
    for(int i=0;i<plat_count;i++) {
        int top=plat_y(&plats[i],stage_ticks),left=plat_x(&plats[i],stage_ticks);
        if(x+PW>left&&x<left+PLAT_W&&bottom<=top+2&&next>=top) {
            *ny=(top-PH)*SUB;pvy=0;on_ground=1;riding=i;return 1;
        }
    }
    return 0;
}
static int move_player_y(void) {
    int32_t ny=py+pvy;
    int left=floor_div(px/SUB,TILE),right=floor_div((px/SUB)+PW-1,TILE);
    on_ground=0;riding=-1;
    if(pvy>=0) {
        int old_bottom=(py/SUB)+PH,edge=(ny/SUB)+PH-1,r=floor_div(edge,TILE);
        spring_lift=0;
        for(int c=left;c<=right;c++) {
            int t=tile_at(c,r);
            if(t==T_SPRING&&old_bottom<=r*TILE+8) {
                ny=(r*TILE+8-PH)*SUB;pvy=SPRING_V;spring_lift=1;coyote=0;
                spring_col=c;spring_row=r;spring_ticks=10;
                (void)haptic_pulse(30,30);SFX(SPRING);break;
            }
            if(solid(t)||(t==T_PLANK&&old_bottom<=r*TILE)) { ny=(r*TILE-PH)*SUB;pvy=0;on_ground=1;break; }
        }
        if(!on_ground&&pvy>=0) land_on_plats(&ny);
    } else {
        int edge=floor_div(ny,SUB),r=floor_div(edge,TILE);
        /* 头顶优先撞击居中的那块 */
        int center=floor_div((px/SUB)+PW/2,TILE),hit=-1;
        if(solid(tile_at(center,r))) hit=center;
        else for(int c=left;c<=right;c++) if(solid(tile_at(c,r))) { hit=c;break; }
        if(hit>=0) { ny=((r+1)*TILE)*SUB;pvy=0;spring_lift=0;D(hit_block(hit,r)); }
    }
    py=ny;return 0;
}
static int overlap(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh) {
    return ax<bx+bw && bx<ax+aw && ay<by+bh && by<ay+ah;
}
static int stage_finished(void) {
    state=ST_CLEAR;state_ticks=0;score+=1000+lives*500;
    if(stage+1>unlocked&&stage+1<STAGE_COUNT) unlocked=stage+1;
    (void)haptic_pulse(40,120);SFX(CLEAR);return save_progress();
}
static int touch_tiles(void) {
    int x=px/SUB,y=py/SUB;
    for(int r=floor_div(y,TILE);r<=floor_div(y+PH-1,TILE);r++)
        for(int c=floor_div(x,TILE);c<=floor_div(x+PW-1,TILE);c++) {
            int t=tile_at(c,r);
            if(t==T_GEM) D(collect(c,r));
            else if(t==T_LAVA&&overlap(x,y,PW,PH,c*TILE,r*TILE+4,TILE,12)) return player_die();
            else if(t==T_SPIKE&&overlap(x,y,PW,PH,c*TILE+2,r*TILE+6,12,10)) { D(player_hurt());if(state!=ST_PLAY) return 0; }
            else if(t==T_FLAG&&c==check_col&&r==check_row) {
                checkpoint_on=1;map[r][c]=T_FLAG_ON;run_score_base=score;run_gems_base=gems;run_shield_base=shield;SFX(CHECK);
            } else if(t==T_PORTAL||t==T_PORTAL_TOP) {
                if(has_boss&&boss.hp>0) continue;
                return stage_finished();
            }
        }
    return 0;
}
static int input_dir(void) {
    if(pad_on) {
        if(pad_buttons&TINYRT_PAD_LEFT) return -RUN_MAX;
        if(pad_buttons&TINYRT_PAD_RIGHT) return RUN_MAX;
        int a=pad_lx<0?-pad_lx:pad_lx;
        if(a<=24) return 0;   /* 摇杆回中死区；有手柄时不读倾斜 */
        int v=(a-24)*RUN_MAX/(127-24);
        return pad_lx<0?-v:v;
    }
    if(touch_zone==1) return -RUN_MAX;
    if(touch_zone==3) return RUN_MAX;
    int a=tilt<0?-tilt:tilt;
    if(a<130) return 0;
    int v=(a-130)*RUN_MAX/270;if(v>RUN_MAX) v=RUN_MAX;
    return tilt<0?-v:v;
}
static void walk_enemy(enemy_t *e) {
    /* 水平：撞墙转向 */
    int32_t nx=e->x+e->vx;int ex=nx/SUB,ey=e->y/SUB;
    int c=floor_div(e->vx>0?ex+11:ex,TILE),r=floor_div(ey+5,TILE);
    if(solid(tile_at(c,r))) e->vx=-e->vx; else e->x=nx;
    /* 垂直：落地 */
    int32_t ny=e->y+e->vy;int bottom=ny/SUB+9;int rr=floor_div(bottom,TILE);
    int c0=floor_div(e->x/SUB,TILE),c1=floor_div(e->x/SUB+11,TILE);
    int t0=tile_at(c0,rr),t1=tile_at(c1,rr);
    if(e->vy>=0&&(solid(t0)||solid(t1)||t0==T_PLANK||t1==T_PLANK)&&(e->y/SUB+10)<=rr*TILE) {
        ny=(rr*TILE-10)*SUB;e->vy=0;
        if(e->kind==E_HOPPER&&++e->frame>=24) { e->frame=0;e->vy=-150; }
    }
    e->y=ny;
}
static int contact_enemy(enemy_t *e) {
    int x=px/SUB,y=py/SUB;
    if(state!=ST_PLAY||!overlap(x,y,PW,PH,e->x/SUB,e->y/SUB,12,10)) return 0;
    if(e->kind!=E_SPIKER&&pvy>0&&y+PH-pvy/SUB<=e->y/SUB+6) {
        e->state=ES_SQUASHED;e->timer=0;score+=200;pvy=jump_held?JUMP_V:-120;
        (void)haptic_pulse(30,25);SFX(STOMP);return 0;
    }
    return player_hurt();
}
static int step_enemies(void) {
    for(int i=0;i<enemy_count;i++) {
        enemy_t *e=&enemies[i];
        if(e->state==ES_ASLEEP) { if(e->x/SUB < cam_x+VIEW+32) e->state=ES_ALIVE; else continue; }
        if(e->state==ES_SQUASHED) { if(++e->timer>20) e->state=ES_GONE; continue; }
        if(e->state==ES_GONE) continue;
        if(e->state==ES_FALLING) {
            e->vy+=24;if(e->vy>160) e->vy=160;
            e->x+=e->vx/2;e->y+=e->vy;if(e->y/SUB>MAP_H*TILE+32) e->state=ES_GONE;
            continue;
        }
        if(e->kind==E_WASP) {
            /* 三角波上下巡航，不受重力与地形影响 */
            e->timer++;e->y=e->home+(triangle(e->timer,24)-12)*SUB*2;e->x=e->x+e->vx;
            if(e->timer%96==0) e->vx=-e->vx;
            e->frame=(uint8_t)((e->timer/3)&1);
        } else {
            e->vy+=24;if(e->vy>160) e->vy=160;
            walk_enemy(e);
            if(e->y/SUB>MAP_H*TILE+32) { e->state=ES_GONE;continue; }
            if(e->kind!=E_HOPPER&&++e->timer>=8) { e->timer=0;e->frame^=1; }
        }
        D(contact_enemy(e));
        if(state!=ST_PLAY) return 0;
    }
    return 0;
}
static void reveal_portal(void) {
    map[portal_row][portal_col]=T_PORTAL;
    if(portal_row>0) map[portal_row-1][portal_col]=T_PORTAL_TOP;
}
static int step_boss(void) {
    if(!has_boss||boss.hp<=0) return 0;
    if(!boss.awake) { if(boss.x/SUB<cam_x+VIEW) boss.awake=1; else return 0; }
    if(boss.hurt) boss.hurt--;
    int speed=12+(GOLEM_HP-boss.hp)*5;
    boss.vx=boss.vx<0?-speed:speed;
    boss.x+=boss.vx;
    if(boss.x<boss.left||boss.x>boss.right-24*SUB) { boss.vx=-boss.vx;boss.x+=boss.vx*2; }
    boss.vy+=18;if(boss.vy>160) boss.vy=160;
    boss.y+=boss.vy;
    if(boss.y>=boss.floor) {
        boss.y=boss.floor;boss.vy=0;
        if(++boss.timer>=46) { boss.timer=0;boss.vy=-210; }
    }
    int x=px/SUB,y=py/SUB,bx=boss.x/SUB,by=boss.y/SUB;
    if(state!=ST_PLAY||boss.hurt||!overlap(x,y,PW,PH,bx,by,24,24)) return 0;
    if(pvy>0&&y+PH-pvy/SUB<=by+8) {
        boss.hp--;boss.hurt=45;pvy=JUMP_V;score+=1000;
        (void)haptic_pulse(80,120);SFX(BOSS);
        if(!boss.hp) reveal_portal();
        return 0;
    }
    return player_hurt();
}
static int step_play(void) {
    if(invuln) invuln--;
    stage_ticks++;
    if(riding>=0) {
        const plat_t *p=&plats[riding];
        px+=(plat_x(p,stage_ticks)-plat_x(p,stage_ticks-1))*SUB;
        py+=(plat_y(p,stage_ticks)-plat_y(p,stage_ticks-1))*SUB;
    }
    int target=input_dir();
    int accel=on_ground?4:3;
    if(target>pvx) { pvx+=accel;if(pvx>target) pvx=target; }
    else if(target<pvx) { pvx-=accel;if(pvx<target) pvx=target; }
    if(target<0) facing_left=1; else if(target>0) facing_left=0;
    if(on_ground) coyote=3; else if(coyote) coyote--;
    if(jump_buffer) {
        if(coyote) { pvy=JUMP_V;coyote=0;jump_buffer=0;on_ground=0;riding=-1;SFX(JUMP); }
        else jump_buffer--;
    }
    pvy+=((jump_held&&pvy<0)||spring_lift)?12:24;if(pvy>160) pvy=160;
    D(move_player_x());
    D(move_player_y());
    if(py/SUB>MAP_H*TILE+8) return player_die();
    D(touch_tiles());
    if(state!=ST_PLAY) return 0;
    D(step_enemies());
    if(state!=ST_PLAY) return 0;
    D(step_boss());
    if(on_ground && pvx) anim++; else if(on_ground) anim=0;
    update_camera();
    return 0;
}
static int tick(void) {
    ticks++;state_ticks++;
    for(int i=0;i<MAX_POPS;i++) if(pops[i].timer) { pops[i].timer--;pops[i].y-=2; }
    if(bump_ticks&&!--bump_ticks) bump_col=-1;
    if(spring_ticks&&!--spring_ticks) spring_col=-1;
    if(state==ST_INTRO) { if(state_ticks>=42) { state=ST_PLAY;state_ticks=0; } return 0; }
    if(state==ST_PLAY) return step_play();
    if(state==ST_DYING) {
        pvy+=16;if(pvy>160) pvy=160;py+=pvy;
        if(state_ticks>=40) {
            lives--;
            if(lives<=0) { state=ST_OVER;state_ticks=0;return save_progress(); }
            score=run_score_base;gems=run_gems_base;shield=run_shield_base;
            load_level();place_player();update_camera();invuln=45;state=ST_PLAY;state_ticks=0;
        }
        return 0;
    }
    if(state==ST_CLEAR&&state_ticks>=75) {
        if(stage+1<STAGE_COUNT) { stage++;begin_stage(); }
        else { state=ST_WIN;state_ticks=0;return save_progress(); }
    }
    if(state==ST_TITLE) { cam_x=(int)((ticks/2)%(uint32_t)(map_w*TILE-VIEW)); }
    return 0;
}

/* ---------- 绘制 ---------- */
static union { uint32_t align; uint8_t bytes[8192]; } gfx;
static uint32_t gfx_len;
static int gfx_overflow;
/* 记录都是 4 字节对齐、整 4 字节长，按字拷贝省指令预算。 */
static void emit(const void *record,uint32_t size) {
    if(gfx_len+size>sizeof(gfx.bytes)) { gfx_overflow=1;return; }
    const word_t *p=(const word_t *)record;
    word_t *out=(word_t *)(void *)(gfx.bytes+gfx_len);
    for(uint32_t i=0;i<size/4;i++) out[i]=p[i];
    gfx_len+=size;
}
static void sprite_pal(int x,int y,int w,int h,int u,int v,int sw,int sh,uint32_t flags,uint32_t pal) {
    if(x>=VIEW||y>=VIEW||x+w<=0||y+h<=0) return;
    tinyrt_gfx_sprite_t s={TINYRT_GFX_SPRITE,sizeof(s),x,y,w,h,0,(uint32_t)u,(uint32_t)v,(uint32_t)sw,(uint32_t)sh,flags|TINYRT_GFX_TRANSPARENT,pal,0};
    emit(&s,sizeof(s));
}
static void sprite(int x,int y,int w,int h,int u,int v,int sw,int sh,uint32_t flags) {
    sprite_pal(x,y,w,h,u,v,sw,sh,flags,0);
}
static void cell(int x,int y,int index,uint32_t flags) {
    sprite(x,y,TILE,TILE,(index%8)*TILE,(index/8)*TILE,TILE,TILE,flags);
}
static void rect(int x,int y,int w,int h,uint32_t color,uint32_t alpha) {
    tinyrt_gfx_rect_t r={TINYRT_GFX_RECT,sizeof(r),x,y,w,h,color,alpha};emit(&r,sizeof(r));
}
/* HUD 字形 8×8 位于图集 y=80：0-9、x、心、宝石、星 */
static struct { uint16_t op,size;uint32_t tex,sw,sh,flags,pal,count;int32_t inst[24][4]; } hud;
static void glyph(int x,int y,int g) {
    if(hud.count>=24) return;
    int32_t *p=hud.inst[hud.count++];p[0]=x;p[1]=y;p[2]=(g%16)*8;p[3]=80+(g/16)*8;
}
static int number(int x,int y,int value,int digits) {
    char d[6];for(int i=digits-1;i>=0;i--) { d[i]=(char)(value%10);value/=10; }
    for(int i=0;i<digits;i++) glyph(x+i*8,y,d[i]);
    return x+digits*8;
}
static const uint32_t theme_sky[4]={RGB565(110,198,255),RGB565(26,24,48),RGB565(150,184,250),RGB565(44,20,28)};
static void compose_backdrop(void) {
    struct { uint16_t op,size;uint32_t color; } clear={TINYRT_GFX_CLEAR,8,theme_sky[theme]};
    emit(&clear,sizeof(clear));
    /* 远景：云 1/8 视差、山 1/3 视差；洞穴没有云 */
    if(theme!=1) for(int i=-1;i<3;i++) {
        int x=i*120-(cam_x/8)%120;
        sprite_pal(x+20,34+(i&1)*14,32,16,64,64,32,16,0,(uint32_t)theme);
    }
    for(int i=-1;i<4;i++) {
        int x=i*96-(cam_x/3)%96;
        sprite_pal(x,120,96,48,96,64,32,16,0,(uint32_t)theme);
    }
}
static void compose_map(void) {
    int col0=cam_x/TILE,offset=cam_x%TILE;
    struct { uint16_t op,size;int32_t x,y;uint32_t cols,rows,tw,th,tex,sx,sy,flags,pal;uint8_t ids[WIN_COLS*WIN_ROWS]; } tm;
    tm.op=TINYRT_GFX_TILEMAP;tm.size=sizeof(tm);tm.x=-offset;tm.y=VIEW_Y;tm.cols=WIN_COLS;tm.rows=WIN_ROWS;
    tm.tw=TILE;tm.th=TILE;tm.tex=0;tm.sx=0;tm.sy=0;tm.flags=TINYRT_GFX_TRANSPARENT;tm.pal=(uint32_t)theme;
    int gem_frame=(int)(ticks/4)%4,shine=(ticks/9)%4==0,lava=(ticks/8)&1;
    for(int r=0;r<WIN_ROWS;r++) for(int c=0;c<WIN_COLS;c++) {
        int t=tile_at(col0+c,r<MAP_H?r:MAP_H-1);
        if(r==MAP_H&&t!=T_DIRT&&t!=T_STONE&&t!=T_LAVA) t=T_EMPTY;
        if(col0+c>=map_w) t=T_EMPTY;
        if(t==T_GEM) t=T_GEM+gem_frame;
        else if(t==T_QBOX||t==T_SHIELD_BOX) t=shine?T_QBOX_SHINE:T_QBOX;
        else if(t==T_LAVA&&lava) t=T_LAVA2;
        else if(t==T_SPRING&&col0+c==spring_col&&r==spring_row) t=T_SPRING_UP;
        if(col0+c==bump_col&&r==bump_row) t=T_EMPTY;
        tm.ids[r*WIN_COLS+c]=(uint8_t)t;
    }
    emit(&tm,sizeof(tm));
    if(bump_col>=0) {
        int lift=bump_ticks>3?6-bump_ticks:bump_ticks;
        int index=map[bump_row][bump_col];
        sprite_pal(bump_col*TILE-cam_x,bump_row*TILE+VIEW_Y-lift*2,TILE,TILE,(index%8)*TILE,(index/8)*TILE,TILE,TILE,0,(uint32_t)theme);
    }
    for(int i=0;i<plat_count;i++) {
        int x=plat_x(&plats[i],stage_ticks)-cam_x,y=plat_y(&plats[i],stage_ticks)+VIEW_Y;
        for(int n=0;n<3;n++) sprite_pal(x+n*TILE,y,TILE,TILE,(T_PLANK%8)*TILE,(T_PLANK/8)*TILE,TILE,TILE,0,(uint32_t)theme);
    }
}
static void compose_actors(void) {
    for(int i=0;i<MAX_POPS;i++) if(pops[i].timer)
        cell(pops[i].x-cam_x,pops[i].y+VIEW_Y,pops[i].cell==T_GEM?T_GEM+(int)(ticks/2)%4:pops[i].cell,0);
    for(int i=0;i<enemy_count;i++) {
        enemy_t *e=&enemies[i];
        if(e->state==ES_ASLEEP||e->state==ES_GONE) continue;
        int idx=CELL_BEETLE+e->frame;
        if(e->kind==E_WASP) idx=CELL_WASP+e->frame;
        else if(e->kind==E_HOPPER) idx=e->vy<0?CELL_HOPPER_UP:CELL_HOPPER;
        else if(e->kind==E_SPIKER) idx=CELL_SPIKER+e->frame;
        if(e->state==ES_SQUASHED) idx=34;
        uint32_t fl=(e->vx>0?TINYRT_GFX_FLIP_X:0)|(e->state==ES_FALLING?TINYRT_GFX_FLIP_Y:0);
        cell(e->x/SUB-2-cam_x,e->y/SUB-6+VIEW_Y,idx,fl);
    }
    if(has_boss&&boss.hp>0&&!(boss.hurt&&(ticks&2)))
        sprite(boss.x/SUB-4-cam_x,boss.y/SUB-8+VIEW_Y,32,32,(CELL_GOLEM%8)*TILE,(CELL_GOLEM/8)*TILE,32,32,boss.vx>0?TINYRT_GFX_FLIP_X:0);
    if(state!=ST_TITLE && state!=ST_INTRO && !(invuln&&(ticks&2))) {
        int idx=CELL_PLAYER;
        if(state==ST_DYING) idx=CELL_PLAYER+4;
        else if(!on_ground) idx=CELL_PLAYER+3;
        else if(pvx) idx=CELL_PLAYER+1+(anim/4)%2;
        if(state!=ST_CLEAR||state_ticks<6) {
            cell(px/SUB-3-cam_x,py/SUB-2+VIEW_Y,idx,facing_left?TINYRT_GFX_FLIP_X:0);
            if(shield) cell(px/SUB-3-cam_x,py/SUB-14+VIEW_Y,T_SHIELD,0);
        }
    }
}
static void compose_hud(void) {
    hud.op=TINYRT_GFX_SPRITE_BATCH;hud.tex=0;hud.sw=8;hud.sh=8;hud.flags=TINYRT_GFX_TRANSPARENT;hud.pal=0;hud.count=0;
    if(state!=ST_TITLE) {
        glyph(84,22,12);glyph(92,22,10);number(100,22,gems>99?99:gems,2);
        glyph(124,22,11);glyph(132,22,10);number(140,22,lives<0?0:lives,1);
        number(96,32,score>99999?99999:score,5);
    }
    if(state!=ST_PLAY&&state!=ST_DYING&&!paused) rect(34,78,165,80,RGB565(20,16,40),170);
    if(state==ST_OVER||state==ST_CLEAR||state==ST_WIN) {
        number(96,128,score>99999?99999:score,5);
        glyph(88,142,13);number(100,142,best>99999?99999:best,5);
    }
    if(hud.count) { hud.size=(uint16_t)(28+hud.count*16);emit(&hud,hud.size); }
}
static void compose(void) {
    gfx_len=0;gfx_overflow=0;
    compose_backdrop();compose_map();compose_actors();compose_hud();
    frame_ready=1;
}
static int text(int y,int h,const char *s,uint32_t color,int px_size) {
    uint32_t n=0;while(s[n]) n++;
    return draw_text_box(68,y,330,h,s,n,color,px_size,1);
}
/* 关卡编号 "STAGE 1-1"：每个主题两关。 */
static const char *stage_label(int index) {
    static char label[]="STAGE 0-0";
    label[6]=(char)('1'+index/2);label[8]=(char)('1'+index%2);
    return label;
}
int32_t tinyrt_render(void) {
    if(!frame_ready||gfx_overflow) return draw_skip();
    frame_ready=0;
    D(gfx_begin(2u<<TINYRT_GFX_SCALE_SHIFT));
    D(gfx_submit(gfx.bytes,gfx_len));
    D(gfx_end());
    if(paused) {
        D(text(190,48,"PAUSED",0xffd23f,36));
        D(text(240,30,"TAP OR PRESS A",0xffffff,18));
    } else if(state==ST_TITLE) {
        D(text(170,56,"PIXEL QUEST",0xffd23f,48));
        D(text(232,30,stage_label(stage),0x42f5c8,24));
        D(text(262,30,unlocked?(pad_on?"LEFT/RIGHT: PICK STAGE":"EDGES: PICK STAGE"):(pad_on?"D-PAD: MOVE  A: JUMP":"TILT: MOVE  TAP: JUMP"),0xffffff,18));
        if((ticks/15)&1) D(text(390,36,pad_on?"PRESS START":"TAP TO START",0xffffff,24));
    } else if(state==ST_INTRO) {
        D(text(176,48,stage_label(stage),0xffd23f,36));
        D(text(226,36,stages[stage].name,0xffffff,24));
    } else if(state==ST_OVER) {
        D(text(176,48,"GAME OVER",0xff6070,36));
        D(text(220,30,"SCORE",0xffffff,18));
        if(state_ticks>30) D(text(390,36,"TAP TO RETRY",0xffffff,24));
    } else if(state==ST_CLEAR) {
        D(text(176,48,"STAGE CLEAR!",0x42f5c8,36));
        D(text(220,30,"SCORE",0xffffff,18));
    } else if(state==ST_WIN) {
        D(text(176,48,"QUEST COMPLETE!",0xffd23f,36));
        D(text(220,30,"SCORE",0xffffff,18));
        if(state_ticks>30) D(text(390,36,"TAP TO PLAY AGAIN",0xffffff,24));
    }
    return 0;
}

/* ---------- 回调 ---------- */
static int press_jump(void) { jump_buffer=5;jump_held=1;return 0; }
static void pick_stage(int delta) {
    int next=stage+delta;
    if(next>=0&&next<=unlocked) { stage=next;load_level();cam_x=0; }
}
static int start_or_jump(int x) {
    if(paused) { paused=0;last_ms=now_ms();return 0; }
    if(state==ST_TITLE) {
        if(x<150) { pick_stage(-1);return 0; }
        if(x>316) { pick_stage(1);return 0; }
        new_game(stage);return 0;
    }
    if(state==ST_OVER||state==ST_WIN) {
        if(state_ticks<=30) return 0;
        D(save_progress());
        if(state==ST_OVER) new_game(stage);
        else { stage=0;state=ST_TITLE;state_ticks=0;checkpoint_on=0;load_level(); }
        return 0;
    }
    if(state!=ST_PLAY) return 0;
    touch_zone=x<150?1:x>316?3:2;
    if(touch_zone==2) return press_jump();
    return 0;
}
static void release_hold(void) { touch_zone=0;jump_held=0; }
static int on_gamepad(uint32_t buttons,int32_t sticks,int32_t arg) {
    if(!TINYRT_PAD_CONNECTED(arg)) {
        /* 游戏中拔掉手柄：暂停，之后恢复倾斜和触屏操作。 */
        if(pad_on&&state==ST_PLAY) paused=1;
        pad_on=0;pad_buttons=0;pad_lx=0;jump_held=0;
        return 0;
    }
    if(!pad_on) { pad_on=1;tilt=0;touch_zone=0; }
    uint32_t pressed=buttons&~pad_buttons,released=pad_buttons&~buttons;
    pad_buttons=buttons;pad_lx=TINYRT_PAD_LX(sticks);
    if(state==ST_TITLE&&!paused) {
        if(pressed&TINYRT_PAD_LEFT) pick_stage(-1);
        if(pressed&TINYRT_PAD_RIGHT) pick_stage(1);
    }
    if(pressed&TINYRT_PAD_START) {
        if(paused||state!=ST_PLAY) return start_or_jump(233);
    }
    if(pressed&TINYRT_PAD_A) {
        if(paused||state!=ST_PLAY) return start_or_jump(233);
        return press_jump();
    }
    if(released&TINYRT_PAD_A) jump_held=0;
    return 0;
}

int32_t tinyrt_init(int32_t width,int32_t height) {
    if(width!=466||height!=466) return -1;
    if((gfx_caps()&(TINYRT_GFX_CAP_TILEMAP|TINYRT_GFX_CAP_SPRITE|TINYRT_GFX_CAP_SPRITE_BATCH))!=
       (TINYRT_GFX_CAP_TILEMAP|TINYRT_GFX_CAP_SPRITE|TINYRT_GFX_CAP_SPRITE_BATCH)) return -1;
    uint16_t palette[PALETTE_COUNT];
    for(uint32_t i=0;i<THEME_COUNT;i++) {
        D(asset_read(PALETTE_OFFSET+i*PALETTE_COUNT*2,palette,PALETTE_COUNT*2)!=(int32_t)(PALETTE_COUNT*2));
        D(gfx_pal_upload(i,0,PALETTE_COUNT,palette));
    }
    D(gfx_tex_upload(0,TINYRT_GFX_INDEX8,128,128,(const void *)(uintptr_t)ATLAS_OFFSET,ATLAS_LENGTH,TINYRT_GFX_FROM_ASSET));
    best=kv_get(KV_BEST,0);if(best<0||best>99999) best=0;saved_best=best;
    unlocked=kv_get(KV_UNLOCKED,0);if(unlocked<0||unlocked>=STAGE_COUNT) unlocked=0;saved_unlocked=unlocked;
    state=ST_TITLE;ticks=0;touch_zone=tilt=0;jump_held=0;invuln=0;
    pad_on=pad_lx=paused=0;pad_buttons=0;
    stage=unlocked;checkpoint_on=0;load_level();place_player();cam_x=0;
    /* 需要支持手柄事件的固件：旧宿主拒绝 0x100 并终止实例，无法降级重试。 */
    D(input_events(TINYRT_INPUT_LIFECYCLE_MASK|TINYRT_IN_MOTION|TINYRT_IN_GAMEPAD));
    last_ms=now_ms();remainder=0;compose();
    return clock_interval((int32_t)STEP_MS);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    switch(kind) {
    case TINYRT_TOUCH_PRESS: return start_or_jump(x);
    case TINYRT_TOUCH_MOVE:
        if(state==ST_PLAY&&touch_zone&&touch_zone!=2) touch_zone=x<150?1:x>316?3:touch_zone;
        return 0;
    case TINYRT_TOUCH_RELEASE: release_hold();return 0;
    case TINYRT_TOUCH_CANCEL: touch_zone=0;jump_held=0;return 0;
    case TINYRT_EV_MOTION:
        /* 屏幕 X 轴即 ax；一阶低通平滑抖动 */
        if(!pad_on) tilt+=(x-tilt)/3;
        return 0;
    case TINYRT_EV_GAMEPAD: return on_gamepad((uint32_t)x,y,arg);
    case TINYRT_CLOCK_EVENT: {
        uint32_t now=now_ms(),elapsed=now-last_ms;last_ms=now;
        if(paused) { compose();return 0; }
        if(elapsed>132u) elapsed=132u;
        remainder+=elapsed;
        while(remainder>=STEP_MS) { remainder-=STEP_MS;D(tick()); }
        compose();return 0;
    }
    default: return 0;
    }
}
int32_t tinyrt_stop(void) { touch_zone=0;jump_held=0;pad_buttons=0;return save_progress(); }
