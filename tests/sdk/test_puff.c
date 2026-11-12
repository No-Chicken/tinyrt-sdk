#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define PUFF_CAL_FLOOR_Q8 (-17461)
#define PUFF_CAL_REF_Q8 (-6403)
#include "../../examples/puff/main.c"

static unsigned char asset[400000];
static unsigned asset_size, test_ms, input_mask, submitted, large_partial_arcs, large_circles, night_spans;
static int callback_phase, opened, values[16], present[16];
uint32_t now_ms(void) { return test_ms; }
int32_t clock_interval(int32_t ms) { assert(ms >= 16 && ms <= 50); return 0; }
int32_t input_events(uint32_t mask) { assert(callback_phase == 0); input_mask = mask; return 0; }
int32_t kv_get(uint32_t k, int32_t fallback) { assert(k < 16); return present[k] ? values[k] : fallback; }
int32_t kv_set(uint32_t k, int32_t v) { assert(k < 16); present[k] = 1; values[k] = v; return 0; }
int32_t asset_read(uint32_t o, void *p, uint32_t n) { assert(o + n <= asset_size); memcpy(p, asset + o, n); return n; }
int32_t gfx_pal_upload(uint32_t s, uint32_t first, uint32_t n, const uint16_t *p) {
    (void)first; (void)p; assert(s < 2 && n <= 256); return 0;
}
int32_t gfx_tex_upload(uint32_t s, uint32_t f, uint32_t w, uint32_t h, const void *p, uint32_t n, uint32_t flags) {
    (void)f; (void)flags; assert(s < 2 && n == w * h && (uintptr_t)p + n <= asset_size); return 0;
}
int32_t gfx_begin(uint32_t flags) { (void)flags; submitted++; return 0; }
int32_t gfx_end(void) { return 0; }
static uint32_t read32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
int32_t gfx_submit(const void *p, uint32_t n) {
    const uint8_t *b = p; unsigned pos = 0;
    assert(n > 0 && n <= TINYRT_GFX_MAX_SUBMIT);
    while (pos < n) { unsigned size = b[pos + 2] | b[pos + 3] << 8; assert(size >= 8 && pos + size <= n); if (b[pos] == TINYRT_GFX_ARC && size == 32 && read32(b+pos+12) > 100 && (read32(b+pos+20) != 0 || read32(b+pos+24) != 360)) large_partial_arcs++; if (b[pos] == TINYRT_GFX_ARC && size == 32 && read32(b+pos+12) > 100) large_circles++; if (b[pos] == TINYRT_GFX_RECT && size == 28 && read32(b+pos+20) == rgb565(0x05061a)) night_spans++; pos += size; }
    assert(pos == n); return 0;
}
int32_t draw_skip(void) { return 0; }
int32_t audio_play(uint32_t o, uint32_t n, uint32_t rate) { assert(o + n <= asset_size && rate == 16000); return 0; }
int32_t haptic_pulse(uint32_t s, uint32_t ms) { assert(s <= 100 && ms <= 2000); return 0; }
int32_t mic_open(uint32_t rate, uint32_t flags) { assert(callback_phase < 2 && rate == 16000 && flags == TINYRT_MIC_FEATURES); opened = 1; return 0; }
int32_t mic_gain(int32_t db) { assert(opened && db == 12); return db; }
int32_t mic_close(void) { assert(callback_phase < 2); opened = 0; return 0; }
int32_t mic_features(void *p, uint32_t n) { (void)p; (void)n; assert(opened); return 0; }

static int failures;
static void check(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "FAIL %s\n", message); failures++; }
}
static void render(void) { callback_phase = 2; assert(tinyrt_render() == 0); }
static void event(int kind, int x, int y) { callback_phase = 1; assert(tinyrt_event(kind, x, y, 0) == 0); }

