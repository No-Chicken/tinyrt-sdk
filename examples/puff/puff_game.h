/* Puff 2 game simulation. Deterministic integer logic shared with main.c. */
#ifndef PUFF_GAME_H
#define PUFF_GAME_H
#include <stdint.h>

/* ---- units: positions in 1/16 px, time step 10 ms ---- */
#define PG_FP 16
#define PG_STEP_MS 10
#define PG_SCREEN 466
#define PG_MAX_OBJECTS 96
#define PG_MAX_BALLOONS 5
#define PG_PLAYER_SCREEN_Y 300   /* player is drawn at this screen y */
#define PG_PLAYER_R 15           /* body collision radius (px) */
#define PG_BALLOON_R 13          /* balloon cluster collision radius (px) */
#define PG_FIELD_MIN 52          /* playfield x range inside the round screen */
#define PG_FIELD_MAX 414

enum { PG_LEVEL_COUNT = 16 };
enum { PG_KIND_CLIMB, PG_KIND_TIMED, PG_KIND_COLLECT, PG_KIND_ESCAPE, PG_KIND_BOSS };
enum {
    PO_NONE, PO_COIN, PO_STAR, PO_BALLOON, PO_SHIELD, PO_MAGNET, PO_FEATHER, PO_HOURGLASS, PO_PIN,
    PO_BEE, PO_THORN, PO_WIND, PO_BOUNCE, PO_THUNDER, PO_CROW, PO_SPIKEBALL, PO_RAIN, PO_VORTEX,
    PO_ZAP, PO_FIREFLY, PO_GOAL
};
enum { PB_NONE, PB_QUEEN, PB_STORMCLOUD, PB_OWL, PB_KING };
enum { PG_PLAYING, PG_WON, PG_LOST };
/* Hazard hitbox padding in px. Negative = forgiving. The level solver raises it
 * to prove a route still exists with a larger hitbox than players get. */
static int pg_hazard_pad = -4;

/* Static level object. Units in pixels; y measured upward from the floor. */
typedef struct { uint8_t type; int16_t x, y; int16_t a, b; } pg_spawn_t;
/* a/b meaning per type:
 *  BEE/SPIKEBALL: a = patrol half-width (px), b = speed (px/s, negative = start left)
 *  THORN/ZAP: a = width (px), b = height (px); ZAP toggles every 1.5 s (b<0 = starts off)
 *  WIND: a = width, b = push (px/s, + right); height 160
 *  BOUNCE: a = width
 *  THUNDER: a = period ms, b = phase ms
 *  CROW: a = trigger range (px), b = dive speed (px/s)
 *  RAIN: a = width, b = height (push down)
 *  VORTEX: a = radius, b = pull (px/s)
 *  FIREFLY: restores light radius
 *  GOAL: a = width */

typedef struct {
    const char *name;
    uint8_t world, kind;
    int16_t height;      /* goal height in px, or arena height for bosses */
    int16_t time_s;      /* time limit (TIMED/COLLECT) or 0 */
    int16_t coins_needed;/* COLLECT target */
    int16_t scroll;      /* ESCAPE storm wall speed px/s */
    uint8_t boss;
    uint8_t dark;        /* limited visibility */
    const pg_spawn_t *spawns;
    uint16_t spawn_count;
} pg_level_t;

typedef struct {
    uint8_t type, alive, flags;
    int32_t x, y;        /* 1/16 px, y upward */
    int32_t ox, oy;      /* spawn position */
    int16_t a, b;
    int32_t t;           /* per object timer/state */
    int32_t vx, vy;
} pg_obj_t;

typedef struct {
    uint8_t blow;        /* 0..100 blow strength */
    int8_t tilt;         /* -100..100 */
    uint8_t use_item;    /* edge triggered by caller */
} pg_input_t;

typedef struct {
    const pg_level_t *level;
    int level_index;
    int32_t px, py, vx, vy;      /* player, 1/16 px */
    int balloons, start_balloons;
    int invuln_ms, shield_ms, magnet_ms, slow_ms, dash_ms;
    int has_feather;
    int coins, stars, combo, combo_ms, score;
    int time_ms;                 /* elapsed */
    int time_limit_ms;           /* 0 = none */
    int32_t wall_y;              /* escape storm wall, 1/16 px */
    int light;                   /* visibility radius for dark levels */
    int state;
    int hits_taken;
    int boss_hp, boss_max, boss_hurt_ms, boss_phase;
    int32_t bx, by, bvx;         /* boss position 1/16 px */
    int boss_timer, boss_attack;
    uint32_t rng;
    int events;                  /* bit flags for one-shot feedback, cleared by caller */
    pg_obj_t obj[PG_MAX_OBJECTS];
    int obj_count;
    int camera;                  /* world y (px) at the bottom of the screen */
} pg_game_t;

enum { /* event bits for sound/haptics */
    PE_COIN = 1, PE_STAR = 2, PE_HIT = 4, PE_ITEM = 8, PE_DASH = 16, PE_BOSS_HIT = 32,
    PE_WIN = 64, PE_LOSE = 128, PE_BOUNCE = 256, PE_ZAP = 512
};

