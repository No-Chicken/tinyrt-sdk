#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "assets_generated.h"

/* Pixel Quest：原创横版平台跳跃。逻辑画布 233×233，gfx 整数放大 2 倍到 466 圆屏。
 * 33 ms 固定物理步长，单位 1/16 像素；关卡用 TILEMAP 提交可见窗口。 */
#define D(call) do { if ((call) != 0) return -1; } while (0)
#define RGB565(r,g,b) ((uint32_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define SKY RGB565(110,198,255)
#define VIEW 233
#define TILE 16
#define MAP_H 14
#define VIEW_Y (-8)          /* 地面顶（第 11 行）落在逻辑 y=168 */
#define WIN_COLS 16
#define WIN_ROWS (MAP_H+1)  /* 末行重复最底层地面，盖住画布底边 */
#define SUB 16
#define STEP_MS 33u
#define RUN_MAX 48          /* 3 px/步 */

/* 关卡字符：# 地面  B 砖  Q 宝石箱  S 石块  = 单向木台  ^ 尖刺  o 宝石
 * e 甲虫  b 灌木  f 小花  C 检查点  P 传送门  @ 起点 */
static const char *const level_rows[MAP_H] = {
    "                                                                                                                                                                                ",
    "                                                                                                                                                                                ",
    "                                                                                                                                                                                ",
    "                                                                                                  oooooo                                                                        ",
    "                 o                                                                                ======                 o                                                      ",
    "                                                                                                                                                                                ",
    "                                     o               o   o                       o       o                               S         S                    o                       ",
    "                BQBQB               o          oo    BBQBB                 o    ===            QBQ                      SS        SS    BQQB                                    ",
    "        o o o                        S  S                                 ===                                          SSS       SSS            o o o                           ",
    "                                    SS  SS    ====                                       S                            SSSS      SSSS                                            ",
    "   @  b    f f        b  e         SSSe SSS                e   e f   C       ^^    e     S          e  e   ^         SSSSS      SSSS  e   e   e       b     f   e    P          ",
    "#############################   ##############    #####################################  S  ####################   ##########   ################################################",
    "#############################   ##############    #####################################  S  ####################   ##########   ################################################",
    "#############################   ##############    #####################################  S  ####################   ##########   ################################################"
};
#define MAP_W 176

enum { T_EMPTY=0,T_GRASS=1,T_DIRT=2,T_BRICK=3,T_QBOX=4,T_USED=5,T_STONE=6,T_SPIKE=7,
       T_PLANK=8,T_BUSH=9,T_FLOWER=10,T_FLAG=11,T_FLAG_ON=12,T_PORTAL_TOP=13,T_PORTAL=14,
       T_QBOX_SHINE=15,T_GEM=16 };
enum { ST_TITLE, ST_PLAY, ST_DYING, ST_OVER, ST_CLEAR };
#define MAX_ENEMIES 16
#define MAX_POPS 6

typedef struct { int32_t x,y,vx,vy; uint8_t state,frame; uint16_t timer; } enemy_t;
typedef struct { int32_t x,y; uint16_t timer; } pop_t;

static uint8_t map[MAP_H][MAP_W];
static enemy_t enemies[MAX_ENEMIES];
static int enemy_count;
static pop_t pops[MAX_POPS];
static int start_col,check_col,check_row;

/* 玩家碰撞盒 10×14，精灵 16×16 在盒左上 (-3,-2)。 */
#define PW 10
#define PH 14
static int32_t px,py,pvx,pvy;
static int on_ground,facing_left,coyote,jump_buffer,jump_held,anim,invuln;
static int state,lives,gems,score,best,saved_best,run_score_base,run_gems_base;
static int checkpoint_on,state_ticks;
static int cam_x;
static int bump_col=-1,bump_row,bump_ticks;
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
    if(c<0||c>=MAP_W) return T_STONE;          /* 左右边界视为墙 */
    if(r<0||r>=MAP_H) return T_EMPTY;          /* 上方开阔、下方是坑 */
    return map[r][c];
}
static int solid(int t) { return t==T_GRASS||t==T_DIRT||t==T_BRICK||t==T_QBOX||t==T_USED||t==T_STONE; }
static int floor_div(int v,int d) { return v>=0 ? v/d : -((-v+d-1)/d); }

