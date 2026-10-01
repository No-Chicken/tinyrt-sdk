#include "tinyrt.h"
int32_t tinyrt_init(int32_t width,int32_t height){(void)width;(void)height;return 0;}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){(void)kind;(void)x;(void)y;(void)arg;return 0;}
int32_t tinyrt_render(void){draw_clear(0x182630);return draw_text(40,100,"Hello TinyRT",12,0xffffff);}
