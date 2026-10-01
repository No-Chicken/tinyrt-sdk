#include "tinyrt.h"

#ifdef POMODORO_FAST
#define WORK_MS 25000u
#define BREAK_MS 5000u
#define SAVE_TAG 0x60000000u
#else
#define WORK_MS 1500000u
#define BREAK_MS 300000u
#define SAVE_TAG 0x50000000u
#endif
#define REMAIN_MASK 0x001fffffu
#define PHASE_BIT 0x00200000u
#define CHECKPOINT_MS 60000u
#define IVORY 0xf5efdfu
#define AMBER 0xe8ac59u
#define MUTED 0xa69e90u
#define TRACK 0x37322bu
static uint32_t remaining, last_ms, checkpoint_ms, phase;
static int running, completed;

static uint32_t duration(void) { return phase ? BREAK_MS : WORK_MS; }

/* One i32 snapshot avoids partially updated phase/remaining pairs. The clock
 * deadline and running flag are never persisted: reopening is always paused. */
static int32_t save(void) {
    return kv_set(0, (int32_t)(SAVE_TAG | (phase ? PHASE_BIT : 0u) | remaining));
}

static int advance(uint32_t now) {
    uint32_t elapsed = now - last_ms;
    last_ms = now;
    if (!running) return 0;
    if (elapsed >= remaining) {
        phase ^= 1u;
        remaining = duration();
        running = 0;
        completed = 1;
        checkpoint_ms = 0;
        return 1;
    }
    remaining -= elapsed;
    checkpoint_ms += elapsed;
    return 0;
}

int32_t tinyrt_init(int32_t width, int32_t height) {
    /* This demo is laid out and tested for the product's 466px circular panel. */
    if (width != 466 || height != 466) return -1;
    phase = 0;
    remaining = WORK_MS;
    running = completed = 0;
    checkpoint_ms = 0;
    last_ms = now_ms();
    uint32_t saved = (uint32_t)kv_get(0, 0);
    if ((saved & ~(REMAIN_MASK | PHASE_BIT)) == SAVE_TAG) {
        uint32_t saved_phase = (saved & PHASE_BIT) ? 1u : 0u;
        uint32_t saved_remaining = saved & REMAIN_MASK;
        if (saved_remaining > 0 && saved_remaining <= (saved_phase ? BREAK_MS : WORK_MS)) {
            phase = saved_phase;
            remaining = saved_remaining;
        }
    }
    return 0;
}

/* Restrict release targets to the visible rounded button, including corners. */
static int hit(int32_t x, int32_t y, int32_t left, int32_t width) {
    const int32_t top = 358, height = 52, radius = 26;
    if (x < left || x >= left + width || y < top || y >= top + height) return 0;
    int32_t dx = x < left + radius ? left + radius - x :
        x >= left + width - radius ? x - (left + width - radius - 1) : 0;
    int32_t dy = y < top + radius ? top + radius - y : y - (top + height - radius - 1);
    return dx * dx + dy * dy <= radius * radius;
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    (void)arg;
    if (kind == TINYRT_CLOCK_EVENT) {
        int changed = advance(now_ms());
        if (changed || (running && checkpoint_ms >= CHECKPOINT_MS)) {
            checkpoint_ms = 0;
            return save();
        }
    } else if (kind == TINYRT_TOUCH_RELEASE) {
        if (hit(x, y, 294, 52)) {
            phase = 0;
            remaining = WORK_MS;
            running = completed = 0;
        } else if (hit(x, y, 130, 150)) {
            if (running) {
                (void)advance(now_ms());
                running = 0;
            } else {
                running = 1;
                completed = 0;
                last_ms = now_ms();
            }
        } else return 0;
        checkpoint_ms = 0;
        return save();
    }
    return 0;
}

/* Normal host teardown calls this once before destroying the instance. A failed
 * write is returned to the host; the previous committed snapshot stays valid. */
int32_t tinyrt_stop(void) {
    (void)advance(now_ms());
    running = 0;
    checkpoint_ms = 0;
    return save();
}

static void text(int32_t x, int32_t y, int32_t w, int32_t h,
                 const char *s, uint32_t len, uint32_t color, int32_t font) {
    (void)draw_text_box(x, y, w, h, s, len, color, font, 1);
}

int32_t tinyrt_render(void) {
    uint32_t seconds = (remaining + 999u) / 1000u;
    char time[5] = {(char)('0' + seconds / 600u), (char)('0' + seconds / 60u % 10u),
                   ':', (char)('0' + seconds % 60u / 10u), (char)('0' + seconds % 10u)};
    draw_clear(0);
    /* The top system return region x203..263,y24..76 stays empty. */
#ifdef POMODORO_FAST
    text(123, 90, 220, 30, "TEST 25s/5s", 11, AMBER, 24);
#else
    text(123, 90, 220, 30, "POMODORO", 8, IVORY, 24);
#endif
    draw_arc(233, 233, 114, 6, 0, 360, TRACK);
    uint32_t degrees = (remaining * 360u + duration() - 1u) / duration();
    /* A top-origin remaining ring uses two non-wrapping ABI arc segments. */
    if (degrees > 0) draw_arc(233, 233, 114, 6, 270, degrees < 90u ? 270 + (int32_t)degrees : 360, AMBER);
    if (degrees > 90u) draw_arc(233, 233, 114, 6, 0, (int32_t)degrees - 90, AMBER);
    text(163, 156, 140, 30, phase ? "BREAK" : "FOCUS", 5, AMBER, 24);
    text(123, 202, 220, 62, time, 5, IVORY, 48);
    text(153, 268, 160, 26, running ? "RUNNING" : "PAUSED", running ? 7 : 6, MUTED, 18);
    if (completed) text(153, 296, 160, 26, phase ? "FOCUS DONE" : "BREAK DONE", 10, IVORY, 18);
    else if (!running) text(153, 296, 160, 26, "TAP START", 9, MUTED, 18);
    draw_round_rect(130, 358, 150, 52, 26, IVORY);
    text(142, 369, 126, 30, running ? "PAUSE" : "START", 5, 0, 24);
    draw_round_rect(294, 358, 52, 52, 26, TRACK);
    draw_arc(320, 384, 12, 2, 35, 315, IVORY);
    draw_rect(327, 373, 5, 2, IVORY);
    draw_rect(330, 373, 2, 6, IVORY);
    return 0;
}