/* ------------------------------------------------------------------ level data */
#define S(t,x,y,a,b) {PO_##t,x,y,a,b}
/* World 1: garden */
static const pg_spawn_t L11[] = {
    S(COIN,233,140,0,0),S(COIN,233,190,0,0),S(COIN,233,240,0,0),S(COIN,180,330,0,0),S(COIN,286,330,0,0),
    S(STAR,120,420,0,0),S(COIN,233,500,0,0),S(COIN,300,580,0,0),S(COIN,340,660,0,0),S(STAR,350,760,0,0),
    S(BALLOON,233,700,0,0),S(COIN,233,850,0,0),S(COIN,180,930,0,0),S(COIN,140,1010,0,0),S(STAR,110,1080,0,0),
    S(GOAL,233,1200,200,0),
};
static const pg_spawn_t L12[] = {
    S(COIN,233,180,0,0),S(BEE,233,330,110,70),S(COIN,233,420,0,0),S(STAR,90,470,0,0),
    S(COIN,320,560,0,0),S(BEE,233,700,120,-90),S(SHIELD,233,620,0,0),S(COIN,140,820,0,0),
    S(STAR,380,880,0,0),S(BEE,233,1040,130,100),S(COIN,233,1140,0,0),S(COIN,233,1200,0,0),
    S(BALLOON,300,1300,0,0),S(BEE,233,1400,100,-110),S(COIN,160,1500,0,0),S(STAR,233,1620,0,0),
    S(GOAL,233,1800,200,0),
};
static const pg_spawn_t L13[] = {
    S(THORN,120,300,140,24),S(THORN,350,300,120,24),S(COIN,250,250,0,0),S(COIN,250,330,0,0),
    S(COIN,250,380,0,0),S(COIN,90,420,0,0),S(COIN,380,430,0,0),S(BEE,233,520,100,80),
    S(THORN,330,640,180,24),S(COIN,140,600,0,0),S(COIN,140,680,0,0),S(STAR,90,740,0,0),
    S(COIN,233,800,0,0),S(THORN,140,900,180,24),S(COIN,330,860,0,0),S(COIN,330,940,0,0),
    S(MAGNET,330,1000,0,0),S(COIN,233,1080,0,0),S(COIN,190,1120,0,0),S(COIN,276,1120,0,0),
    S(BEE,233,1220,120,-90),S(STAR,380,1260,0,0),S(COIN,233,1320,0,0),S(COIN,233,1380,0,0),
    S(THORN,120,1480,140,24),S(THORN,350,1480,120,24),S(COIN,250,1440,0,0),S(COIN,250,1530,0,0),
    S(COIN,150,1620,0,0),S(COIN,320,1700,0,0),S(STAR,233,1780,0,0),S(COIN,233,1850,0,0),
    S(GOAL,233,2000,200,0),
};
/* World 2: clouds */
static const pg_spawn_t L21[] = {
    S(COIN,233,200,0,0),S(WIND,233,330,330,70),S(COIN,330,360,0,0),S(BOUNCE,140,500,110,0),
    S(STAR,100,560,0,0),S(COIN,233,620,0,0),S(WIND,233,760,330,-80),S(COIN,140,800,0,0),
    S(FEATHER,233,880,0,0),S(BOUNCE,320,1000,110,0),S(COIN,320,1080,0,0),S(STAR,380,1150,0,0),
    S(WIND,233,1300,330,90),S(COIN,233,1300,0,0),S(BEE,233,1480,110,90),S(BALLOON,140,1560,0,0),
    S(COIN,233,1660,0,0),S(WIND,233,1820,330,-90),S(STAR,233,1950,0,0),S(COIN,233,2050,0,0),
    S(GOAL,233,2200,200,0),
};
static const pg_spawn_t L22[] = {
    S(COIN,233,180,0,0),S(THUNDER,140,360,2400,0),S(THUNDER,326,360,2400,1200),S(COIN,233,360,0,0),
    S(STAR,233,520,0,0),S(HOURGLASS,120,600,0,0),S(WIND,233,700,330,80),S(COIN,300,760,0,0),
    S(THUNDER,233,900,2000,0),S(COIN,120,960,0,0),S(COIN,346,960,0,0),S(SHIELD,233,1060,0,0),
    S(THUNDER,140,1200,2200,600),S(THUNDER,326,1200,2200,1700),S(STAR,90,1280,0,0),
    S(WIND,233,1420,330,-90),S(COIN,233,1480,0,0),S(BEE,233,1620,120,100),S(COIN,233,1720,0,0),
    S(THUNDER,233,1860,1800,900),S(HOURGLASS,350,1900,0,0),S(STAR,380,2000,0,0),S(COIN,233,2120,0,0),
    S(COIN,233,2220,0,0),S(GOAL,233,2400,200,0),
};
static const pg_spawn_t L23[] = {
    S(COIN,233,220,0,0),S(BOUNCE,233,420,120,0),S(COIN,140,500,0,0),S(COIN,326,500,0,0),
    S(STAR,100,640,0,0),S(BEE,233,760,110,110),S(FEATHER,233,860,0,0),S(BOUNCE,130,1000,110,0),
    S(BOUNCE,336,1100,110,0),S(COIN,233,1180,0,0),S(STAR,380,1260,0,0),S(WIND,233,1420,330,90),
    S(BALLOON,233,1500,0,0),S(BEE,233,1640,130,-110),S(COIN,233,1760,0,0),S(BOUNCE,233,1900,140,0),
    S(STAR,233,2080,0,0),S(COIN,233,2200,0,0),S(COIN,233,2320,0,0),S(GOAL,233,2600,200,0),
};
/* World 3: night (dark) */
static const pg_spawn_t L31[] = {
    S(COIN,233,200,0,0),S(FIREFLY,233,300,0,0),S(BEE,233,450,110,80),S(COIN,140,520,0,0),
    S(STAR,90,600,0,0),S(FIREFLY,330,700,0,0),S(SPIKEBALL,233,850,120,90),S(COIN,233,920,0,0),
    S(SHIELD,330,980,0,0),S(FIREFLY,140,1100,0,0),S(STAR,380,1150,0,0),S(BEE,233,1300,130,-100),
    S(COIN,233,1400,0,0),S(FIREFLY,233,1550,0,0),S(SPIKEBALL,233,1700,130,-110),S(BALLOON,140,1800,0,0),
    S(FIREFLY,300,1950,0,0),S(COIN,233,2050,0,0),S(STAR,233,2250,0,0),S(FIREFLY,233,2350,0,0),
    S(COIN,233,2450,0,0),S(GOAL,233,2600,200,0),
};
static const pg_spawn_t L32[] = {
    S(COIN,233,200,0,0),S(COIN,190,260,0,0),S(COIN,276,260,0,0),S(CROW,380,480,170,220),
    S(COIN,120,420,0,0),S(COIN,120,480,0,0),S(STAR,90,560,0,0),S(SPIKEBALL,233,680,120,100),
    S(COIN,233,760,0,0),S(COIN,300,820,0,0),S(MAGNET,233,900,0,0),S(CROW,90,1100,170,230),
    S(COIN,330,1000,0,0),S(COIN,330,1060,0,0),S(COIN,330,1120,0,0),S(STAR,380,1200,0,0),
    S(COIN,233,1280,0,0),S(COIN,190,1340,0,0),S(COIN,276,1340,0,0),S(SPIKEBALL,233,1460,130,-110),
    S(BALLOON,233,1560,0,0),S(COIN,140,1600,0,0),S(COIN,140,1660,0,0),S(CROW,380,1800,170,240),
    S(COIN,330,1750,0,0),S(COIN,233,1880,0,0),S(COIN,233,1940,0,0),S(STAR,233,2040,0,0),
    S(COIN,190,2120,0,0),S(COIN,276,2120,0,0),S(COIN,233,2200,0,0),S(PIN,140,2260,0,0),
    S(COIN,233,2320,0,0),S(COIN,190,2400,0,0),S(COIN,276,2400,0,0),S(COIN,233,2480,0,0),
    S(COIN,150,2560,0,0),S(COIN,316,2560,0,0),S(COIN,233,2640,0,0),S(GOAL,233,2800,200,0),
};
static const pg_spawn_t L33[] = {
    S(COIN,233,200,0,0),S(SPIKEBALL,233,380,140,110),S(HOURGLASS,233,470,0,0),S(STAR,90,560,0,0),
    S(CROW,380,720,170,230),S(COIN,233,780,0,0),S(SPIKEBALL,233,950,130,-120),S(SHIELD,330,1040,0,0),
    S(STAR,380,1150,0,0),S(SPIKEBALL,233,1300,140,130),S(CROW,90,1480,170,240),S(COIN,233,1560,0,0),
    S(HOURGLASS,140,1660,0,0),S(SPIKEBALL,233,1800,140,-130),S(BALLOON,330,1900,0,0),
    S(STAR,233,2050,0,0),S(CROW,380,2240,170,250),S(COIN,233,2340,0,0),S(SPIKEBALL,233,2500,140,140),
    S(COIN,233,2650,0,0),S(COIN,233,2780,0,0),S(GOAL,233,3000,200,0),
};
/* World 4: storm */
static const pg_spawn_t L41[] = {
    S(COIN,233,200,0,0),S(RAIN,140,420,150,260),S(COIN,330,420,0,0),S(STAR,380,520,0,0),
    S(VORTEX,233,720,110,60),S(COIN,100,760,0,0),S(SHIELD,366,820,0,0),S(RAIN,330,1000,150,260),
    S(COIN,140,1000,0,0),S(STAR,90,1100,0,0),S(BEE,233,1240,120,110),S(VORTEX,233,1450,110,70),
    S(BALLOON,100,1500,0,0),S(RAIN,233,1700,200,240),S(COIN,90,1700,0,0),S(COIN,376,1700,0,0),
    S(FEATHER,233,1900,0,0),S(SPIKEBALL,233,2050,130,120),S(STAR,233,2250,0,0),S(RAIN,120,2450,130,260),
    S(RAIN,346,2450,130,260),S(COIN,233,2450,0,0),S(COIN,233,2650,0,0),S(COIN,233,2800,0,0),
    S(GOAL,233,3000,200,0),
};
static const pg_spawn_t L42[] = {
    S(COIN,233,220,0,0),S(ZAP,233,420,200,20),S(COIN,233,480,0,0),S(STAR,90,560,0,0),
    S(FEATHER,233,620,0,0),S(ZAP,150,780,160,-20),S(ZAP,316,960,160,20),S(COIN,233,1020,0,0),
    S(SHIELD,380,1060,0,0),S(STAR,380,1140,0,0),S(BOUNCE,233,1260,120,0),S(ZAP,233,1440,200,-20),
    S(BALLOON,140,1520,0,0),S(SPIKEBALL,233,1660,120,100),S(COIN,233,1780,0,0),S(ZAP,150,1920,160,20),
    S(ZAP,316,2080,160,-20),S(STAR,233,2180,0,0),S(HOURGLASS,233,2260,0,0),S(ZAP,233,2440,200,20),
    S(COIN,233,2560,0,0),S(BEE,233,2720,120,110),S(COIN,233,2860,0,0),S(COIN,233,3000,0,0),
    S(GOAL,233,3200,200,0),
};
static const pg_spawn_t L43[] = {
    S(COIN,233,200,0,0),S(COIN,190,250,0,0),S(COIN,276,250,0,0),S(RAIN,100,420,120,260),S(COIN,233,400,0,0),
    S(COIN,330,450,0,0),S(STAR,380,520,0,0),S(ZAP,233,640,240,20),S(COIN,233,580,0,0),S(COIN,233,700,0,0),
    S(MAGNET,140,760,0,0),S(VORTEX,233,900,110,70),S(COIN,100,880,0,0),S(COIN,366,880,0,0),S(COIN,100,960,0,0),
    S(COIN,366,960,0,0),S(BEE,233,1100,130,120),S(COIN,233,1180,0,0),S(COIN,190,1240,0,0),S(COIN,276,1240,0,0),
    S(STAR,90,1320,0,0),S(CROW,380,1500,170,250),S(SHIELD,233,1420,0,0),S(COIN,233,1560,0,0),S(COIN,233,1620,0,0),
    S(WIND,233,1760,330,-90),S(COIN,140,1760,0,0),S(COIN,140,1820,0,0),S(BALLOON,330,1880,0,0),
    S(SPIKEBALL,233,2000,140,130),S(COIN,233,2080,0,0),S(COIN,190,2140,0,0),S(COIN,276,2140,0,0),
    S(STAR,233,2240,0,0),S(ZAP,233,2380,260,-20),S(COIN,233,2320,0,0),S(COIN,233,2460,0,0),
    S(THUNDER,140,2600,2200,0),S(THUNDER,326,2600,2200,1100),S(COIN,233,2600,0,0),S(COIN,233,2700,0,0),
    S(COIN,190,2780,0,0),S(COIN,276,2780,0,0),S(COIN,233,2860,0,0),S(HOURGLASS,233,2940,0,0),
    S(COIN,150,3000,0,0),S(COIN,316,3000,0,0),S(COIN,233,3080,0,0),S(COIN,190,3160,0,0),
    S(COIN,276,3160,0,0),S(COIN,233,3240,0,0),S(GOAL,233,3400,200,0),
};
/* Boss arenas: items spawn on a timer (see pg_boss_step). */
static const pg_spawn_t LB1[] = { S(BALLOON,100,140,0,0),S(PIN,366,140,0,0),S(STAR,233,60,0,0) };
static const pg_spawn_t LB2[] = { S(BALLOON,100,140,0,0),S(SHIELD,366,140,0,0),S(STAR,233,60,0,0) };
static const pg_spawn_t LB3[] = { S(BALLOON,100,140,0,0),S(PIN,366,140,0,0),S(STAR,233,60,0,0) };
static const pg_spawn_t LB4[] = { S(BALLOON,100,140,0,0),S(SHIELD,366,140,0,0),S(STAR,233,60,0,0) };
#undef S
#define PG_N(a) (uint16_t)(sizeof(a)/sizeof((a)[0]))

