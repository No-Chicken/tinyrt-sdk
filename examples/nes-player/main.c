/* Mapper-0 player: resources own the ROM; the guest only supplies scheduling/input. */
#include "tinyrt.h"
#include "engine.h"
static uint32_t active, shown, pointer, physical, started, dirty, larger;
static unsigned lines_per_event;
static int hit(int x,int y,int left,int top,int w,int h) {
    return x>=left&&x<left+w&&y>=top&&y<top+h;
}
static void update_pad(void) { nes_player_buttons(pointer|physical); }
int32_t tinyrt_init(int32_t width,int32_t height) {
    if(width!=466||height!=466)return -1;
    active=dirty=1;shown=pointer=physical=started=larger=0;
    lines_per_event=runtime_backend()==1?64:3;
    if(input_events(TINYRT_INPUT_LIFECYCLE_MASK|TINYRT_INPUT_USER_KEY_MASK))return -1;
    if(nes_player_start())return -1;
    return clock_interval(1);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    if(!active)return 0;
    if(kind==TINYRT_CLOCK_EVENT) {
        nes_player_status_t before=nes_player_status();
        if(before.scanline==0)started=(uint32_t)arg;
        for(unsigned i=0;i<lines_per_event;i++) {
            if(nes_player_step()==2)return -1;
            if(nes_player_status().frame!=before.frame) {
                uint32_t elapsed=now_ms()-started;
                return clock_interval(elapsed<16?16-(int32_t)elapsed:1);
            }
        }
        return clock_interval(1);
    }
    if(kind==TINYRT_USER_KEY_EVENT) {
        physical=y?NES_PAD_A:0;update_pad();return 0;
    }
    if(kind==TINYRT_TOUCH_CANCEL) { pointer=0;update_pad();return 0; }
    if(kind==TINYRT_TOUCH_RELEASE) { pointer=0;update_pad();return 0; }
    if(kind!=TINYRT_TOUCH_PRESS&&kind!=TINYRT_TOUCH_MOVE)return 0;
    if(kind==TINYRT_TOUCH_PRESS&&hit(x,y,283,44,46,26)) {
        larger=!larger;dirty=1;return 0;
    }
    uint32_t buttons=0;
    if(hit(x,y,117,366,48,30))buttons=NES_PAD_UP;
    else if(hit(x,y,171,366,48,30))buttons=NES_PAD_DOWN;
    else if(hit(x,y,247,366,84,30))buttons=NES_PAD_SELECT;
    else if(hit(x,y,117,400,48,36))buttons=NES_PAD_LEFT;
    else if(hit(x,y,171,400,48,36))buttons=NES_PAD_RIGHT;
    else if(hit(x,y,247,400,48,36))buttons=NES_PAD_B;
    else if(hit(x,y,301,400,48,36))buttons=NES_PAD_A;
    else if(hit(x,y,99,44,63,26))buttons=NES_PAD_START;
    pointer=buttons;update_pad();return 0;
}
static int button(int x,int y,int w,int h,const char* text,int len,uint32_t pressed) {
    return draw_round_rect(x,y,w,h,12,pressed?0x389bff:0x263747)||
           draw_text_box(x,y,w,h,text,len,0xffffff,18,1);
}
int32_t tinyrt_render(void) {
    nes_player_status_t state=nes_player_status();
    if(!dirty&&state.frame==shown)return draw_skip();
    if(draw_clear(0x080f19))return -1;
    if(state.frame<4) {
        if(draw_text_box(83,200,300,50,"Loading cartridge",17,0xffffff,24,1))return -1;
    } else {
        /* 338 x 317 has diagonal <466: complete source image fits the circle. */
        int w=larger?410:338,h=larger?384:317;
        if(draw_rgb565_scaled((466-w)/2,(466-h)/2,w,h,256,240,
                             (const uint8_t*)nes_player_pixels(),122880))return -1;
        if(button(99,44,63,26,"START",5,pointer&NES_PAD_START)||
           button(247,366,84,30,"SELECT",6,pointer&NES_PAD_SELECT)||
           button(117,366,48,30,"^",1,pointer&NES_PAD_UP)||
           button(171,366,48,30,"v",1,pointer&NES_PAD_DOWN)||
           button(283,44,46,26,larger?"1X":"+",larger?2:1,0)||
           button(117,400,48,36,"<",1,pointer&NES_PAD_LEFT)||
           button(171,400,48,36,">",1,pointer&NES_PAD_RIGHT)||
           button(247,400,48,36,"B",1,pointer&NES_PAD_B)||
           button(301,400,48,36,"A",1,(pointer|physical)&NES_PAD_A))return -1;
        if(draw_text_box(156,441,154,18,"KEY1: A",7,0x9facbb,18,1))return -1;
    }
    dirty=0;shown=state.frame;return 0;
}
int32_t tinyrt_stop(void) { active=pointer=physical=0;update_pad();return 0; }