static void load_level(void) {
    enemy_count=0;
    for(int r=0;r<MAP_H;r++) for(int c=0;c<MAP_W;c++) {
        char ch=level_rows[r][c];uint8_t t=T_EMPTY;
        switch(ch) {
        case '#': t=(r>0&&level_rows[r-1][c]=='#')?T_DIRT:T_GRASS; break;
        case 'B': t=T_BRICK; break;
        case 'Q': t=T_QBOX; break;
        case 'S': t=T_STONE; break;
        case '=': t=T_PLANK; break;
        case '^': t=T_SPIKE; break;
        case 'o': t=T_GEM; break;
        case 'b': t=T_BUSH; break;
        case 'f': t=T_FLOWER; break;
        case 'C': t=T_FLAG; check_col=c; check_row=r; break;
        case 'P': t=T_PORTAL; if(r>0) map[r-1][c]=T_PORTAL_TOP; break;
        case '@': start_col=c; break;
        case 'e':
            if(enemy_count<MAX_ENEMIES) {
                enemy_t *e=&enemies[enemy_count++];
                e->x=c*TILE*SUB+2*SUB;e->y=(r*TILE+6)*SUB;e->vx=-10;e->vy=0;e->state=0;e->frame=0;e->timer=0;
            }
            break;
        default: break;
        }
        map[r][c]=t;
    }
    if(checkpoint_on) map[check_row][check_col]=T_FLAG_ON;
    for(int i=0;i<MAX_POPS;i++) pops[i].timer=0;
    bump_col=-1;
}
static void place_player(void) {
    int col=checkpoint_on?check_col:start_col;
    px=(col*TILE+3)*SUB;py=(10*TILE+2)*SUB;pvx=pvy=0;
    on_ground=1;facing_left=0;coyote=jump_buffer=0;anim=0;
}
static void update_camera(void) {
    int target=px/SUB+PW/2-VIEW/2+8;
    if(target<0) target=0;
    if(target>MAP_W*TILE-VIEW) target=MAP_W*TILE-VIEW;
    cam_x=target;
}
static void new_game(void) {
    lives=3;gems=0;score=0;checkpoint_on=0;run_score_base=0;run_gems_base=0;
    load_level();place_player();update_camera();
}

/* ---------- 存档 ---------- */
static int save_best(void) {
    if(score>best) best=score;
    if(best==saved_best) return 0;
    D(kv_set(0,best));saved_best=best;return 0;
}