static const pg_level_t pg_levels[PG_LEVEL_COUNT] = {
    {"第一口气",1,PG_KIND_CLIMB,1200,0,0,0,PB_NONE,0,L11,PG_N(L11)},
    {"蜂巢小路",1,PG_KIND_CLIMB,1800,0,0,0,PB_NONE,0,L12,PG_N(L12)},
    {"刺藤迷宫",1,PG_KIND_COLLECT,2000,0,15,0,PB_NONE,0,L13,PG_N(L13)},
    {"蜂后",1,PG_KIND_BOSS,466,0,0,0,PB_QUEEN,0,LB1,PG_N(LB1)},
    {"风之阶梯",2,PG_KIND_CLIMB,2200,0,0,0,PB_NONE,0,L21,PG_N(L21)},
    {"雷云巷",2,PG_KIND_TIMED,2400,60,0,0,PB_NONE,0,L22,PG_N(L22)},
    {"白云快车",2,PG_KIND_ESCAPE,2600,0,0,55,PB_NONE,0,L23,PG_N(L23)},
    {"雷鸣云",2,PG_KIND_BOSS,466,0,0,0,PB_STORMCLOUD,0,LB2,PG_N(LB2)},
    {"萤火之夜",3,PG_KIND_CLIMB,2600,0,0,0,PB_NONE,1,L31,PG_N(L31)},
    {"乌鸦林",3,PG_KIND_COLLECT,2800,0,21,0,PB_NONE,0,L32,PG_N(L32)},
    {"星河",3,PG_KIND_TIMED,3000,75,0,0,PB_NONE,0,L33,PG_N(L33)},
    {"夜枭",3,PG_KIND_BOSS,466,0,0,0,PB_OWL,1,LB3,PG_N(LB3)},
    {"暴雨",4,PG_KIND_CLIMB,3000,0,0,0,PB_NONE,0,L41,PG_N(L41)},
    {"电网塔",4,PG_KIND_ESCAPE,3200,0,0,52,PB_NONE,0,L42,PG_N(L42)},
    {"风暴之眼",4,PG_KIND_COLLECT,3400,90,26,0,PB_NONE,0,L43,PG_N(L43)},
    {"风暴之王",4,PG_KIND_BOSS,466,0,0,0,PB_KING,0,LB4,PG_N(LB4)},
};

