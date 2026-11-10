#include "tinyrt.h"
#include <stddef.h>

static uint16_t image[256*240];
static int32_t x0,y0;
static uint32_t frame;
#ifdef PROBE_LEVEL4
#define PAGES 20
#define RECTS 140
#define TEXT_BYTES 95
#define LABEL_EXTRA " 0123456789 0123456789 012345678"
#else
#define PAGES 16
#define RECTS 125
#define TEXT_BYTES 63
#define LABEL_EXTRA ""
#endif
static const char label[]="Quota 0123456789 0123456789 0123456789 0123456789 0123456789..." LABEL_EXTRA;
_Static_assert(sizeof(label)-1==TEXT_BYTES,"text length");

int32_t tinyrt_init(int32_t width,int32_t height) {
    uint32_t pages=(uint32_t)__builtin_wasm_memory_size(0);
    if (pages<PAGES && __builtin_wasm_memory_grow(0,PAGES-pages)==(size_t)-1) return -1;
#ifdef PROBE_LEVEL4
    tinyrt_sys_info_v4_t info;
    if (sys_info(&info,sizeof(info)) || info.base.mem_pages_max!=20 || info.base.budget_max!=125000 ||
        info.commands_max!=144 || info.image_count_max!=2 || info.text_bytes_max!=95 || info.kv_keys_max!=24)
        return -1;
#endif
    x0=(width-256)/2;
    y0=(height-240)/2;
    for (unsigned y=0;y<240;++y) image[y*256]=(uint16_t)(0x7e0u+y);
    return clock_interval(33);
}

int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    (void)x;(void)y;(void)arg;
    if (kind==TINYRT_CLOCK_EVENT) ++frame;
    return 0;
}

int32_t tinyrt_render(void) {
    if (draw_clear(0x152535) ||
#ifdef PROBE_LEVEL4
        draw_rgb565(x0,y0,256,120,(const uint8_t *)image,sizeof(image)/2) ||
        draw_rgb565(x0,y0+120,256,120,(const uint8_t *)(image+256*120),sizeof(image)/2) ||
#else
        draw_rgb565(x0,y0,256,240,(const uint8_t *)image,sizeof(image)) ||
#endif
        draw_text(12,y0+12,label,sizeof(label)-1,0xffffff)) return -1;
    for (unsigned i=0;i<RECTS;++i) {
        uint32_t color=(frame&1)?0x437682:0x655283;
        if (draw_rect(x0+(int)(i%15)*16,y0+40+(int)(i/15)*16,12,12,color)) return -1;
    }
    return 0;
}