/* ---------- 物理 ---------- */
static int pop_spawn(int x,int y) {
    for(int i=0;i<MAX_POPS;i++) if(!pops[i].timer) { pops[i].x=x;pops[i].y=y;pops[i].timer=14;return 0; }
    return 0;
}
static int collect(int c,int r) {
    map[r][c]=T_EMPTY;gems++;score+=100;SFX(GEM);return 0;
}
static int hit_block(int c,int r) {
    int t=tile_at(c,r);
    if(t==T_QBOX) {
        map[r][c]=T_USED;bump_col=c;bump_row=r;bump_ticks=6;
        gems++;score+=100;pop_spawn(c*TILE,r*TILE-TILE);SFX(GEM);
        /* 顶起箱子同时顶掉上方甲虫 */
        for(int i=0;i<enemy_count;i++) {
            enemy_t *e=&enemies[i];
            if(e->state==1 && e->x/SUB/TILE==c && (e->y/SUB+10)/TILE==r-1) { e->state=3;e->vy=-120;score+=200; }
        }
    } else if(t==T_BRICK||t==T_USED||t==T_STONE) {
        if(t==T_BRICK) { bump_col=c;bump_row=r;bump_ticks=6; }
        SFX(BUMP);
    }
    return 0;
}
static int player_die(void) {
    state=ST_DYING;state_ticks=0;pvx=0;pvy=-150;
    (void)haptic_pulse(70,180);SFX(HURT);return 0;
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
static int move_player_y(void) {
    int32_t ny=py+pvy;
    int left=floor_div(px/SUB,TILE),right=floor_div((px/SUB)+PW-1,TILE);
    on_ground=0;
    if(pvy>=0) {
        int old_bottom=(py/SUB)+PH,edge=(ny/SUB)+PH-1,r=floor_div(edge,TILE);
        for(int c=left;c<=right;c++) {
            int t=tile_at(c,r);
            if(solid(t)||(t==T_PLANK&&old_bottom<=r*TILE)) { ny=(r*TILE-PH)*SUB;pvy=0;on_ground=1;break; }
        }
    } else {
        int edge=floor_div(ny,SUB),r=floor_div(edge,TILE);
        /* 头顶优先撞击居中的那块 */
        int center=floor_div((px/SUB)+PW/2,TILE),hit=-1;
        if(solid(tile_at(center,r))) hit=center;
        else for(int c=left;c<=right;c++) if(solid(tile_at(c,r))) { hit=c;break; }
        if(hit>=0) { ny=((r+1)*TILE)*SUB;pvy=0;D(hit_block(hit,r)); }
    }
    py=ny;return 0;
}
static int overlap(int ax,int ay,int aw,int ah,int bx,int by,int bw,int bh) {
    return ax<bx+bw && bx<ax+aw && ay<by+bh && by<ay+ah;
}
static int touch_tiles(void) {
    int x=px/SUB,y=py/SUB;
    for(int r=floor_div(y,TILE);r<=floor_div(y+PH-1,TILE);r++)
        for(int c=floor_div(x,TILE);c<=floor_div(x+PW-1,TILE);c++) {
            int t=tile_at(c,r);
            if(t==T_GEM) D(collect(c,r));
            else if(t==T_SPIKE && !invuln && overlap(x,y,PW,PH,c*TILE+2,r*TILE+6,12,10)) return player_die();
            else if(t==T_FLAG && c==check_col && r==check_row) {
                checkpoint_on=1;map[r][c]=T_FLAG_ON;run_score_base=score;run_gems_base=gems;SFX(CHECK);
            } else if(t==T_PORTAL||t==T_PORTAL_TOP) {
                state=ST_CLEAR;state_ticks=0;score+=1000+lives*500;
                (void)haptic_pulse(40,120);SFX(CLEAR);return save_best();
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
static int step_enemies(void) {
    int x=px/SUB,y=py/SUB;
    for(int i=0;i<enemy_count;i++) {
        enemy_t *e=&enemies[i];
        if(e->state==0) { if(e->x/SUB < cam_x+VIEW+32) e->state=1; else continue; }
        if(e->state==2) { if(++e->timer>20) e->state=4; continue; }
        if(e->state==4) continue;
        e->vy+=24;if(e->vy>160) e->vy=160;
        if(e->state==3) { e->x+=e->vx/2;e->y+=e->vy;if(e->y/SUB>MAP_H*TILE+32) e->state=4; continue; }
        /* 水平：撞墙转向 */
        int32_t nx=e->x+e->vx;int ex=nx/SUB,ey=e->y/SUB;
        int c=floor_div(e->vx>0?ex+11:ex,TILE),r=floor_div(ey+5,TILE);
        if(solid(tile_at(c,r))) e->vx=-e->vx; else e->x=nx;
        /* 垂直：落地 */
        int32_t ny=e->y+e->vy;int bottom=ny/SUB+9;int rr=floor_div(bottom,TILE);
        int c0=floor_div(e->x/SUB,TILE),c1=floor_div(e->x/SUB+11,TILE);
        int t0=tile_at(c0,rr),t1=tile_at(c1,rr);
        if(e->vy>=0&&(solid(t0)||solid(t1)||t0==T_PLANK||t1==T_PLANK)&&(e->y/SUB+10)<=rr*TILE) { ny=(rr*TILE-10)*SUB;e->vy=0; }
        e->y=ny;
        if(e->y/SUB>MAP_H*TILE+32) { e->state=4; continue; }
        if(++e->timer>=8) { e->timer=0;e->frame^=1; }
        /* 与玩家接触 */
        if(state==ST_PLAY && overlap(x,y,PW,PH,e->x/SUB,e->y/SUB,12,10)) {
            if(pvy>0 && y+PH-pvy/SUB<=e->y/SUB+6) {
                e->state=2;e->timer=0;score+=200;pvy=jump_held?-168:-120;
                (void)haptic_pulse(30,25);SFX(STOMP);
            } else if(!invuln) return player_die();
        }
    }
    return 0;
}
static int step_play(void) {
    if(invuln) invuln--;
    int target=input_dir();
    int accel=on_ground?4:3;
    if(target>pvx) { pvx+=accel;if(pvx>target) pvx=target; }
    else if(target<pvx) { pvx-=accel;if(pvx<target) pvx=target; }
    if(target<0) facing_left=1; else if(target>0) facing_left=0;
    if(on_ground) coyote=3; else if(coyote) coyote--;
    if(jump_buffer) {
        if(coyote) { pvy=-168;coyote=0;jump_buffer=0;on_ground=0;SFX(JUMP); }
        else jump_buffer--;
    }
    pvy+=(jump_held&&pvy<0)?12:24;if(pvy>160) pvy=160;
    D(move_player_x());
    D(move_player_y());
    if(py/SUB>MAP_H*TILE+8) return player_die();
    D(touch_tiles());
    if(state!=ST_PLAY) return 0;
    D(step_enemies());
    if(on_ground && pvx) anim++; else if(on_ground) anim=0;
    update_camera();
    return 0;
}
static int tick(void) {
    ticks++;state_ticks++;
    for(int i=0;i<MAX_POPS;i++) if(pops[i].timer) { pops[i].timer--;pops[i].y-=2; }
    if(bump_ticks&&!--bump_ticks) bump_col=-1;
    if(state==ST_PLAY) return step_play();
    if(state==ST_DYING) {
        pvy+=16;if(pvy>160) pvy=160;py+=pvy;
        if(state_ticks>=40) {
            lives--;
            if(lives<=0) { state=ST_OVER;state_ticks=0;return save_best(); }
            score=run_score_base;gems=run_gems_base;
            load_level();place_player();update_camera();invuln=45;state=ST_PLAY;
        }
        return 0;
    }
    if(state==ST_TITLE) { cam_x=(int)((ticks/2)%(MAP_W*TILE-VIEW)); }
    return 0;
}

/* ---------- 绘制 ---------- */
static union { uint32_t align; uint8_t bytes[8192]; } gfx;
static uint32_t gfx_len;
static int gfx_overflow;
static void emit(const void *record,uint32_t size) {
    if(gfx_len+size>sizeof(gfx.bytes)) { gfx_overflow=1;return; }
    const uint8_t *p=(const uint8_t *)record;
    for(uint32_t i=0;i<size;i++) gfx.bytes[gfx_len+i]=p[i];
    gfx_len+=size;
}
static void sprite(int x,int y,int w,int h,int u,int v,int sw,int sh,uint32_t flags) {
    if(x>=VIEW||y>=VIEW||x+w<=0||y+h<=0) return;
    tinyrt_gfx_sprite_t s={TINYRT_GFX_SPRITE,sizeof(s),x,y,w,h,0,(uint32_t)u,(uint32_t)v,(uint32_t)sw,(uint32_t)sh,flags|TINYRT_GFX_TRANSPARENT,0,0};
    emit(&s,sizeof(s));
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
static void compose(void) {
    gfx_len=0;gfx_overflow=0;
    struct { uint16_t op,size;uint32_t color; } clear={TINYRT_GFX_CLEAR,8,SKY};
    emit(&clear,sizeof(clear));
    /* 远景：云 1/8 视差、山 1/3 视差 */
    for(int i=-1;i<3;i++) {
        int x=i*120-(cam_x/8)%120;
        sprite(x+20,34+(i&1)*14,32,16,64,64,32,16,0);
    }
    for(int i=-1;i<4;i++) {
        int x=i*96-(cam_x/3)%96;
        sprite(x,120,96,48,96,64,32,16,0);
    }
    /* 关卡窗口 */
    int col0=cam_x/TILE,offset=cam_x%TILE;
    struct { uint16_t op,size;int32_t x,y;uint32_t cols,rows,tw,th,tex,sx,sy,flags,pal;uint8_t ids[WIN_COLS*WIN_ROWS]; } tm;
    tm.op=TINYRT_GFX_TILEMAP;tm.size=sizeof(tm);tm.x=-offset;tm.y=VIEW_Y;tm.cols=WIN_COLS;tm.rows=WIN_ROWS;
    tm.tw=TILE;tm.th=TILE;tm.tex=0;tm.sx=0;tm.sy=0;tm.flags=TINYRT_GFX_TRANSPARENT;tm.pal=0;
    int gem_frame=(int)(ticks/4)%4,shine=(ticks/9)%4==0;
    for(int r=0;r<WIN_ROWS;r++) for(int c=0;c<WIN_COLS;c++) {
        int t=tile_at(col0+c,r<MAP_H?r:MAP_H-1);
        if(r==MAP_H&&t!=T_DIRT&&t!=T_STONE) t=T_EMPTY;
        if(col0+c>=MAP_W) t=T_EMPTY;
        if(t==T_GEM) t=T_GEM+gem_frame;
        else if(t==T_QBOX&&shine) t=T_QBOX_SHINE;
        if(col0+c==bump_col&&r==bump_row) t=T_EMPTY;
        tm.ids[r*WIN_COLS+c]=(uint8_t)t;
    }
    emit(&tm,sizeof(tm));
    if(bump_col>=0) {
        int lift=bump_ticks>3?6-bump_ticks:bump_ticks;
        cell(bump_col*TILE-cam_x,bump_row*TILE+VIEW_Y-lift*2,map[bump_row][bump_col],0);
    }
    for(int i=0;i<MAX_POPS;i++) if(pops[i].timer)
        cell(pops[i].x-cam_x,pops[i].y+VIEW_Y,T_GEM+(int)(ticks/2)%4,0);
    for(int i=0;i<enemy_count;i++) {
        enemy_t *e=&enemies[i];
        if(e->state==0||e->state==4) continue;
        int idx=e->state==2?34:32+e->frame;
        uint32_t fl=(e->vx>0?TINYRT_GFX_FLIP_X:0)|(e->state==3?TINYRT_GFX_FLIP_Y:0);
        cell(e->x/SUB-2-cam_x,e->y/SUB-6+VIEW_Y,idx,fl);
    }
    if(state!=ST_TITLE && !(invuln&&(ticks&2))) {
        int idx=24;
        if(state==ST_DYING) idx=28;
        else if(!on_ground) idx=27;
        else if(pvx) idx=25+(anim/4)%2;
        if(state!=ST_CLEAR||state_ticks<6)
            cell(px/SUB-3-cam_x,py/SUB-2+VIEW_Y,idx,facing_left?TINYRT_GFX_FLIP_X:0);
    }
    /* HUD */
    hud.op=TINYRT_GFX_SPRITE_BATCH;hud.tex=0;hud.sw=8;hud.sh=8;hud.flags=TINYRT_GFX_TRANSPARENT;hud.pal=0;hud.count=0;
    if(state!=ST_TITLE) {
        glyph(84,22,12);glyph(92,22,10);number(100,22,gems>99?99:gems,2);
        glyph(124,22,11);glyph(132,22,10);number(140,22,lives<0?0:lives,1);
        number(96,32,score>99999?99999:score,5);
    }
    if(state==ST_TITLE||state==ST_OVER||state==ST_CLEAR) rect(34,78,165,80,RGB565(20,16,40),170);
    if(state==ST_OVER||state==ST_CLEAR) {
        number(96,128,score>99999?99999:score,5);
        glyph(88,142,13);number(100,142,best>99999?99999:best,5);
    }
    if(hud.count) { hud.size=(uint16_t)(28+hud.count*16);emit(&hud,hud.size); }
    frame_ready=1;
}
static int text(int y,int h,const char *s,uint32_t color,int px_size) {
    uint32_t n=0;while(s[n]) n++;
    return draw_text_box(68,y,330,h,s,n,color,px_size,1);
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
        D(text(232,30,pad_on?"D-PAD OR STICK: MOVE":"TILT OR EDGES: MOVE",0xffffff,18));
        D(text(262,30,pad_on?"A: JUMP":"TAP CENTER: JUMP",0xffffff,18));
        if((ticks/15)&1) D(text(390,36,pad_on?"PRESS START":"TAP TO START",0xffffff,24));
    } else if(state==ST_OVER) {
        D(text(176,48,"GAME OVER",0xff6070,36));
        D(text(220,30,"SCORE",0xffffff,18));
        if(state_ticks>30) D(text(390,36,"TAP TO RETRY",0xffffff,24));
    } else if(state==ST_CLEAR) {
        D(text(176,48,"STAGE CLEAR!",0x42f5c8,36));
        D(text(220,30,"SCORE",0xffffff,18));
        if(state_ticks>30) D(text(390,36,"TAP TO PLAY AGAIN",0xffffff,24));
    }
    return 0;
}

/* ---------- 回调 ---------- */
static int press_jump(void) { jump_buffer=5;jump_held=1;return 0; }
static int start_or_jump(int x) {
    if(paused) { paused=0;last_ms=now_ms();return 0; }
    if(state==ST_TITLE) { new_game();state=ST_PLAY;invuln=0;touch_zone=0;return 0; }
    if(state==ST_OVER||state==ST_CLEAR) {
        if(state_ticks<=30) return 0;
        D(save_best());new_game();state=ST_PLAY;touch_zone=0;return 0;
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
    D(asset_read(PALETTE_OFFSET,palette,PALETTE_COUNT*2)!=(int32_t)(PALETTE_COUNT*2));
    D(gfx_pal_upload(0,0,PALETTE_COUNT,palette));
    D(gfx_tex_upload(0,TINYRT_GFX_INDEX8,128,128,(const void *)(uintptr_t)ATLAS_OFFSET,ATLAS_LENGTH,TINYRT_GFX_FROM_ASSET));
    best=kv_get(0,0);if(best<0||best>99999) best=0;saved_best=best;
    state=ST_TITLE;ticks=0;touch_zone=tilt=0;jump_held=0;invuln=0;
    pad_on=pad_lx=paused=0;pad_buttons=0;
    new_game();cam_x=0;
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
int32_t tinyrt_stop(void) { touch_zone=0;jump_held=0;pad_buttons=0;return save_best(); }