/* ------------------------------------------------------------------ helpers */
static inline int pg_abs(int v) { return v < 0 ? -v : v; }
static inline int32_t pg_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline uint32_t pg_rand(pg_game_t *g) { g->rng = g->rng * 1103515245u + 12345u; return (g->rng >> 16) & 0x7fff; }
static inline int pg_is_hazard(int t) {
    return t == PO_BEE || t == PO_THORN || t == PO_THUNDER || t == PO_CROW || t == PO_SPIKEBALL || t == PO_ZAP;
}
static inline int pg_is_pickup(int t) { return t >= PO_COIN && t <= PO_PIN; }
static inline int pg_total_stars(const pg_level_t *l) {
    int n = 0;
    for (int i = 0; i < l->spawn_count; i++) n += l->spawns[i].type == PO_STAR;
    return n;
}
/* integer sine, angle in 1/1024 turns, result -1024..1024 */
static inline int pg_sin(int a) {
    a &= 1023;
    int s = a < 512 ? 1 : -1;
    a &= 511;
    int x = a < 256 ? a : 512 - a;          /* 0..256 */
    int v = (x * (512 - x)) >> 4;           /* bhaskara-like parabola, peak 4096 at 256 */
    return s * (v >> 2);
}

static inline void pg_add(pg_game_t *g, int type, int x, int y, int a, int b) {
    if (g->obj_count >= PG_MAX_OBJECTS) return;
    pg_obj_t *o = &g->obj[g->obj_count++];
    *o = (pg_obj_t){0};
    o->type = (uint8_t)type; o->alive = 1;
    o->x = o->ox = x * PG_FP; o->y = o->oy = y * PG_FP; o->a = (int16_t)a; o->b = (int16_t)b;
}

