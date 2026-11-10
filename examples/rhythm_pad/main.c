#include "tinyrt.h"

static int16_t pcm[640];
static int32_t center;
static int feedback;

int32_t tinyrt_init(int32_t width,int32_t height) {
    (void)height;
    center=width/2;
    for (unsigned i=0;i<640;++i) pcm[i]=(i%32)<16 ? 3072 : -3072;
    return input_events(0);
}

int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg) {
    (void)y;(void)arg;
    if (kind!=TINYRT_TOUCH_RELEASE) return 0;
    static const uint16_t steps[]={45,65,90};
    if (x<center) {
        feedback=audio_queue_free()>=(int32_t)sizeof(pcm) &&
                 audio_write(pcm,sizeof(pcm))==(int32_t)sizeof(pcm);
    } else {
        feedback=audio_tone(880,120)==0;
    }
    if (haptic_pattern(steps,3)!=0) feedback=0;
    return 0;
}

int32_t tinyrt_render(void) {
    if (draw_clear(0x172735)) return -1;
    if (draw_text(center-88,180,"PCM",3,0xe8e8e8)) return -1;
    if (draw_text(center+30,180,"Tone",4,0xe8e8e8)) return -1;
    return draw_text(center-40,250,feedback?"Played":"Tap a pad",feedback?6:9,0xf8c86b);
}
