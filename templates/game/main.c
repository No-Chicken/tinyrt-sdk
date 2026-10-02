#include "tinyrt.h"

/* Host drives callbacks. Compute in event, submit graphics in render. */
static uint16_t pixels[32 * 32];
static int32_t player_x, player_y, pointer_x, pointer_y, held, dirty;
static uint32_t previous_ms;

int32_t tinyrt_init(int32_t width, int32_t height)
{
    if (width != 466 || height != 466) return -1;
    player_x = player_y = 16;
    held = 0; dirty = 1; previous_ms = now_ms();
    if (input_events(TINYRT_INPUT_LIFECYCLE_MASK) != 0) return -1;
    return clock_interval(33);
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg)
{
    (void)arg;
    if (kind == TINYRT_TOUCH_PRESS || kind == TINYRT_TOUCH_MOVE) {
        held = 1; pointer_x = x; pointer_y = y;
    } else if (kind == TINYRT_TOUCH_RELEASE || kind == TINYRT_TOUCH_CANCEL) held = 0;
    if (kind == TINYRT_CLOCK_EVENT) {
        uint32_t current = now_ms();
        uint32_t elapsed = current - previous_ms; previous_ms = current;
        if (held && elapsed) {
            /* Clamp to the safe central play area, independent of the pointer. */
            player_x += pointer_x > 233 ? 1 : -1;
            player_y += pointer_y > 233 ? 1 : -1;
            if (player_x < 2) player_x = 2;
            if (player_x > 29) player_x = 29;
            if (player_y < 2) player_y = 2;
            if (player_y > 29) player_y = 29;
            dirty = 1;
        }
    }
    return 0;
}

int32_t tinyrt_render(void)
{
    if (!dirty) return draw_skip();
    dirty = 0;
    for (int32_t y = 0; y < 32; ++y) {
        for (int32_t x = 0; x < 32; ++x) {
            uint16_t color = ((x / 4 + y / 4) & 1) ? 0x1928 : 0x2149;
            if (x >= player_x - 1 && x <= player_x + 1 &&
                y >= player_y - 1 && y <= player_y + 1) color = 0x3cff;
            pixels[y * 32 + x] = color;
        }
    }
    if (draw_clear(0x101c2a) != 0) return -1;
    if (draw_text_box(88, 73, 290, 30, "ORBIT RUNNER", 12, 0xffffff, 24, 1) != 0) return -1;
    if (draw_rgb565_scaled(105, 105, 256, 256, 32, 32, (const uint8_t *)pixels, sizeof(pixels)) != 0) return -1;
    return draw_text_box(83, 377, 300, 24, "Hold to move", 12, 0x8cb0c8, 18, 1);
}

int32_t tinyrt_stop(void) { held = 0; return 0; }