/* ------------------------------------------------------------------ setup */
static inline void pg_start(pg_game_t *g, int level_index, uint32_t seed) {
    const pg_level_t *l = &pg_levels[level_index];
    /* Clear word by word: a struct-literal copy of 3.6 KB is costly on the interpreter. */
    for (uint32_t *w = (uint32_t *)g, *e = (uint32_t *)(g + 1); w < e; w++) *w = 0;
    g->level = l; g->level_index = level_index; g->rng = seed | 1u;
    g->balloons = g->start_balloons = 3;
    g->px = 233 * PG_FP; g->py = 24 * PG_FP;
    g->time_limit_ms = l->time_s * 1000;
    g->light = 150;
    g->wall_y = -260 * PG_FP;
    for (int i = 0; i < l->spawn_count; i++) {
        const pg_spawn_t *s = &l->spawns[i];
        pg_add(g, s->type, s->x, s->y, s->a, s->b);
    }
    if (l->boss) {
        static const int hp[] = {0, 3, 4, 4, 5};
        g->boss_hp = g->boss_max = hp[l->boss];
        g->bx = 233 * PG_FP; g->by = 360 * PG_FP; g->bvx = 70 * PG_FP / 100;
        g->boss_timer = 2000;
        g->py = 60 * PG_FP;
    }
}

/* ------------------------------------------------------------------ collisions */
static inline int pg_overlap_circle_rect(int32_t cx, int32_t cy, int r, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    int32_t nx = pg_clamp(cx, x0, x1), ny = pg_clamp(cy, y0, y1);
    int64_t dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy < (int64_t)r * r;
}
static inline int pg_near(int32_t ax, int32_t ay, int32_t bx, int32_t by, int r) {
    int64_t dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy < (int64_t)r * r;
}
/* Player hit shape: body circle + balloon cluster circle above it. */
static inline int pg_player_hits(const pg_game_t *g, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int pad) {
    int br = (PG_BALLOON_R + g->balloons * 2 + pad) * PG_FP;
    return pg_overlap_circle_rect(g->px, g->py, (PG_PLAYER_R + pad) * PG_FP, x0, y0, x1, y1) ||
           pg_overlap_circle_rect(g->px, g->py + 40 * PG_FP, br, x0, y0, x1, y1);
}
static inline int pg_player_near(const pg_game_t *g, int32_t x, int32_t y, int r, int pad) {
    int br = PG_BALLOON_R + g->balloons * 2 + pad;
    return pg_near(g->px, g->py, x, y, (PG_PLAYER_R + pad + r) * PG_FP) ||
           pg_near(g->px, g->py + 40 * PG_FP, x, y, (br + r) * PG_FP);
}

static inline void pg_hurt(pg_game_t *g) {
    if (g->invuln_ms > 0 || g->dash_ms > 0) return;
    g->hits_taken++;
    if (g->shield_ms > 0) { g->shield_ms = 0; g->invuln_ms = 800; g->events |= PE_HIT; return; }
    g->balloons--;
    g->invuln_ms = 1200;
    g->events |= PE_HIT;
    g->combo = 0;
    if (g->balloons <= 0) { g->state = PG_LOST; g->events |= PE_LOSE; }
}

static inline void pg_collect(pg_game_t *g, pg_obj_t *o) {
    o->alive = 0;
    switch (o->type) {
    case PO_COIN:
        g->coins++;
        g->combo = g->combo_ms > 0 ? g->combo + 1 : 1;
        g->combo_ms = 1000;
        g->score += g->combo >= 10 ? 20 : 10;
        g->events |= PE_COIN;
        break;
    case PO_STAR: g->stars++; g->score += 100; g->events |= PE_STAR; break;
    case PO_BALLOON: if (g->balloons < PG_MAX_BALLOONS) g->balloons++; g->events |= PE_ITEM; break;
    case PO_SHIELD: g->shield_ms = 15000; g->events |= PE_ITEM; break;
    case PO_MAGNET: g->magnet_ms = 8000; g->events |= PE_ITEM; break;
    case PO_FEATHER: g->has_feather = 1; g->events |= PE_ITEM; break;
    case PO_HOURGLASS: g->slow_ms = 6000; if (g->time_limit_ms) g->time_limit_ms += 10000; g->events |= PE_ITEM; break;
    case PO_PIN: {
        if (g->level->boss && g->boss_hp > 0) {
            g->boss_hp--; g->boss_hurt_ms = 1500; g->events |= PE_BOSS_HIT;
            if (g->boss_hp <= 0) { g->state = PG_WON; g->events |= PE_WIN; }
        } else {
            int best = -1; int64_t bd = 0;
            for (int i = 0; i < g->obj_count; i++) {
                pg_obj_t *e = &g->obj[i];
                if (!e->alive || !(e->type == PO_BEE || e->type == PO_CROW || e->type == PO_SPIKEBALL)) continue;
                int64_t dx = e->x - g->px, dy = e->y - g->py, d = dx * dx + dy * dy;
                if (d < (int64_t)(300 * PG_FP) * (300 * PG_FP) && (best < 0 || d < bd)) { best = i; bd = d; }
            }
            if (best >= 0) g->obj[best].alive = 0;
        }
        g->events |= PE_ITEM;
        break;
    }
    default: break;
    }
}

/* ------------------------------------------------------------------ objects */
static inline int pg_zap_on(const pg_obj_t *o, int t) { return ((t / 1500) & 1) == (o->b < 0); }
static inline int pg_thunder_strike(const pg_obj_t *o, int t) {
    int p = (t + o->b) % o->a;
    return p >= o->a - 500;           /* last 500 ms of the period is the strike */
}
static inline int pg_thunder_warn(const pg_obj_t *o, int t) {
    int p = (t + o->b) % o->a;
    return p >= o->a - 1300 && p < o->a - 500;
}

