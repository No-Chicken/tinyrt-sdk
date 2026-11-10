#include "tinyrt.h"

static unsigned count;
static int32_t center_x, center_y;

int32_t tinyrt_init(int32_t width, int32_t height) {
    center_x = width / 2;
    center_y = height / 2;
    return input_events(0);
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    if (kind == TINYRT_TOUCH_RELEASE) count = (count + 1) % 100;
    return 0;
}

int32_t tinyrt_render(void) {
    char text[] = "Taps 00";
    text[5] = '0' + count / 10;
    text[6] = '0' + count % 10;
    if (draw_clear(0x142333)) return -1;
    return draw_text(center_x - 36, center_y, text, sizeof(text) - 1, 0xffffff);
}
