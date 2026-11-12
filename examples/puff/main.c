/* Puff 2: microphone balloon adventure for Magie S31. Blow to rise, tilt to
 * steer. 16 levels in 4 worlds plus an endless climb. Game logic lives in
 * puff_game.h and is shared with the native level solver. */
#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "assets_generated.h"
#include "puff_input.h"
#include "puff_game.h"

/* ------------------------------------------------------------------ state */
enum { S_TITLE, S_MAP, S_CARD, S_PLAY, S_RESULT, S_CAL, S_SETTINGS };
enum { KV_VERSION, KV_STARS0, KV_STARS1, KV_STARS2, KV_STARS3, KV_UNLOCK, KV_BEST_H, KV_BEST_S,
       KV_SETTINGS, KV_CAL_FLOOR, KV_CAL_REF, KV_COINS };
#define SAVE_VERSION 2
#define ENDLESS_LEVEL (-1)

static pg_game_t game;
static int screen = S_TITLE, map_world, card_level, endless, result_stars, new_record;
static int stars[PG_LEVEL_COUNT], unlocked, best_height, best_score, total_coins;
static int fixed_mode, tilt_sens = 1, touch_blow, mic_running, mic_error;
static int frame_ready = 1;
static int touch_down, blow_level, tilt_level, feather_tap;
static int32_t tilt_raw, tilt_zero;
static int countdown_ms, result_ms, hint_ms, hint_id, anim, shake_ms, pop_ms, pop_x, pop_y;
static int cal_step, cal_ms, step_debt, pending_level = -2; /* start on the next tick */
static uint32_t last_ms, mic_last_rx;
static ta_calib_t calibration = {PUFF_CAL_FLOOR_Q8, PUFF_CAL_REF_Q8, 1};
static ta_calib_window_t cal_window;
static puff_blow_t blow_detector;
/* endless mode: generated chunks */
static pg_spawn_t endless_spawns[PG_MAX_OBJECTS];
static pg_level_t endless_level;
static int endless_top;

/* ------------------------------------------------------------------ drawing */
typedef uint32_t __attribute__((may_alias)) command_word_t;
static union { uint32_t aligned; uint8_t bytes[60000]; } cmd;
static uint32_t used;
static void emit(const void *src, uint32_t n) {
    const command_word_t *p = src;
    if (used + n > sizeof(cmd.bytes)) return;
    command_word_t *out = (command_word_t *)(cmd.bytes + used);
    for (uint32_t i = 0; i < n / 4; i++) out[i] = p[i];
    used += n;
}
static uint32_t rgb565(uint32_t c) { return ((c >> 8) & 0xf800u) | ((c >> 5) & 0x07e0u) | ((c >> 3) & 0x001fu); }
static void rect(int x, int y, int w, int h, uint32_t c, int a) {
    if (w <= 0 || h <= 0 || a <= 0) return;
    if (w > 4000) w = 4000; if (h > 4000) h = 4000;
    tinyrt_gfx_rect_t r = {TINYRT_GFX_RECT, sizeof r, x, y, w, h, rgb565(c), (uint32_t)a};
    emit(&r, sizeof r);
}
static void sprite_scaled(const sprite_t *s, int x, int y, int w, int h, int flags) {
    if (w <= 0 || h <= 0 || x > 466 || y > 466 || x + w < 0 || y + h < 0) return;
    tinyrt_gfx_sprite_t r = {TINYRT_GFX_SPRITE, sizeof r, x, y, w, h, 0, (uint32_t)s->u, (uint32_t)s->v,
                             (uint32_t)s->w, (uint32_t)s->h, TINYRT_GFX_TRANSPARENT | (uint32_t)flags, 0, 0};
    emit(&r, sizeof r);
}
static void spr(const sprite_t *s, int cx, int cy) { sprite_scaled(s, cx - s->w / 2, cy - s->h / 2, s->w, s->h, 0); }
static void spr2(const sprite_t *s, int cx, int cy) { sprite_scaled(s, cx - s->w, cy - s->h, s->w * 2, s->h * 2, 0); }
static void spr_flip(const sprite_t *s, int cx, int cy, int flip) {
    sprite_scaled(s, cx - s->w / 2, cy - s->h / 2, s->w, s->h, flip ? TINYRT_GFX_FLIP_X : 0);
}
static void solid(const sprite_t *s, int cx, int cy, uint32_t color) {
    tinyrt_gfx_sprite_t r = {TINYRT_GFX_SOLID_SPRITE, sizeof r, cx - s->w / 2, cy - s->h / 2, s->w, s->h, 0,
                             (uint32_t)s->u, (uint32_t)s->v, (uint32_t)s->w, (uint32_t)s->h,
                             TINYRT_GFX_TRANSPARENT, 0, rgb565(color)};
    emit(&r, sizeof r);
}
static void round_rect(int x, int y, int w, int h, int radius, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;
    struct { uint16_t op, size; int32_t x, y, w, h, r; uint32_t color; } r = {TINYRT_GFX_ROUND_RECT, 28, x, y, w, h, radius, rgb565(c)};
    emit(&r, sizeof r);
}
static void arc_raw(int cx, int cy, int radius, int thick, int a0, int a1, uint32_t c) {
    if (radius <= 0 || thick <= 0 || a1 <= a0) return;
    if (thick > radius) thick = radius;
    struct { uint16_t op, size; int32_t cx, cy, r, t, a0, a1; uint32_t color; } r = {TINYRT_GFX_ARC, 32, cx, cy, radius, thick, a0, a1, rgb565(c)};
    emit(&r, sizeof r);
}
/* Angles in degrees, 0 = right, clockwise; ranges may wrap past 360. */
static void arc(int cx, int cy, int radius, int thick, int a0, int a1, uint32_t c) {
    while (a0 >= 360) { a0 -= 360; a1 -= 360; }
    while (a0 < 0) { a0 += 360; a1 += 360; }
    if (a1 - a0 >= 360) { arc_raw(cx, cy, radius, thick, 0, 360, c); return; }
    if (a1 <= 360) { arc_raw(cx, cy, radius, thick, a0, a1, c); return; }
    arc_raw(cx, cy, radius, thick, a0, 360, c);
    arc_raw(cx, cy, radius, thick, 0, a1 - 360, c);
}
static void number(int value, int cx, int cy) {
    static const sprite_t *const dg[] = {&d0, &d1, &d2, &d3, &d4, &d5, &d6, &d7, &d8, &d9};
    int v[7], n = 0, w = 0;
    if (value < 0) value = 0;
    do { v[n] = value % 10; w += dg[v[n]]->w - 2; n++; value /= 10; } while (value && n < 7);
    int x = cx - w / 2;
    while (n) { const sprite_t *s = dg[v[--n]]; sprite_scaled(s, x, cy - s->h / 2, s->w, s->h, 0); x += s->w - 2; }
}