static inline void pg_object_step(pg_game_t *g, pg_obj_t *o, int dt) {
    int slow = g->slow_ms > 0 ? 2 : 1;
    int t = g->time_ms / slow;
    switch (o->type) {
    case PO_BEE: case PO_SPIKEBALL: {
        o->t += pg_abs(o->b) * dt / slow;                  /* 1000 units = 1 px travelled */
        int span = o->a * 4 ? o->a * 4 : 1;
        int pos = (o->t / 1000) % span;                    /* 0..4a, back and forth */
        int off = pos < o->a * 2 ? pos - o->a : 3 * o->a - pos;
        if (o->b < 0) off = -off;
        o->x = o->ox + off * PG_FP;
        if (o->type == PO_SPIKEBALL) o->y = o->oy + pg_sin(o->t / 2000) * 24 * PG_FP / 1024;
        break;
    }
    case PO_CROW: {
        /* t: 0 idle, 1 diving, 2 resting */
        if (o->flags == 0) {
            int dy = (o->y - g->py) / PG_FP;
            if (dy > 0 && dy < o->a + 60 && pg_abs((o->x - g->px) / PG_FP) < 260) {
                o->flags = 1;
                int dx = (g->px - o->x) / PG_FP, dyy = (g->py + 30 * PG_FP - o->y) / PG_FP;
                int len = pg_abs(dx) + pg_abs(dyy); if (!len) len = 1;
                o->vx = dx * o->b * PG_FP / len; o->vy = dyy * o->b * PG_FP / len;
                o->t = 0;
            }
        } else if (o->flags == 1) {
            o->x += o->vx * dt / 1000 / slow; o->y += o->vy * dt / 1000 / slow;
            o->t += dt;
            if (o->t > 1400) { o->flags = 2; o->t = 0; }
        } else {
            o->t += dt;
            if (o->t > 2500) { o->flags = 0; o->x = o->ox; o->y = o->oy; }
        }
        break;
    }
    default: break;
    }
    (void)t;
}