int main(int argc, char **argv) {
    assert(argc == 2); FILE *f = fopen(argv[1], "rb"); assert(f);
    asset_size = (unsigned)fread(asset, 1, sizeof asset, f); fclose(f);
    assert(tinyrt_init(466, 466) == 0);
    check(!(input_mask & TINYRT_INPUT_USER_KEY_MASK), "short key must remain owned by the hall");
    render(); unsigned before = submitted;
    for (int i = 0; i < 20; i++) { event(TINYRT_EV_MOTION, 450, 0); render(); }
    check(tilt_level > 0, "positive screen-right IMU input must steer right");
    check(submitted == before, "motion events must not submit duplicate frames");
    event(TINYRT_EV_MOTION, -450, 0);
    check(tilt_level < 0, "negative IMU input must steer left");
    test_ms += 33; event(TINYRT_CLOCK_EVENT, 0, 0); render();
    check(submitted == before + 1, "clock must submit one animation frame");

    callback_phase = 1; start_level(0);
    test_ms += 33; event(TINYRT_CLOCK_EVENT, 0, 0); render();
    check(!large_partial_arcs, "gameplay gauge must avoid full-screen per-pixel angle calculations");

    callback_phase = 1; start_level(8);
    test_ms += 33; event(TINYRT_CLOCK_EVENT, 0, 0); render();
    check(!large_circles, "night mask must avoid large per-pixel ring calculations");

    game.invuln_ms = 1200; anim = 70; frame_ready = 1; night_spans = 0; render();
    check(night_spans > 10, "night shading must remain when the hurt sprite blinks off");

    pg_game_t g; pg_start(&g, 0, 1234); g.obj_count = 0;
    g.py = 500 * PG_FP; g.vy = 200 * PG_FP;
    pg_input_t quiet = {0};
    for (int i = 0; i < 300; i++) pg_step(&g, &quiet);
    check(g.py == 500 * PG_FP && g.vy == 0, "stopping breath must immediately hover without sinking");
    pg_start(&g, 12, 1234); g.obj_count = 0; g.py = 500 * PG_FP;
    pg_add(&g, PO_RAIN, 233, 500, 400, 500);
    for (int i = 0; i < 50; i++) pg_step(&g, &quiet);
    check(g.py >= 500 * PG_FP, "rain must not turn stopped breathing into automatic descent");
    pg_start(&g, 0, 1234); g.obj_count = 0;
    pg_input_t blowing = {80, 0, 0}; int32_t y = g.py;
    for (int i = 0; i < 100; i++) pg_step(&g, &blowing);
    check(g.py > y, "blowing must still lift the player");

    static const struct { int level, pattern; } bosses[] = {
        {3, PB_QUEEN}, {7, PB_STORMCLOUD}, {11, PB_OWL},
        {15, PB_QUEEN}, {15, PB_STORMCLOUD}, {15, PB_OWL}
    };
    for (unsigned i = 0; i < sizeof bosses / sizeof bosses[0]; i++) {
        for (int strength = 80; strength <= 100; strength += 20) {
            pg_start(&g, bosses[i].level, 1234); g.obj_count = 0;
            g.py = 300 * PG_FP; g.invuln_ms = 1000000;
            g.bx = g.px; g.by = (bosses[i].pattern == PB_OWL ? 165 : 360) * PG_FP;
            if (g.level->boss == PB_KING) g.boss_phase = bosses[i].pattern - 1;
            if (bosses[i].pattern != PB_QUEEN) { g.boss_attack = 2; g.boss_timer = PG_STEP_MS; }
            int hp = g.boss_hp;
            pg_input_t attack = {(uint8_t)strength, 0, 0};
            for (int t = 0; t < 1700 && g.boss_hp == hp; t += PG_STEP_MS) {
                g.px = g.bx; pg_step(&g, &attack);
            }
            check(g.boss_hp < hp, "hovering at the ceiling must not lock a boss weak point");
        }
    }

    if (!failures) puts("PASS Puff key ownership, IMU, frame scheduling and hover");
    return failures ? 1 : 0;
}
