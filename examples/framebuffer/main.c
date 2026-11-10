#include "tinyrt.h"

enum { PATCH = 24 };
static uint16_t pixels[PATCH * PATCH];

int32_t tinyrt_init(int32_t width, int32_t height) {
    for (unsigned y = 0; y < PATCH; ++y)
        for (unsigned x = 0; x < PATCH; ++x)
            pixels[y * PATCH + x] = ((x / 4 + y / 4) & 1) ? 0xfde0 : 0x07bf;
    return fb_config(192, 192, 2);
}

int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    return 0;
}

int32_t tinyrt_render(void) {
    return fb_present(pixels, sizeof(pixels), 104, 104, PATCH, PATCH);
}