/* Text: glyphs from the generated 20 px atlas (texture 1), tinted per call.
 * Sizes other than 20 px scale the glyph cells. Strings are UTF-8. */
static int utf8_next(const char **p) {
    const unsigned char *q = (const unsigned char *)*p;
    int c = q[0], n = 1;
    if (c >= 0xf0) { c = ((c & 7) << 18) | ((q[1] & 63) << 12) | ((q[2] & 63) << 6) | (q[3] & 63); n = 4; }
    else if (c >= 0xe0) { c = ((c & 15) << 12) | ((q[1] & 63) << 6) | (q[2] & 63); n = 3; }
    else if (c >= 0xc0) { c = ((c & 31) << 6) | (q[1] & 63); n = 2; }
    *p += n;
    return c;
}
static const glyph_t *glyph_find(int code) {
    int lo = 0, hi = GLYPH_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((int)glyphs[mid].code == code) return &glyphs[mid];
        if ((int)glyphs[mid].code < code) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
static int text_width(const char *s, int px) {
    int w = 0;
    while (*s) {
        int c = utf8_next(&s);
        const glyph_t *g = glyph_find(c);
        w += g ? g->adv * px / 20 : (c == ' ' ? px / 3 : px / 2);
    }
    return w;
}
static void glyph_run(const char *s, int x, int y, int px, uint32_t color) {
    while (*s) {
        int c = utf8_next(&s);
        const glyph_t *g = glyph_find(c);
        if (!g) { x += c == ' ' ? px / 3 : px / 2; continue; }
        if (g->w * px / 20 <= 0 || g->h * px / 20 <= 0) { x += g->adv * px / 20; continue; }
        tinyrt_gfx_sprite_t r = {TINYRT_GFX_SOLID_SPRITE, sizeof r, x, y, g->w * px / 20, g->h * px / 20, 1,
                                 g->u, g->v, g->w, g->h, TINYRT_GFX_TRANSPARENT, 1, rgb565(color)};
        emit(&r, sizeof r);
        x += g->adv * px / 20;
    }
}
static void text(int cx, int cy, int w, int px, uint32_t color, const char *s) {
    (void)w;
    int tw = text_width(s, px), h = 27 * px / 20;
    if (px >= 24 && color != 0x2e2440) glyph_run(s, cx - tw / 2 + 2, cy - h / 2 + 2, px, 0x2e2440);   /* drop shadow */
    glyph_run(s, cx - tw / 2, cy - h / 2, px, color);
}
static void button(int cx, int cy, int w, uint32_t color, const char *s) {
    round_rect(cx - w / 2, cy - 22, w, 44, 22, 0x2e2440);
    round_rect(cx - w / 2 + 2, cy - 22, w - 4, 40, 20, color);
    text(cx, cy - 2, w, 18, 0xffffff, s);
}
static int hit(int x, int y, int cx, int cy, int w, int h) { return x >= cx - w / 2 && x <= cx + w / 2 && y >= cy - h / 2 && y <= cy + h / 2; }

/* ------------------------------------------------------------------ audio / haptics */
static void sound(uint32_t off, uint32_t bytes) { (void)audio_play(off, bytes, 16000); }
static void feedback(int ev) {
    if (ev & PE_WIN) { sound(SND_WIN_OFFSET, SND_WIN_BYTES); (void)haptic_pulse(60, 300); }
    else if (ev & PE_LOSE) { sound(SND_LOSE_OFFSET, SND_LOSE_BYTES); (void)haptic_pulse(90, 400); }
    else if (ev & PE_BOSS_HIT) { sound(SND_BOSS_OFFSET, SND_BOSS_BYTES); (void)haptic_pulse(80, 120); }
    else if (ev & PE_HIT) { sound(SND_HIT_OFFSET, SND_HIT_BYTES); (void)haptic_pulse(100, 200); }
    else if (ev & PE_STAR) sound(SND_STAR_OFFSET, SND_STAR_BYTES);
    else if (ev & PE_DASH) sound(SND_DASH_OFFSET, SND_DASH_BYTES);
    else if (ev & PE_ITEM) sound(SND_ITEM_OFFSET, SND_ITEM_BYTES);
    else if (ev & PE_BOUNCE) sound(SND_BOUNCE_OFFSET, SND_BOUNCE_BYTES);
    else if (ev & PE_COIN) sound(SND_COIN_OFFSET, SND_COIN_BYTES);
}

/* ------------------------------------------------------------------ save data */
static void load(void) {
    if (kv_get(KV_VERSION, 0) != SAVE_VERSION) {
        for (int k = 1; k < 12; k++) (void)kv_set((uint32_t)k, 0);
        (void)kv_set(KV_VERSION, SAVE_VERSION);
        (void)kv_set(KV_SETTINGS, 1 << 2);
    }
    for (int w = 0; w < 4; w++) {
        int packed = kv_get((uint32_t)(KV_STARS0 + w), 0);
        for (int i = 0; i < 4; i++) stars[w * 4 + i] = (packed >> (i * 2)) & 3;
    }
    unlocked = kv_get(KV_UNLOCK, 0);
    best_height = kv_get(KV_BEST_H, 0); best_score = kv_get(KV_BEST_S, 0); total_coins = kv_get(KV_COINS, 0);
    int st = kv_get(KV_SETTINGS, 1 << 2);
    fixed_mode = st & 1; touch_blow = (st >> 1) & 1; tilt_sens = (st >> 2) & 3; if (tilt_sens > 2) tilt_sens = 1;
    int f = kv_get(KV_CAL_FLOOR, 0), r = kv_get(KV_CAL_REF, 0);
    if (f && r) { calibration.floor_db_q8 = (int16_t)f; calibration.ref_db_q8 = (int16_t)r; calibration.ok = 1; }
}
static void save_settings(void) { (void)kv_set(KV_SETTINGS, fixed_mode | touch_blow << 1 | tilt_sens << 2); }
static void save_level(int level, int s) {
    if (s > stars[level]) stars[level] = s;
    int w = level / 4, packed = 0;
    for (int i = 0; i < 4; i++) packed |= stars[w * 4 + i] << (i * 2);
    (void)kv_set((uint32_t)(KV_STARS0 + w), packed);
    if (level + 1 > unlocked && level + 1 < PG_LEVEL_COUNT) { unlocked = level + 1; (void)kv_set(KV_UNLOCK, unlocked); }
    if (level + 1 >= PG_LEVEL_COUNT && unlocked < PG_LEVEL_COUNT) { unlocked = PG_LEVEL_COUNT; (void)kv_set(KV_UNLOCK, unlocked); }
}

/* ------------------------------------------------------------------ microphone */
static void mic_stop(void) { if (mic_running) (void)mic_close(); mic_running = 0; blow_level = 0; }
static int mic_begin(void) {
    /* Opens the microphone for features; returns 0 on success, sets mic_error otherwise. */
    mic_stop();
    if (mic_open(16000, TINYRT_MIC_FEATURES) != 0) { mic_error = 1; return -1; }
    mic_running = 1;
    if (mic_gain(12) != 12) { mic_stop(); mic_error = 1; return -1; }
    mic_last_rx = now_ms(); mic_error = 0;
    return 0;
}
static int mic_start(void) {
    if (touch_blow) { mic_stop(); return -1; }
    if (mic_begin()) return -1;
    puff_blow_init(&blow_detector, &calibration);
    return 0;
}
static void mic_pump(void (*on_frame)(const tinyrt_mic_frame_t *)) {
    if (!mic_running) return;
    tinyrt_mic_frame_t frames[16];
    int bytes = 0;
    while (mic_running && (bytes = mic_features(frames, sizeof frames)) > 0) {
        mic_last_rx = now_ms();
        for (int i = 0; i < bytes / (int)sizeof frames[0] && mic_running; i++) on_frame(&frames[i]);
    }
    if (!mic_running) return;          /* a frame handler finished and closed the microphone */
    uint32_t stale = now_ms() - mic_last_rx;
    if (bytes < 0 || stale > 600) { mic_stop(); mic_error = 1; }
    else if (stale > 120) blow_level = 0;
}
static void on_play_frame(const tinyrt_mic_frame_t *f) {
    ta_output_t out = puff_blow_push(&blow_detector, f);
    if (!(out.valid && out.active)) { blow_level = 0; return; }
    blow_level = fixed_mode ? 80 : 30 + out.strength_q15 * 70 / 32767;
}
static void on_cal_frame(const tinyrt_mic_frame_t *f) {
    if (ta_calib_push(&cal_window, f, cal_step == 1, &calibration)) {
        if (cal_step == 0) { cal_step = 1; cal_window = (ta_calib_window_t){0}; }
        else {
            cal_step = 2; cal_ms = 0; calibration.ok = 1;
            (void)kv_set(KV_CAL_FLOOR, calibration.floor_db_q8); (void)kv_set(KV_CAL_REF, calibration.ref_db_q8);
            mic_stop();
        }
    }
}

/* ------------------------------------------------------------------ endless generator */
static void endless_chunk_add(int base) {
    /* Each chunk is 360 px tall and built from templates that the solver proved passable. */
    static const pg_spawn_t T[][4] = {
        {{PO_COIN,233,60,0,0},{PO_BEE,233,180,110,90},{PO_COIN,140,260,0,0},{PO_COIN,326,300,0,0}},
        {{PO_THORN,120,120,140,24},{PO_COIN,330,120,0,0},{PO_COIN,330,200,0,0},{PO_STAR,233,300,0,0}},
        {{PO_WIND,233,140,330,80},{PO_COIN,300,160,0,0},{PO_BOUNCE,150,280,110,0},{PO_COIN,150,330,0,0}},
        {{PO_SPIKEBALL,233,160,130,110},{PO_COIN,233,80,0,0},{PO_SHIELD,120,280,0,0},{PO_COIN,233,320,0,0}},
        {{PO_THUNDER,140,200,2400,0},{PO_THUNDER,326,200,2400,1200},{PO_COIN,233,200,0,0},{PO_BALLOON,233,320,0,0}},
        {{PO_CROW,380,240,170,220},{PO_COIN,120,140,0,0},{PO_COIN,120,200,0,0},{PO_MAGNET,233,320,0,0}},
        {{PO_ZAP,233,180,240,20},{PO_COIN,233,100,0,0},{PO_FEATHER,233,260,0,0},{PO_COIN,233,330,0,0}},
        {{PO_RAIN,140,180,150,240},{PO_COIN,330,180,0,0},{PO_HOURGLASS,330,280,0,0},{PO_COIN,233,330,0,0}},
    };
    int depth = base / 360;
    int pool = depth < 3 ? 4 : depth < 6 ? 6 : 8;
    int pick = (int)(pg_rand(&game) % (unsigned)pool);
    for (int i = 0; i < 4; i++) {
        pg_spawn_t s = T[pick][i];
        int speedup = depth > 8 ? 130 : 100 + depth * 4;
        if (s.type == PO_BEE || s.type == PO_SPIKEBALL) s.b = (int16_t)(s.b * speedup / 100);
        /* recycle dead slots far below the camera */
        int slot = -1;
        for (int k = 0; k < game.obj_count; k++)
            if (!game.obj[k].alive || game.obj[k].y < (game.camera - 300) * PG_FP) { slot = k; break; }
        if (slot < 0) { if (game.obj_count >= PG_MAX_OBJECTS) continue; slot = game.obj_count++; }
        pg_obj_t *o = &game.obj[slot];
        *o = (pg_obj_t){0};
        o->type = s.type; o->alive = 1; o->a = s.a; o->b = s.b;
        o->x = o->ox = s.x * PG_FP; o->y = o->oy = (base + s.y) * PG_FP;
    }
}
static void endless_start(void) {
    endless_level = (pg_level_t){"无尽", 0, PG_KIND_CLIMB, 30000, 0, 0, 0, PB_NONE, 0, endless_spawns, 0};
    pg_start(&game, 0, now_ms());
    game.level = &endless_level; game.level_index = ENDLESS_LEVEL; game.obj_count = 0;
    endless_top = 0;
    for (int i = 0; i < 3; i++) { endless_chunk_add(200 + endless_top); endless_top += 360; }
}

/* ------------------------------------------------------------------ flow */
static void hint(int id) { hint_id = id; hint_ms = 3200; }
static void start_level(int level) {
    endless = level == ENDLESS_LEVEL;
    if (endless) endless_start(); else pg_start(&game, level, 1234u + (unsigned)level);
    card_level = level;
    countdown_ms = 2000; shake_ms = pop_ms = 0; feather_tap = 0; step_debt = 0;
    screen = S_PLAY;
    (void)mic_start();
    if (!endless) {
        static const signed char hints[PG_LEVEL_COUNT] = {1, 7, 8, 6, 4, 0, 9, 6, 5, 0, 0, 6, 0, 9, 8, 6};
        if (hints[level]) hint(hints[level]);
    }
}
static void finish(void) {
    mic_stop();
    result_ms = 0; new_record = 0;
    total_coins += game.coins; (void)kv_set(KV_COINS, total_coins);
    if (endless) {
        int h = (int)(game.py / PG_FP) / 10;
        game.score += h * 5;                         /* height counts toward the endless score */
        if (h > best_height) { best_height = h; (void)kv_set(KV_BEST_H, h); new_record = 1; }
        if (game.score > best_score) { best_score = game.score; (void)kv_set(KV_BEST_S, game.score); new_record = 1; }
        result_stars = 0;
    } else {
        result_stars = pg_result_stars(&game);
        if (game.state == PG_WON) save_level(card_level, result_stars);
    }
    screen = S_RESULT;
}

/* ------------------------------------------------------------------ simulation tick */
static void play_tick(void) {
    if (countdown_ms > 0) { countdown_ms -= PG_STEP_MS; return; }
    pg_input_t in = {0};
    in.blow = (uint8_t)(touch_blow || mic_error ? (touch_down ? (fixed_mode ? 80 : 100) : 0) : blow_level);
    in.tilt = (int8_t)tilt_level;
    in.use_item = (uint8_t)feather_tap; feather_tap = 0;
    pg_step(&game, &in);
    if (game.events) {
        feedback(game.events);
        if (game.events & PE_HIT) { shake_ms = 250; pop_ms = 400; pop_x = (int)(game.px / PG_FP); pop_y = PG_PLAYER_SCREEN_Y - 40; }
        game.events = 0;
    }
    if (endless) {
        while (endless_top < game.camera + 900) { endless_chunk_add(200 + endless_top); endless_top += 360; }
        game.level = &endless_level;
    }
    if (game.state != PG_PLAYING) finish();
}

static void tilt_update(void) {
    static const int gain[] = {70, 100, 140};
    int v = (int)(tilt_raw - tilt_zero);
    if (v > -60 && v < 60) v = 0;                  /* dead zone, mg */
    v = v * gain[tilt_sens] / 450;                 /* about 25 degrees for full speed */
    tilt_level = v > 100 ? 100 : v < -100 ? -100 : v;
}

/* ------------------------------------------------------------------ input */
static void press(int x, int y) {
    switch (screen) {
    case S_TITLE:
        if (hit(x, y, 233, 262, 180, 48)) screen = S_MAP;
        else if (hit(x, y, 233, 318, 180, 48)) { if (unlocked >= 4) { card_level = ENDLESS_LEVEL; screen = S_CARD; } else hint(10); }
        else if (hit(x, y, 233, 374, 180, 48)) screen = S_SETTINGS;
        break;
    case S_MAP:
        if (hit(x, y, 80, 233, 60, 80)) { if (map_world > 0) map_world--; }
        else if (hit(x, y, 386, 233, 60, 80)) { if (map_world < 3) map_world++; }
        else if (hit(x, y, 233, 405, 140, 44)) screen = S_TITLE;
        else for (int i = 0; i < 4; i++) {
            int lx = 233 + (i % 2 ? 62 : -62), ly = 205 + (i / 2) * 104;
            if (hit(x, y, lx, ly, 104, 90)) {
                int lv = map_world * 4 + i;
                if (lv <= unlocked) { card_level = lv; screen = S_CARD; } else hint(11);
            }
        }
        break;
    case S_CARD:
        if (hit(x, y, 233, 330, 180, 48)) pending_level = card_level;
        else if (hit(x, y, 233, 392, 140, 44)) screen = card_level == ENDLESS_LEVEL ? S_TITLE : S_MAP;
        break;
    case S_PLAY:
        touch_down = 1;
        if (game.has_feather && countdown_ms <= 0) feather_tap = 1;
        break;
    case S_RESULT:
        if (result_ms < 700) break;
        if (!endless && game.state == PG_WON && card_level + 1 < PG_LEVEL_COUNT && hit(x, y, 233, 300, 180, 48)) {
            card_level++; map_world = card_level / 4; screen = S_CARD;
        } else if (hit(x, y, 233, game.state == PG_WON && !endless && card_level + 1 < PG_LEVEL_COUNT ? 356 : 300, 180, 48)) pending_level = card_level;
        else if (hit(x, y, 233, 410, 140, 40)) screen = endless ? S_TITLE : S_MAP;
        break;
    case S_CAL:
        if (cal_step >= 2 || mic_error) { mic_stop(); screen = S_SETTINGS; }
        break;
    case S_SETTINGS:
        if (hit(x, y, 233, 150, 220, 44)) {
            screen = S_CAL; cal_step = 0; cal_ms = 0; cal_window = (ta_calib_window_t){0};
            mic_error = 0;
            (void)mic_begin();
        } else if (hit(x, y, 233, 206, 220, 44)) { fixed_mode ^= 1; save_settings(); }
        else if (hit(x, y, 233, 262, 220, 44)) { tilt_sens = (tilt_sens + 1) % 3; save_settings(); }
        else if (hit(x, y, 233, 318, 220, 44)) { touch_blow ^= 1; save_settings(); }
        else if (hit(x, y, 233, 392, 140, 44)) screen = S_TITLE;
        break;
    }
}

/* ------------------------------------------------------------------ render helpers */
static const uint32_t sky_top[] = {0x8fd3ff, 0xb9e2ff, 0x1c2350, 0x3b3552};
static const uint32_t sky_bottom[] = {0xd8f4ff, 0xeef8ff, 0x3a3f7a, 0x6a6585};
static int world_of_current(void) { return endless ? (game.camera / 1800) % 4 : game.level->world - 1; }

static void background(int world, int cam) {
    uint32_t a = sky_top[world], b = sky_bottom[world];
    for (int i = 0; i < 12; i++) {
        int t = i * 255 / 11;
        uint32_t r = ((a >> 16 & 255) * (255 - t) + (b >> 16 & 255) * t) / 255;
        uint32_t g = ((a >> 8 & 255) * (255 - t) + (b >> 8 & 255) * t) / 255;
        uint32_t bl = ((a & 255) * (255 - t) + (b & 255) * t) / 255;
        rect(0, i * 39, 466, 40, r << 16 | g << 8 | bl, 255);
    }
    if (world == 2) {
        spr(&moon, 340, 90);
        for (int i = 0; i < 14; i++) spr(&twinkle, (i * 151 + 40) % 466, (i * 97 + cam / 6) % 466);
    } else if (world < 2) spr(&sun, 340, 90);
    for (int i = 0; i < 6; i++) {
        int y = (i * 113 + cam / 3) % 600 - 70;
        int x = (i * 173 + 30) % 520 - 40;
        if (world == 3) solid(&cloud, x, y, 0x55506a);
        else if (world == 2) solid(&cloud, x, y, 0x2d3468);
        else spr(&cloud, x, y);
    }
}

static int sy(int wy) { return 466 - (wy - game.camera) - 10; }   /* world y (up) -> screen y (down) */

static const sprite_t *pickup_icon(int t) {
    switch (t) {
    case PO_BALLOON: return &i_balloon; case PO_SHIELD: return &i_shield; case PO_MAGNET: return &i_magnet;
    case PO_FEATHER: return &i_feather; case PO_HOURGLASS: return &i_hourglass; case PO_PIN: return &i_pin;
    default: return &i_balloon;
    }
}

static void draw_object(const pg_obj_t *o) {
    int x = (int)(o->x / PG_FP), y = sy((int)(o->y / PG_FP));
    int t = game.time_ms / (game.slow_ms > 0 ? 2 : 1);
    if (y < -120 || y > 600) return;
    switch (o->type) {
    case PO_COIN: { static const sprite_t *const c[] = {&coin0, &coin1, &coin2, &coin3}; spr(c[(anim / 90 + x) % 4], x, y); break; }
    case PO_STAR: spr(&starcoin, x, y + pg_sin(anim) * 4 / 1024); break;
    case PO_BALLOON: case PO_SHIELD: case PO_MAGNET: case PO_FEATHER: case PO_HOURGLASS: case PO_PIN:
        spr(&bubble, x, y + pg_sin(anim + x) * 3 / 1024); spr(pickup_icon(o->type), x, y + pg_sin(anim + x) * 3 / 1024); break;
    case PO_BEE: spr_flip(anim / 80 % 2 ? &bee0 : &bee1, x, y, (o->t / 1000) % (o->a * 4 ? o->a * 4 : 1) < o->a * 2 ? (o->b > 0) : (o->b < 0)); break;
    case PO_SPIKEBALL: spr(&spike, x, y); break;
    case PO_CROW: spr_flip(o->flags == 1 ? &crow1 : o->flags == 2 ? &crow2 : (anim / 120 % 2 ? &crow0 : &crow2), x, y, o->vx > 0); break;
    case PO_THORN: for (int k = -o->a / 2; k < o->a / 2; k += 32) sprite_scaled(&thorn, x + k, y - o->b / 2, k + 32 > o->a / 2 ? o->a / 2 - k : 32, o->b, 0); break;
    case PO_ZAP:
        spr(&post, x - o->a / 2, y); spr(&post, x + o->a / 2, y);
        if (pg_zap_on(o, t)) for (int k = -o->a / 2 + 5; k < o->a / 2 - 5; k += 32)
            sprite_scaled(&zap, x + k, y - 6, k + 32 > o->a / 2 - 5 ? o->a / 2 - 5 - k : 32, 12, anim / 60 % 2 ? TINYRT_GFX_FLIP_Y : 0);
        break;
    case PO_THUNDER:
        spr(pg_thunder_warn(o, t) || pg_thunder_strike(o, t) ? &thunder1 : &thunder0, x, y);
        if (pg_thunder_warn(o, t) && anim / 100 % 2) rect(x - 22, y + 20, 44, 200, 0xfff36b, 60);
        if (pg_thunder_strike(o, t)) { sprite_scaled(&bolt, x - 12, y + 14, 24, 104, 0); sprite_scaled(&bolt, x - 12, y + 114, 24, 104, TINYRT_GFX_FLIP_X); }
        break;
    case PO_WIND: {
        rect(x - o->a / 2, y - 80, o->a, 160, 0xffffff, 28);
        int dir = o->b > 0;
        for (int k = 0; k < 3; k++) {
            int ax = x - o->a / 2 + ((anim / 6 * (dir ? 1 : -1) + k * 110) % o->a + o->a) % o->a;
            spr_flip(&arrow, ax, y - 50 + k * 50, !dir);
        }
        break;
    }
    case PO_BOUNCE: sprite_scaled(&bounce, x - o->a / 2, y - 14, o->a, 28, 0); break;
    case PO_RAIN:
        rect(x - o->a / 2, y - o->b / 2, o->a, o->b, 0x4a5b8c, 60);
        for (int k = 0; k < 10; k++) spr(&drop, x - o->a / 2 + (k * 37) % o->a, y - o->b / 2 + ((anim / 3 + k * 53) % o->b));
        break;
    case PO_VORTEX: sprite_scaled(&swirl, x - o->a / 2, y - o->a / 2, o->a, o->a, (anim / 120 % 4) << TINYRT_GFX_ROTATE_SHIFT); break;
    case PO_FIREFLY: spr(&firefly, x, y + pg_sin(anim * 2 + x) * 6 / 1024); break;
    case PO_GOAL: spr(&flag, x, y - 30); break;
    default: break;
    }
}

static void draw_boss(void) {
    if (!game.level->boss) return;
    int x = (int)(game.bx / PG_FP), y = sy((int)(game.by / PG_FP)) ;
    int blink = game.boss_hurt_ms > 0 && anim / 60 % 2;
    if (blink) return;
    switch (game.level->boss) {
    case PB_QUEEN: spr2(anim / 90 % 2 ? &queen0 : &queen1, x, y); break;
    case PB_STORMCLOUD:
        spr2(game.boss_attack == 1 || game.boss_attack == 2 ? &storm1 : &storm0, x, y);
        if (game.boss_attack == 1 && anim / 90 % 2) rect(x - 30, y + 30, 60, 466 - y, 0xfff36b, 50);
        if (game.boss_attack == 2) for (int k = y + 30; k < 466; k += 100) sprite_scaled(&bolt, x - 16, k, 32, 100, (k / 100) % 2 ? TINYRT_GFX_FLIP_X : 0);
        break;
    case PB_OWL: spr2(game.boss_attack == 1 ? &owl1 : game.boss_attack == 3 ? &owl2 : &owl0, x, y); break;
    case PB_KING: {
        int kp = 1 + (game.boss_phase % 3);
        spr2(game.boss_attack ? &king1 : &king0, x, y);
        if (kp == PB_STORMCLOUD && game.boss_attack == 1 && anim / 90 % 2) rect(x - 30, y + 30, 60, 466 - y, 0xfff36b, 50);
        if (kp == PB_STORMCLOUD && game.boss_attack == 2)
            for (int k = y + 40; k < 466; k += 100) sprite_scaled(&bolt, x - 16, k, 32, 100, (k / 100) % 2 ? TINYRT_GFX_FLIP_X : 0);
        break;
    }
    }
    /* boss health pips */
    for (int i = 0; i < game.boss_max; i++) rect(233 - game.boss_max * 12 + i * 24 + 4, 92, 16, 8, i < game.boss_hp ? 0xff5d6c : 0x4a4f66, 255);
}

static void draw_player(void) {
    static const sprite_t *const colors[] = {&b_red, &b_blue, &b_green, &b_yellow, &b_purple};
    int x = (int)(game.px / PG_FP) + (shake_ms > 0 ? (anim / 15 % 2 ? 3 : -3) : 0);
    int y = sy((int)(game.py / PG_FP));
    if (!(game.invuln_ms > 0 && anim / 70 % 2)) {
        int n = game.balloons;
        for (int i = 0; i < n; i++) {
            int bx = x + (i - (n - 1) / 2) * 13 - (n % 2 ? 0 : 6), by = y - 52 - (i % 2) * 8 + pg_sin(anim + i * 200) * 3 / 1024;
            rect(bx, by + 13, 1, y - 14 - by - 13, 0x6b6e78, 255);   /* string down to the antenna */
            spr(colors[i], bx, by);
        }
        int kicking = (blow_level > 0 || (touch_down && (touch_blow || mic_error))) && anim / 120 % 2;
        spr(game.invuln_ms > 800 ? &hero2 : kicking ? &hero1 : &hero0, x, y);
        if (game.shield_ms > 0) arc(x, y - 26, 44, 3, 0, 360, 0x7fe0ff);
        if (game.dash_ms > 0) for (int k = 0; k < 3; k++) rect(x - 10 + k * 10, y + 20 + k * 6, 3, 18, 0xffffff, 160);
    }
    if (game.level->dark) {
        /* Horizontal spans keep the circular view without a per-pixel ring test. */
        int r = game.light, cy = y - 26;
        rect(0, 0, 466, cy - r, 0x05061a, 255);
        rect(0, cy + r, 466, 466 - cy - r, 0x05061a, 255);
        for (int dy = -r; dy < r; dy += 4) {
            int h = r - dy < 4 ? r - dy : 4;
            int mid = dy + h / 2;
            int half = r * circle_width[pg_abs(mid) * 64 / r] / 1024;
            rect(0, cy + dy, x - half, h, 0x05061a, 255);
            rect(x + half, cy + dy, 466 - x - half, h, 0x05061a, 255);
        }
    }
}

static void progress_gauge(int first, int thickness, uint32_t color) {
    /* Precomputed arc points avoid host atan2 work on every display pixel. */
    for (int i = first; i <= 120; i++)
        rect(233 + gauge_points[i][0] - thickness / 2,
             233 + gauge_points[i][1] - thickness / 2, thickness, thickness, color, 255);
}

static void hud(void) {
    /* balloons (left arc), coins (top), timer/height (right arc), feather (bottom) */
    spr(&i_balloon, 112, 86); spr(&d_x, 132, 88); number(game.balloons, 152, 88);
    spr(&coin0, 196, 52); number(game.coins, 226, 54);
    if (!endless && game.level->kind == PG_KIND_COLLECT) { spr(&d_slash, 250, 54); number(game.level->coins_needed, 276, 54); }
    for (int i = 0, n = endless ? 0 : pg_total_stars(game.level); i < n; i++)
        solid(&starcoin, (!endless && game.level->kind == PG_KIND_COLLECT ? 318 : 290) + i * 22, 54, i < game.stars ? 0xffd23f : 0x4a4f66);
    if (game.time_limit_ms) {
        int left = (game.time_limit_ms - game.time_ms) / 1000;
        number(left, 354, 88);
        int frac = left * 1000 / (game.time_limit_ms ? game.time_limit_ms / 120 : 1);
        progress_gauge(0, 7, 0x2e2440);
        if (frac > 0) progress_gauge(120 - (frac > 120 ? 120 : frac), 4, left < 10 ? 0xff5d6c : 0xffffff);
    } else if (!game.level->boss) {
        int h = endless ? (int)(game.py / PG_FP) / 10 : 0;
        if (endless) { number(h, 340, 88); spr(&d_m, 372, 88); }
        else {
            int prog = (int)(game.py / PG_FP) * 100 / game.level->height;
            /* climb gauge on the right edge: bottom (60 deg) to top (-60 deg) */
            progress_gauge(0, 7, 0x2e2440);
            int p = prog > 100 ? 100 : prog;
            if (p > 0) progress_gauge(120 - 120 * p / 100, 4, 0xffd23f);
        }
    }
    if (game.has_feather) { round_rect(213, 400, 40, 40, 20, 0x2e2440); spr(&i_feather, 233, 420); }
    int icons = 0;
    if (game.magnet_ms > 0) spr(&i_magnet, 160 + 24 * icons++, 400);
    if (game.slow_ms > 0) spr(&i_hourglass, 160 + 24 * icons++, 400);
    if (pop_ms > 0) {
        int span = (400 - pop_ms) / 10;
        for (int i = 0; i < 8; i++) rect(pop_x + pg_sin(i * 128) * span / 1024, pop_y + pg_sin(i * 128 + 256) * span / 1024, 4, 4, 0xff5d6c, 255);
    }
    if (game.state == PG_PLAYING && game.level->kind == PG_KIND_ESCAPE) {
        int wy = sy((int)(game.wall_y / PG_FP));
        if (wy < 466) { rect(0, wy, 466, 466 - wy, 0x4a3d6b, 200); for (int k = 0; k < 466; k += 40) spr(&swirl, k + 20, wy); }
    }
}

static const char *const hints[] = {
    "", "吹气上升，倾斜设备左右移动", "侧风会把你吹走", "收集金币与星币", "从下方撞首领的粉色弱点",
    "萤火虫能照亮四周", "倾斜躲开，从下方撞它的粉色弱点", "泡泡里有道具，羽毛点屏幕使用", "收齐金币才开终点",
    "风暴在后面追，快往上飞！", "通过第 1-4 关解锁无尽模式", "先通过前一关",
};

/* ------------------------------------------------------------------ screens */
static void star_row(int count, int cx, int cy, int size) {
    for (int i = 0; i < 3; i++) {
        const sprite_t *s = i < count ? &star_on : &star_off;
        sprite_scaled(s, cx + (i - 1) * (size + 6) - size / 2, cy - size / 2, size, size, 0);
    }
}
static void title_screen(void) {
    background(0, anim / 4);
    spr(&b_red, 205, 150 + pg_sin(anim) * 6 / 1024); spr(&b_blue, 233, 140 + pg_sin(anim + 200) * 6 / 1024);
    spr(&b_yellow, 261, 150 + pg_sin(anim + 400) * 6 / 1024); spr(&hero0, 233, 196);
    text(233, 92, 300, 48, 0xffffff, "PUFF");
    button(233, 262, 180, 0xff8a3d, "冒险");
    button(233, 318, 180, unlocked >= 4 ? 0x4fb3ff : 0x8a8fa8, "无尽");
    button(233, 374, 180, 0x5ad18a, "设置");
    if (mic_error) text(233, 420, 320, 18, 0xffffff, "麦克风不可用，可在设置中用按屏代替");
}
static void map_screen(void) {
    static const char *const worlds[] = {"花园", "云海", "夜空", "风暴"};
    background(map_world, 0);
    text(233, 82, 200, 24, 0xffffff, worlds[map_world]);
    int total = 0; for (int i = 0; i < PG_LEVEL_COUNT; i++) total += stars[i];
    spr(&star_on, 196, 120); number(total, 228, 122); spr(&d_slash, 252, 122); number(PG_LEVEL_COUNT * 3, 280, 122);
    for (int i = 0; i < 4; i++) {
        int lv = map_world * 4 + i;
        int lx = 233 + (i % 2 ? 62 : -62), ly = 205 + (i / 2) * 104;
        int open = lv <= unlocked;
        round_rect(lx - 52, ly - 45, 104, 90, 18, 0x2e2440);
        round_rect(lx - 50, ly - 45, 100, 86, 16, open ? (i == 3 ? 0xff6b8a : 0xffffff) : 0x8a8fa8);
        if (open) {
            if (i == 3) text(lx, ly - 18, 96, 18, 0xffffff, "首领");
            else { static const char *const ids[] = {"1-1","1-2","1-3","2-1","2-2","2-3","3-1","3-2","3-3","4-1","4-2","4-3"};
                   text(lx, ly - 18, 96, 26, 0x2e2440, ids[map_world * 3 + i]); }
            star_row(stars[lv], lx, ly + 18, 18);
        } else spr(&lock, lx, ly);
    }
    if (map_world > 0) { round_rect(54, 203, 52, 60, 18, 0x2e2440); spr_flip(&arrow, 80, 233, 1); }
    if (map_world < 3) { round_rect(360, 203, 52, 60, 18, 0x2e2440); spr(&arrow, 386, 233); }
    button(233, 405, 140, 0x8a8fa8, "返回");
}
static void card_screen(void) {
    int world = card_level == ENDLESS_LEVEL ? 0 : pg_levels[card_level].world - 1;
    background(world, 0);
    round_rect(83, 110, 300, 250, 28, 0x2e2440);
    round_rect(87, 112, 292, 244, 26, 0xfff7e8);
    if (card_level == ENDLESS_LEVEL) {
        text(233, 150, 280, 24, 0x2e2440, "无尽爬升");
        text(233, 200, 280, 18, 0x5c5470, "能飞多高就飞多高");
        text(233, 240, 280, 18, 0x5c5470, "最高纪录");
        number(best_height, 220, 276); spr(&d_m, 262, 276);
    } else {
        static const char *const kinds[] = {"飞到终点旗", "限时飞到终点", "收齐金币后到终点", "逃离追赶的风暴", "击败首领"};
        const pg_level_t *l = &pg_levels[card_level];
        text(233, 150, 280, 24, 0x2e2440, l->name);
        text(233, 192, 280, 18, 0x5c5470, kinds[l->kind]);
        star_row(stars[card_level], 233, 240, 30);
        text(233, 282, 280, 15, 0x5c5470, "星：通关 / 全部星币 / 不丢气球");
    }
    button(233, 330, 180, 0xff8a3d, "开始");
    button(233, 392, 140, 0x8a8fa8, "返回");
}
static void play_screen(void) {
    background(world_of_current(), game.camera);
    if (game.camera < 120) {
        int gy = sy(0);
        sprite_scaled(&hill, 0, gy - 20, 240, 80, 0); sprite_scaled(&hill, 230, gy - 26, 240, 86, TINYRT_GFX_FLIP_X);
    }
    for (int i = 0; i < game.obj_count; i++) if (game.obj[i].alive) draw_object(&game.obj[i]);
    draw_boss();
    draw_player();
    hud();
    if (countdown_ms > 0) {
        round_rect(153, 190, 160, 70, 24, 0x2e2440);
        text(233, 225, 160, 36, 0xffffff, countdown_ms > 1000 ? "准备" : "出发！");
    }
    if (hint_ms > 0 && hint_id) {
        int hw = text_width(hints[hint_id], 18) + 28;
        round_rect(233 - hw / 2, 372, hw, 36, 18, 0x2e2440);
        text(233, 389, hw, 18, 0xffffff, hints[hint_id]);
    }

}
static void result_screen(void) {
    background(world_of_current(), game.camera);
    round_rect(73, 90, 320, 290, 30, 0x2e2440);
    round_rect(77, 92, 312, 284, 28, 0xfff7e8);
    int won = game.state == PG_WON;
    if (endless) {
        text(233, 130, 280, 30, 0x2e2440, new_record ? "新纪录！" : "飞行结束");
        number((int)(game.py / PG_FP) / 10, 215, 180); spr(&d_m, 255, 180);
        spr(&coin0, 205, 220); number(game.score, 245, 222);
    } else {
        text(233, 130, 280, 30, 0x2e2440, won ? "通关！" : game.time_limit_ms && game.time_ms >= game.time_limit_ms ? "时间到" : "气球飞光了");
        int shown = result_ms / 350; if (shown > result_stars) shown = result_stars;
        star_row(won ? shown : 0, 233, 186, 40);
        spr(&coin0, 205, 232); number(game.coins, 240, 234);
    }
    if (result_ms >= 700) {
        int has_next = won && !endless && card_level + 1 < PG_LEVEL_COUNT;
        if (has_next) { button(233, 300, 180, 0xff8a3d, "下一关"); button(233, 356, 180, 0x4fb3ff, "重试"); }
        else button(233, 300, 180, 0xff8a3d, "再来一次");
        button(233, 410, 140, 0x8a8fa8, endless ? "主页" : "地图");
    }
}
static void cal_screen(void) {
    background(1, 0);
    round_rect(73, 110, 320, 250, 30, 0x2e2440);
    round_rect(77, 112, 312, 244, 28, 0xfff7e8);
    if (mic_error) { text(233, 200, 300, 20, 0x2e2440, "麦克风不可用"); text(233, 240, 300, 16, 0x5c5470, "可在设置中打开按屏代吹"); text(233, 320, 300, 16, 0x5c5470, "点击返回"); return; }
    static const char *const steps[] = {"请保持安静", "对着屏幕持续吹气", "标定完成"};
    text(233, 160, 300, 24, 0x2e2440, steps[cal_step]);
    int prog = cal_window.frames * 100 / (cal_step == 1 ? 30 : 100);
    if (cal_step < 2) {
        arc(233, 250, 48, 10, 0, 360, 0xd8d2e6);
        arc(233, 250, 48, 10, 270, 270 + prog * 360 / 100 > 630 ? 630 : 270 + prog * 360 / 100, 0xff8a3d);
        spr(cal_step ? &hero1 : &hero0, 233, 250);
    } else {
        spr(&hero1, 233, 240);
        text(233, 320, 300, 16, 0x5c5470, "点击返回");
    }
}
static void settings_screen(void) {
    static const char *const sens[] = {"倾斜灵敏度：低", "倾斜灵敏度：中", "倾斜灵敏度：高"};
    background(1, 0);
    text(233, 92, 200, 24, 0x2e2440, "设置");
    button(233, 150, 220, 0xff8a3d, "重新标定麦克风");
    button(233, 206, 220, 0x4fb3ff, fixed_mode ? "吹气：匀速" : "吹气：力度");
    button(233, 262, 220, 0x5ad18a, sens[tilt_sens]);
    button(233, 318, 220, 0xb98bff, touch_blow ? "按屏代吹：开" : "按屏代吹：关");
    button(233, 392, 140, 0x8a8fa8, "返回");
}

/* ------------------------------------------------------------------ exports */
int32_t tinyrt_init(int32_t width, int32_t height) {
    if (width != 466 || height != 466) return -1;
    uint16_t palette[256];
    if (asset_read(PALETTE_OFFSET, palette, sizeof palette) != (int32_t)sizeof palette) return -1;
    if (gfx_pal_upload(0, 0, 256, palette)) return -1;
    if (gfx_tex_upload(0, TINYRT_GFX_INDEX8, ATLAS_WIDTH, ATLAS_HEIGHT, (const void *)0,
                       ATLAS_WIDTH * ATLAS_HEIGHT, TINYRT_GFX_FROM_ASSET)) return -1;
    static const uint16_t glyph_palette[2] = {0, 0xffff};
    if (gfx_pal_upload(1, 0, 2, glyph_palette)) return -1;
    if (gfx_tex_upload(1, TINYRT_GFX_INDEX8, TEXT_ATLAS_WIDTH, TEXT_ATLAS_HEIGHT, (const void *)TEXT_ATLAS_OFFSET,
                       TEXT_ATLAS_WIDTH * TEXT_ATLAS_HEIGHT, TINYRT_GFX_FROM_ASSET)) return -1;
    if (input_events(TINYRT_INPUT_LIFECYCLE_MASK | TINYRT_IN_MOTION)) return -1;
    load();
    last_ms = now_ms();
    if (!calibration.ok && !touch_blow) {
        screen = S_CAL; cal_step = 0; cal_window = (ta_calib_window_t){0};
        (void)mic_begin();
    }
    return clock_interval(33);
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    if (kind == TINYRT_TOUCH_PRESS) { press(x, y); frame_ready = 1; }
    else if (kind == TINYRT_TOUCH_RELEASE || kind == TINYRT_TOUCH_CANCEL) touch_down = 0;
    else if (kind == TINYRT_EV_MOTION) {
        /* S31 screen-right tilt follows the host X axis. */
        tilt_raw = x;
        tilt_update();
    } else if (kind == TINYRT_CLOCK_EVENT) {
        frame_ready = 1;
        uint32_t now = now_ms(), delta = now - last_ms;
        last_ms = now;
        if (delta > 100) delta = 100;
        anim += (int)delta;
        if (hint_ms > 0) hint_ms -= (int)delta;
        if (shake_ms > 0) shake_ms -= (int)delta;
        if (pop_ms > 0) pop_ms -= (int)delta;
        if (screen == S_RESULT) result_ms += (int)delta;
        if (pending_level != -2) {
            int lv = pending_level; pending_level = -2;
            start_level(lv); return 0;
        }
        if (screen == S_CAL) { mic_pump(on_cal_frame); return 0; }
        if (screen != S_PLAY) return 0;
        mic_pump(on_play_frame);
        if (!mic_running && !touch_blow && !mic_error) (void)mic_start();
        /* Fixed 10 ms steps; at most 6 per tick so one callback stays within its budget.
         * Leftover time is carried, so slow frames only slow the game slightly. */
        step_debt += (int)delta;
        if (step_debt > 6 * PG_STEP_MS) step_debt = 6 * PG_STEP_MS;
        while (step_debt >= PG_STEP_MS && screen == S_PLAY) { play_tick(); step_debt -= PG_STEP_MS; }
    }
    (void)arg;
    return 0;
}

int32_t tinyrt_render(void) {
    if (!frame_ready) return draw_skip();
    frame_ready = 0;
    used = 0;
    switch (screen) {
    case S_TITLE: title_screen(); break;
    case S_MAP: map_screen(); break;
    case S_CARD: card_screen(); break;
    case S_PLAY: play_screen(); break;
    case S_RESULT: result_screen(); break;
    case S_CAL: cal_screen(); break;
    case S_SETTINGS: settings_screen(); break;
    }
    if (gfx_begin(0)) return -1;
    for (uint32_t off = 0; off < used; ) {               /* submit in <= 16 KiB pieces on record boundaries */
        uint32_t end = off;
        while (end < used) {
            uint32_t size = (uint32_t)cmd.bytes[end + 2] | (uint32_t)cmd.bytes[end + 3] << 8;
            if (end + size - off > TINYRT_GFX_MAX_SUBMIT) break;
            end += size;
        }
        if (gfx_submit(cmd.bytes + off, end - off)) return -1;
        off = end;
    }
    return gfx_end();
}
