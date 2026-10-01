#include "tinyrt.h"

#define COLS 10u
#define CELLS 80u
#define STEP_MS 400u
#define MAX_CATCHUP_MS (2u * STEP_MS)
#define SAVE_TAG 0x53000000u
#define IVORY 0xf5efdfu
#define GREEN 0x6cbb8bu
#define AMBER 0xe8ac59u
#define MUTED 0xa69e90u
#define PANEL 0x1d2822u
#define BUTTON 0x303b34u

enum { RIGHT, DOWN, LEFT, UP };
enum { READY, RUNNING, PAUSED, GAME_OVER, WON };
static uint8_t body[CELLS], occupied[CELLS];
static uint32_t tail, length, food, direction, queued_direction;
static uint32_t last_ms, elapsed_ms, random_state, best;
static int state, turn_queued, save_pending;

static uint32_t head(void) { return body[(tail + length - 1u) % CELLS]; }

/* A bounded scan chooses a free cell even with only one cell left. */
static void place_food(void) {
    random_state = random_state * 1664525u + 1013904223u;
    uint32_t choice = random_state % (CELLS - length);
    for (uint32_t i = 0; i < CELLS; i++) {
        if (!occupied[i]) {
            if (choice == 0) { food = i; return; }
            choice--;
        }
    }
}

static void reset(uint32_t now) {
    for (uint32_t i = 0; i < CELLS; i++) occupied[i] = 0;
    tail = 0;
    length = 3;
    body[0] = 32; body[1] = 33; body[2] = 34;
    occupied[32] = occupied[33] = occupied[34] = 1;
    food = 37;
    direction = queued_direction = RIGHT;
    turn_queued = 0;
    state = READY;
    elapsed_ms = 0;
    last_ms = now;
    random_state = now ^ 0x8f31a4b7u;
}

/* The version and best score share one KV word, avoiding partial snapshots. */
static int32_t save(void) {
    if (!save_pending) return 0;
    int32_t result = kv_set(0, (int32_t)(SAVE_TAG | best));
    if (result == 0) save_pending = 0;
    return result;
}

static void move(void) {
    direction = queued_direction;
    turn_queued = 0;
    int32_t x = (int32_t)(head() % COLS), y = (int32_t)(head() / COLS);
    if (direction == RIGHT) x++;
    else if (direction == LEFT) x--;
    else if (direction == DOWN) y++;
    else y--;
    if (x < 0 || x >= (int32_t)COLS || y < 0 || y >= 8) {
        state = GAME_OVER;
        return;
    }
    uint32_t next = (uint32_t)y * COLS + (uint32_t)x;
    int eating = next == food;
    /* Moving into the old tail is legal when the tail moves away this step. */
    if (occupied[next] && (eating || next != body[tail])) {
        state = GAME_OVER;
        return;
    }
    if (!eating) occupied[body[tail]] = 0;
    body[(tail + length) % CELLS] = (uint8_t)next;
    occupied[next] = 1;
    if (!eating) {
        tail = (tail + 1u) % CELLS;
        return;
    }
    length++;
    if (length - 3u > best) { best = length - 3u; save_pending = 1; }
    if (length == CELLS) state = WON;
    else place_food();
}

static int32_t advance(uint32_t now) {
    uint32_t delta = now - last_ms;
    last_ms = now;
    if (state != RUNNING) return 0;
    /* Retain ordinary sub-step time; discard backlog after a long host stall. */
    if (delta > MAX_CATCHUP_MS) { delta = MAX_CATCHUP_MS; elapsed_ms = 0; }
    elapsed_ms += delta;
    for (uint32_t i = 0; i < 2 && elapsed_ms >= STEP_MS && state == RUNNING; i++) {
        elapsed_ms -= STEP_MS;
        move();
    }
    return save();
}

int32_t tinyrt_init(int32_t width, int32_t height) {
    if (width != 466 || height != 466) return -1;
    uint32_t saved = (uint32_t)kv_get(0, 0);
    best = (saved & 0xffffff00u) == SAVE_TAG && (saved & 0xffu) <= 77u ? saved & 0xffu : 0;
    save_pending = 0;
    reset(now_ms());
    return 0;
}

