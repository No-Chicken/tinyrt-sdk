#include "tinyrt.h"

/* Keep the serialized format independent of compiler structure layout. */
static uint8_t color[3] = {0x18, 0x33, 0x58};

int32_t tinyrt_init(int32_t width, int32_t height) {
    uint8_t saved[3];
    int32_t n = blob_read(saved, sizeof(saved));
    if (n < 0) return n;
    if (n == (int32_t)sizeof(saved))
        for (unsigned i = 0; i < sizeof(saved); ++i) color[i] = saved[i];
    return input_events(0);
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    if (kind != TINYRT_TOUCH_RELEASE) return 0;
    uint8_t first = color[0];
    color[0] = color[1];
    color[1] = color[2];
    color[2] = first;
    return blob_write(color, sizeof(color)) == (int32_t)sizeof(color) ? 0 : -1;
}

int32_t tinyrt_render(void) {
    return draw_clear(((uint32_t)color[0] << 16) | ((uint32_t)color[1] << 8) | color[2]);
}