static inline void pg_interact(pg_game_t *g, pg_obj_t *o, int dt) {
    int t = g->time_ms / (g->slow_ms > 0 ? 2 : 1);
    int32_t x0, y0, x1, y1;
    switch (o->type) {
    case PO_COIN: case PO_STAR: case PO_BALLOON: case PO_SHIELD: case PO_MAGNET:
    case PO_FEATHER: case PO_HOURGLASS: case PO_PIN:
        if (o->type == PO_COIN && g->magnet_ms > 0 && pg_near(o->x, o->y, g->px, g->py + 20 * PG_FP, 120 * PG_FP)) {
            o->x += (g->px - o->x) / 6; o->y += (g->py + 20 * PG_FP - o->y) / 6;
        }
        if (pg_player_near(g, o->x, o->y, o->type == PO_COIN ? 10 : 14, 4)) pg_collect(g, o);
        break;
    case PO_FIREFLY:
        if (pg_player_near(g, o->x, o->y, 16, 8)) { o->alive = 0; g->light = 190; }
        break;
    case PO_BEE: case PO_SPIKEBALL:
        if (pg_player_near(g, o->x, o->y, o->type == PO_BEE ? 14 : 16, pg_hazard_pad)) pg_hurt(g);
        break;
    case PO_CROW:
        if (o->flags == 1 && pg_player_near(g, o->x, o->y, 14, pg_hazard_pad)) pg_hurt(g);
        break;
    case PO_THORN:
        x0 = o->x - o->a * PG_FP / 2; x1 = o->x + o->a * PG_FP / 2;
        y0 = o->y - o->b * PG_FP / 2; y1 = o->y + o->b * PG_FP / 2;
        if (pg_player_hits(g, x0, y0, x1, y1, pg_hazard_pad)) pg_hurt(g);
        break;
    case PO_ZAP:
        if (!pg_zap_on(o, t)) break;
        x0 = o->x - o->a * PG_FP / 2; x1 = o->x + o->a * PG_FP / 2;
        y0 = o->y - 6 * PG_FP; y1 = o->y + 6 * PG_FP;
        if (pg_player_hits(g, x0, y0, x1, y1, pg_hazard_pad)) { pg_hurt(g); g->events |= PE_ZAP; }
        break;
    case PO_THUNDER:
        if (!pg_thunder_strike(o, t)) break;
        x0 = o->x - 22 * PG_FP; x1 = o->x + 22 * PG_FP;
        y0 = o->y - 220 * PG_FP; y1 = o->y;
        if (pg_player_hits(g, x0, y0, x1, y1, pg_hazard_pad)) { pg_hurt(g); g->events |= PE_ZAP; }
        break;
    case PO_WIND:
        x0 = o->x - o->a * PG_FP / 2; x1 = o->x + o->a * PG_FP / 2;
        y0 = o->y - 80 * PG_FP; y1 = o->y + 80 * PG_FP;
        if (g->px > x0 && g->px < x1 && g->py > y0 && g->py < y1) g->vx += o->b * PG_FP * dt / 1000 / 4;
        break;
    case PO_RAIN:
        x0 = o->x - o->a * PG_FP / 2; x1 = o->x + o->a * PG_FP / 2;
        y0 = o->y - o->b * PG_FP / 2; y1 = o->y + o->b * PG_FP / 2;
        if (g->px > x0 && g->px < x1 && g->py > y0 && g->py < y1) g->vy -= 9 * PG_FP * dt / 100;
        break;
    case PO_VORTEX:
        if (pg_near(g->px, g->py, o->x, o->y, o->a * PG_FP)) {
            g->vx += (o->x > g->px ? 1 : -1) * o->b * PG_FP * dt / 1000 / 3;
            g->vy += (o->y > g->py ? 1 : -1) * o->b * PG_FP * dt / 1000 / 5;
        }
        break;
    case PO_BOUNCE:
        x0 = o->x - o->a * PG_FP / 2; x1 = o->x + o->a * PG_FP / 2;
        y0 = o->y - 10 * PG_FP; y1 = o->y + 10 * PG_FP;
        if (g->vy < 0 && pg_overlap_circle_rect(g->px, g->py, PG_PLAYER_R * PG_FP, x0, y0, x1, y1)) {
            g->vy = 260 * PG_FP / 100; g->events |= PE_BOUNCE;
        }
        break;
    case PO_GOAL:
        if (g->py >= o->y && pg_abs(g->px - o->x) < o->a * PG_FP / 2 + 40 * PG_FP) {
            if (g->level->kind == PG_KIND_COLLECT && g->coins < g->level->coins_needed) break;
            g->state = PG_WON; g->events |= PE_WIN;
        }
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ bosses */
static inline void pg_boss_spawn_drop(pg_game_t *g) {
    /* Periodic supplies so a boss fight never runs out of balloons. */
    int t = g->time_ms / 1000;
    if (t > 0 && t % 9 == 0 && g->time_ms % 1000 < PG_STEP_MS) {
        int kind = (t / 9) % 3 == 0 ? PO_BALLOON : (t / 9) % 3 == 1 ? PO_PIN : PO_SHIELD;
        int x = (t / 9) % 2 ? 110 : 356;
        for (int i = 0; i < g->obj_count; i++)
            if (!g->obj[i].alive) { g->obj[i] = (pg_obj_t){0}; g->obj_count = i; break; }
        pg_add(g, kind, x, 120, 0, 0);
    }
}

static inline void pg_boss_step(pg_game_t *g, int dt) {
    int slow = g->slow_ms > 0 ? 2 : 1;
    const pg_level_t *l = g->level;
    if (g->boss_hurt_ms > 0) g->boss_hurt_ms -= dt;
    int speed_up = (l->boss == PB_KING && g->boss_hp <= 2) ? 3 : 2;   /* halves: 1.0x, 1.5x */
    /* cruising */
    if (g->boss_attack == 0) {
        g->bx += g->bvx * dt * speed_up / 2 / slow;
        if (g->bx < 110 * PG_FP) { g->bx = 110 * PG_FP; g->bvx = -g->bvx; }
        if (g->bx > 356 * PG_FP) { g->bx = 356 * PG_FP; g->bvx = -g->bvx; }
        int base = (l->boss == PB_OWL) ? 330 : 360;
        g->by = (base + pg_sin(g->time_ms / 6) * 14 / 1024) * PG_FP;
    }
    g->boss_timer -= dt / slow;
    if (g->boss_timer > 0) goto contact;
    int pattern = l->boss;
    /* The King runs whole attacks in turn: stingers, then a storm strike, then a dive. */
    if (pattern == PB_KING) {
        if (g->boss_attack == 0) g->boss_phase++;
        pattern = 1 + (g->boss_phase % 3);
    }
    switch (pattern) {
    case PB_QUEEN:
        /* drop two stingers straight down */
        pg_add(g, PO_SPIKEBALL, (int)(g->bx / PG_FP) - 40, (int)(g->by / PG_FP) - 40, 0, 0);
        g->obj[g->obj_count - 1].flags = 9;     /* falling projectile */
        pg_add(g, PO_SPIKEBALL, (int)(g->bx / PG_FP) + 40, (int)(g->by / PG_FP) - 40, 0, 0);
        g->obj[g->obj_count - 1].flags = 9;
        g->boss_timer = 3000;
        break;
    case PB_STORMCLOUD:
        /* warn then strike under the cloud, then sink for a free hit */
        if (g->boss_attack == 0) { g->boss_attack = 1; g->boss_timer = 1000; }
        else if (g->boss_attack == 1) { g->boss_attack = 2; g->boss_timer = 500; }
        else if (g->boss_attack == 2) { g->boss_attack = 3; g->boss_timer = 1600; }
        else { g->boss_attack = 0; g->boss_timer = 1800; }
        break;
    case PB_OWL:
        /* blink, dive across, then rest low */
        if (g->boss_attack == 0) { g->boss_attack = 1; g->boss_timer = 900; }
        else if (g->boss_attack == 1) {
            g->boss_attack = 2; g->boss_timer = 1100;
            g->bvx = (g->bx < 233 * PG_FP) ? 280 * PG_FP / 100 : -280 * PG_FP / 100;
        } else if (g->boss_attack == 2) { g->boss_attack = 3; g->boss_timer = 1800; }
        else { g->boss_attack = 0; g->boss_timer = 2200; g->bvx = (g->bvx > 0 ? 70 : -70) * PG_FP / 100; }
        break;
    default: break;
    }
contact:;
    /* The King borrows each earlier boss's attack; kp is the pattern in use. */
    int kp = l->boss == PB_KING ? 1 + (g->boss_phase % 3) : l->boss;
    if (kp == PB_STORMCLOUD) {
        if (g->boss_attack == 3) g->by -= 60 * PG_FP * dt / 1000;   /* sink after striking */
        if (g->boss_attack == 0 && g->by < 300 * PG_FP) g->by += 120 * PG_FP * dt / 1000;
    }
    if (kp == PB_OWL) {
        if (g->boss_attack == 2) { g->bx += g->bvx * dt / slow; g->by -= 150 * PG_FP * dt / 1000 / slow; }
        /* Recover above a hovering player before exposing the weak point. */
        if (g->boss_attack == 3 && g->by < 330 * PG_FP)
            g->by = pg_clamp(g->by + 220 * PG_FP * dt / 1000, 150 * PG_FP, 330 * PG_FP);
        if (g->boss_attack == 0 && g->by < 330 * PG_FP) g->by += 150 * PG_FP * dt / 1000;
        g->bx = pg_clamp(g->bx, 80 * PG_FP, 386 * PG_FP);
        if (l->dark) g->light = (g->boss_attack == 1 || g->boss_attack == 2) ? 120 : 170;
    }
    /* lightning column under the cloud */
    if (kp == PB_STORMCLOUD && g->boss_attack == 2) {
        if (pg_player_hits(g, g->bx - 30 * PG_FP, 0, g->bx + 30 * PG_FP, g->by, pg_hazard_pad)) { pg_hurt(g); g->events |= PE_ZAP; }
    }
    /* body contact: rising into the weak spot hurts the boss; a diving owl hurts the player */
    if (g->boss_hurt_ms <= 0 && g->state == PG_PLAYING) {
        int r = 40;
        if (pg_near(g->px, g->py + 40 * PG_FP, g->bx, g->by - 30 * PG_FP, (r + PG_BALLOON_R) * PG_FP) ||
            pg_near(g->px, g->py, g->bx, g->by - 30 * PG_FP, (r + PG_PLAYER_R) * PG_FP)) {
            int open = kp == PB_QUEEN || (kp == PB_STORMCLOUD && g->boss_attack == 3) ||
                       (kp == PB_OWL && g->boss_attack == 3);
            if (g->vy > 0 && g->py < g->by && open) {
                g->boss_hp--; g->boss_hurt_ms = 1500; g->events |= PE_BOSS_HIT;
                g->vy = -200 * PG_FP / 100; g->invuln_ms = 600;
                if (g->boss_hp <= 0) { g->state = PG_WON; g->events |= PE_WIN; }
            } else if (kp == PB_OWL && g->boss_attack == 2) {
                pg_hurt(g);
            }
        }
    }
    pg_boss_spawn_drop(g);
}

/* ------------------------------------------------------------------ main step */
static inline void pg_step(pg_game_t *g, const pg_input_t *in) {
    const int dt = PG_STEP_MS;
    if (g->state != PG_PLAYING) return;
    const pg_level_t *l = g->level;
    g->time_ms += dt;
    if (g->invuln_ms > 0) g->invuln_ms -= dt;
    if (g->shield_ms > 0) g->shield_ms -= dt;
    if (g->magnet_ms > 0) g->magnet_ms -= dt;
    if (g->slow_ms > 0) g->slow_ms -= dt;
    if (g->combo_ms > 0) g->combo_ms -= dt;
    if (g->dash_ms > 0) g->dash_ms -= dt;
    if (in->use_item && g->has_feather) { g->has_feather = 0; g->dash_ms = 600; g->events |= PE_DASH; }

    /* ---- vertical: lift vs gravity (px/s * 16) ---- */
    static const int weight[] = {0, 75, 88, 100, 108, 115};
    int lift = in->blow * 300 / 100 * weight[g->balloons > 0 ? g->balloons : 1] / 100;  /* px/s at full */
    int target_vy = in->blow > 0 ? pg_clamp(lift - 70, 0, 440) : 0;                                     /* px/s */
    if (g->dash_ms > 0) target_vy = 420;
    if (!in->blow && g->dash_ms <= 0) g->vy = 0;
    g->vy += ((target_vy * PG_FP) - g->vy) * dt / 180;
    g->vy = pg_clamp(g->vy, -150 * PG_FP, 440 * PG_FP);
    /* ---- horizontal: tilt ---- */
    int target_vx = in->tilt * 220 / 100;
    g->vx += ((target_vx * PG_FP) - g->vx) * dt / 160;
    g->vx = pg_clamp(g->vx, -300 * PG_FP, 300 * PG_FP);

    /* objects and field forces */
    for (int i = 0; i < g->obj_count; i++) {
        pg_obj_t *o = &g->obj[i];
        if (!o->alive) continue;
        if (o->flags == 9) {   /* falling boss projectile */
            o->y -= 190 * PG_FP * dt / 1000 / (g->slow_ms > 0 ? 2 : 1);
            if (o->y < -40 * PG_FP) { o->alive = 0; continue; }
            if (pg_player_near(g, o->x, o->y, 12, pg_hazard_pad)) { pg_hurt(g); o->alive = 0; }
            continue;
        }
        pg_object_step(g, o, dt);
        pg_interact(g, o, dt);
        if (g->state != PG_PLAYING) return;
    }

    /* Weather can reduce lift, but never makes a hovering player sink. */
    if (g->vy < 0) g->vy = 0;
    g->px += g->vx * dt / 1000;
    g->py += g->vy * dt / 1000;
    g->px = pg_clamp(g->px, PG_FIELD_MIN * PG_FP, PG_FIELD_MAX * PG_FP);
    if (g->px == PG_FIELD_MIN * PG_FP || g->px == PG_FIELD_MAX * PG_FP) g->vx = 0;
    int ceiling = l->boss ? 300 : l->height + 120;
    if (g->py < 20 * PG_FP) { g->py = 20 * PG_FP; if (g->vy < 0) g->vy = 0; }
    if (g->level_index >= 0 && g->py > ceiling * PG_FP) { g->py = ceiling * PG_FP; if (g->vy > 0 && !l->boss) g->vy = 0; }

    if (l->boss) pg_boss_step(g, dt);
    if (g->state != PG_PLAYING) return;

    /* escape wall: rises steadily; touching it costs a balloon and pushes the player up */
    if (l->kind == PG_KIND_ESCAPE) {
        g->wall_y += l->scroll * PG_FP * dt / 1000 / (g->slow_ms > 0 ? 2 : 1);
        if (g->py - 20 * PG_FP < g->wall_y) {
            pg_hurt(g);
            if (g->state != PG_PLAYING) return;
            g->py = g->wall_y + 120 * PG_FP; g->vy = 0;
        }
    }
    if (g->level->dark && !l->boss && g->light > 90 && g->time_ms % 1000 < dt) g->light -= 6;

    if (g->time_limit_ms && g->time_ms >= g->time_limit_ms) { g->state = PG_LOST; g->events |= PE_LOSE; }

    /* camera: player at PG_PLAYER_SCREEN_Y, never below the floor */
    int cam = (int)(g->py / PG_FP) - (PG_SCREEN - PG_PLAYER_SCREEN_Y);
    if (l->boss) cam = 0;
    g->camera = cam < 0 ? 0 : cam;
}

/* Stars earned on a finished level: clear + all star coins + no balloon lost. */
static inline int pg_result_stars(const pg_game_t *g) {
    if (g->state != PG_WON) return 0;
    int s = 1;
    if (g->stars >= pg_total_stars(g->level)) s++;
    if (g->balloons >= g->start_balloons) s++;
    return s;
}
#endif