/* Only the visible rounded target accepts release events, including corners. */
static int hit(int32_t x, int32_t y, int32_t left, int32_t top, int32_t w, int32_t h) {
    const int32_t radius = 12;
    if (x < left || x >= left + w || y < top || y >= top + h) return 0;
    int32_t dx = x < left + radius ? left + radius - x :
        x >= left + w - radius ? x - (left + w - radius - 1) : 0;
    int32_t dy = y < top + radius ? top + radius - y :
        y >= top + h - radius ? y - (top + h - radius - 1) : 0;
    return dx * dx + dy * dy <= radius * radius;
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    (void)arg;
    if (kind != TINYRT_CLOCK_EVENT && kind != TINYRT_TOUCH_RELEASE) return 0;
    uint32_t now = now_ms();
    int32_t result = advance(now);
    if (result != 0 || kind == TINYRT_CLOCK_EVENT) return result;
    if (hit(x, y, 266, 324, 106, 36)) {
        reset(now);
    } else if (hit(x, y, 94, 324, 106, 36)) {
        if (state == RUNNING) state = PAUSED;
        else {
            if (state == GAME_OVER || state == WON) reset(now);
            state = RUNNING;
        }
        elapsed_ms = 0;
        last_ms = now;
    } else if (state == RUNNING && !turn_queued) {
        uint32_t requested;
        if (hit(x, y, 209, 316, 48, 48)) requested = UP;
        else if (hit(x, y, 153, 372, 48, 48)) requested = LEFT;
        else if (hit(x, y, 209, 372, 48, 48)) requested = DOWN;
        else if (hit(x, y, 265, 372, 48, 48)) requested = RIGHT;
        else return 0;
        /* Compare against the actual movement direction, not repeated touches. */
        if (requested != direction && requested != ((direction + 2u) % 4u)) {
            queued_direction = requested;
            turn_queued = 1;
        }
    }
    return 0;
}

int32_t tinyrt_stop(void) {
    int32_t result = advance(now_ms());
    if (state == RUNNING) state = PAUSED;
    return result != 0 ? result : save();
}

static void text(int32_t x, int32_t y, int32_t w, int32_t h,
                 const char *s, uint32_t n, uint32_t color, int32_t font) {
    (void)draw_text_box(x, y, w, h, s, n, color, font, 1);
}

static void button(int32_t x, int32_t y, int32_t w, int32_t h, const char *s, uint32_t n) {
    (void)draw_round_rect(x, y, w, h, 12, BUTTON);
    text(x, y, w, h, s, n, IVORY, 18);
}

int32_t tinyrt_render(void) {
    uint32_t score = length - 3u;
    char scores[] = "SCORE 00   BEST 00";
    scores[6] = (char)('0' + score / 10u); scores[7] = (char)('0' + score % 10u);
    scores[16] = (char)('0' + best / 10u); scores[17] = (char)('0' + best % 10u);
    (void)draw_clear(0);
    /* All non-clear content fits R=215 and leaves the host return region free. */
    text(143, 80, 180, 26, "SNAKE", 5, IVORY, 24);
    text(103, 107, 260, 24, scores, 18, MUTED, 18);
    (void)draw_round_rect(121, 134, 224, 180, 8, PANEL);
    if (length < CELLS) (void)draw_round_rect(125 + (int32_t)(food % COLS) * 22,
        138 + (int32_t)(food / COLS) * 22, 18, 18, 6, AMBER);
    for (uint32_t i = 0; i < length; i++) {
        uint32_t cell = body[(tail + i) % CELLS];
        (void)draw_round_rect(125 + (int32_t)(cell % COLS) * 22,
            138 + (int32_t)(cell / COLS) * 22, 18, 18, 4, i + 1u == length ? IVORY : GREEN);
    }
    if (state != RUNNING) {
        const char *label = state == READY ? "READY" : state == PAUSED ? "PAUSED" :
            state == WON ? "YOU WIN" : "GAME OVER";
        uint32_t n = state == READY ? 5u : state == PAUSED ? 6u : state == WON ? 7u : 9u;
        (void)draw_round_rect(151, 203, 164, 42, 12, BUTTON);
        text(151, 203, 164, 42, label, n, IVORY, 24);
    }
    button(94, 324, 106, 36, state == RUNNING ? "PAUSE" : state == PAUSED ? "RESUME" : "START",
        state == PAUSED ? 6u : 5u);
    button(266, 324, 106, 36, "RESET", 5);
    button(209, 316, 48, 48, "UP", 2);
    button(153, 372, 48, 48, "<", 1);
    button(209, 372, 48, 48, "DN", 2);
    button(265, 372, 48, 48, ">", 1);
    return 0;
}
