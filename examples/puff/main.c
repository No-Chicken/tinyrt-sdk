#include "tinyrt.h"
#include "tinyrt_gfx.h"
#include "assets_generated.h"
#include "puff_input.h"
static puff_blow_t blow_detector;
static const ta_calib_t player_calibration={PUFF_CAL_FLOOR_Q8,PUFF_CAL_REF_Q8,1};
static int mic_running,mic_error,mic_have_stamp;
static uint32_t mic_last_stamp,mic_last_received;

enum { MENU, COUNTDOWN, FLIGHT, WON, LOST, TIMEOUT };
enum { GOAL_HEIGHT = 1800, PLAYER_Y = 324, ROUND_MS = 25000 };
static int state, held, strength_mode = 1, throttle = 70;
static int altitude_q, elapsed, balloons, invulnerable, countdown, pop_timer;
static int pop_x, pop_y, pop_color, shake, finished_ms, struck_lanes;
static uint32_t last_ms, accumulator, animation_ms;
static union { uint32_t aligned; uint8_t bytes[12000]; } commands;
static uint32_t used;
static const int bob[] = {0,1,2,2,3,2,2,1,0,-1,-2,-2,-3,-2,-2,-1};
static const int heights[] = {240,520,800,1080,1360,1600};
static const int phases[] = {136,440,180,30,480,120};
typedef struct { const sprite_t *image; int x,y,w,h,balloon; } part_t;

static part_t player_part(int index, int cx, int cy) {
    int wobble=bob[(animation_ms/90)%16];
    if(index==0)return (part_t){&balloon_red,cx-38+wobble,cy-70,40,54,1};
    if(index==1)return (part_t){&balloon_yellow,cx+2-wobble,cy-73,40,54,2};
    return (part_t){held?&robot1:&robot0,cx-24,cy-5+wobble/2,48,48,0};
}
static int character_x(void) {
    return 233+(shake?bob[(animation_ms/15)%16]:0);
}
static int overlaps(const part_t *a, const part_t *b) {
    int left=a->x>b->x?a->x:b->x, top=a->y>b->y?a->y:b->y;
    int right=a->x+a->w<b->x+b->w?a->x+a->w:b->x+b->w;
    int bottom=a->y+a->h<b->y+b->h?a->y+a->h:b->y+b->h;
    for(int y=top;y<bottom;y++) {
        uint32_t row_a=a->image->opaque_rows[(y-a->y)*a->image->h/a->h];
        uint32_t row_b=b->image->opaque_rows[(y-b->y)*b->image->h/b->h];
        for(int x=left;x<right;x++) {
            unsigned bit_a=(unsigned)((x-a->x)*a->image->w/a->w);
            unsigned bit_b=(unsigned)((x-b->x)*b->image->w/b->w);
            if((row_a&(1u<<bit_a)) && (row_b&(1u<<bit_b)))return 1;
        }
    }
    return 0;
}

static void emit(const void *source, uint32_t count) {
    const uint8_t *data = source;
    if (used + count > sizeof(commands.bytes)) return;
    for (uint32_t i=0; i<count; ++i) commands.bytes[used++] = data[i];
}
static void rectangle(int x, int y, int w, int h, uint32_t rgb, int alpha) {
    uint32_t color=((rgb>>8)&0xf800u)|((rgb>>5)&0x07e0u)|((rgb>>3)&0x001fu);
    tinyrt_gfx_rect_t record={TINYRT_GFX_RECT,sizeof(record),x,y,w,h,color,(uint32_t)alpha};
    emit(&record,sizeof(record));
}
static void sprite(const sprite_t *art, int x, int y, int w, int h, int flip) {
    tinyrt_gfx_sprite_t record={TINYRT_GFX_SPRITE,sizeof(record),x,y,w,h,
        0,(uint32_t)art->u,(uint32_t)art->v,(uint32_t)art->w,(uint32_t)art->h,
        TINYRT_GFX_TRANSPARENT|(flip?TINYRT_GFX_FLIP_X:0),0,0};
    emit(&record,sizeof(record));
}
static void centered(const sprite_t *art, int y) {
    sprite(art,(466-art->w)/2,y,art->w,art->h,0);
}
static const sprite_t *const digits[]={&digit0,&digit1,&digit2,&digit3,&digit4,&digit5,&digit6,&digit7,&digit8,&digit9};
static void number(int value, int center, int y) {
    int values[5], count=0, width=0;
    do { values[count]=value%10; width+=digits[values[count]]->w; count++; value/=10; } while(value && count<5);
    int x=center-width/2;
    while(count) { const sprite_t *art=digits[values[--count]];sprite(art,x,y,art->w,art->h,0);x+=art->w; }
}
static int enemy_x(int lane) {
    int phase=(elapsed*(105+lane*5)/1000+phases[lane])%700;
    int x=phase<350?phase:700-phase;
    return x-117;
}
static part_t enemy_part(int lane) {
    return (part_t){(animation_ms/80)%2?&bee0:&bee1,enemy_x(lane)*2-24,
        PLAYER_Y+altitude_q/1000-heights[lane]-24,48,48,0};
}
static int touched_balloon(const part_t *enemy) {
    for(int index=0;index<3;index++) {
        part_t target=player_part(index,character_x(),PLAYER_Y);
        if(target.balloon && !(balloons&target.balloon))continue;
        if(overlaps(enemy,&target)) {
            if(target.balloon)return target.balloon;
            if((balloons&1) && (enemy->x+enemy->w/2<233 || !(balloons&2)))return 1;
            return 2;
        }
    }
    return 0;
}
static void stop_microphone(void) {
    if(mic_running)mic_close();
    mic_running=0;held=0;throttle=0;
}
static int start_microphone(void) {
    stop_microphone();
    if(mic_open(16000,TINYRT_MIC_FEATURES)!=0)return -1;
    mic_running=1;
    if(mic_gain(12)!=12) {stop_microphone();return -1;}
    puff_blow_init(&blow_detector,&player_calibration);
    mic_have_stamp=0;mic_last_received=now_ms();mic_error=0;
    return 0;
}
static void begin_round(void) {
    if(start_microphone()) {mic_error=1;state=MENU;return;}
    altitude_q=elapsed=invulnerable=pop_timer=shake=0;
    balloons=3;struck_lanes=0;countdown=2700;finished_ms=0;state=COUNTDOWN;
}
static void button(int down, int x, int y) {
    if (!down) { held=0; return; }
    if (state==MENU) {
        if (y>=365 && y<=412) {
            if(x>=120 && x<=230)strength_mode=0;
            else if(x>=236 && x<=346)strength_mode=1;
            return;
        }
        begin_round();held=0;return;
    }
    if (state>=WON) {
        if (animation_ms-(uint32_t)finished_ms<650u) return;
        if(x<139 || x>327 || y<303 || y>346)return;
        begin_round();held=0;return;
    }
}
static void step(void) {
    if (pop_timer>0) pop_timer-=10;
    if (invulnerable>0) invulnerable-=10;
    if (shake>0) shake-=10;
    if (state==COUNTDOWN) {countdown-=10;if(countdown<=0)state=FLIGHT;return;}
    if (state!=FLIGHT) return;
    elapsed+=10;
    int velocity=strength_mode?100+160*throttle/100:180;
    if (held) altitude_q+=velocity*10;
    for (int lane=0;lane<6 && !invulnerable;lane++) {
        if(struck_lanes&(1<<lane))continue;
        part_t enemy=enemy_part(lane);
        int hit=touched_balloon(&enemy);
        if(hit) {
            part_t popped=player_part(hit==1?0:1,character_x(),PLAYER_Y);
            pop_x=popped.x+popped.w/2;pop_y=popped.y+22;
            pop_color=hit==1?0xf9795d:0xffd467;
            balloons&=~hit;struck_lanes|=1<<lane;
            invulnerable=1000;pop_timer=480;shake=180;
            if(!balloons) {state=LOST;finished_ms=(int)animation_ms;}
        }
    }
    int altitude=altitude_q/1000;
    if (state==FLIGHT && altitude>=GOAL_HEIGHT) {state=WON;finished_ms=(int)animation_ms;}
    if (state==FLIGHT && elapsed>=ROUND_MS) {state=TIMEOUT;finished_ms=(int)animation_ms;}
}
static void read_microphone(void) {
    tinyrt_mic_frame_t frames[16];
    int bytes;
    while(mic_running && (bytes=mic_features(frames,sizeof(frames)))>0) {
        mic_last_received=now_ms();
        for(int index=0;index<bytes/(int)sizeof(frames[0]);index++) {
            const tinyrt_mic_frame_t *frame=&frames[index];
            uint32_t delta=mic_have_stamp?frame->t_ms-mic_last_stamp:10u;
            mic_have_stamp=1;mic_last_stamp=frame->t_ms;
            if(delta>250)delta=250;
            held=0;
            while(delta>10) {step();delta-=10;}
            if(state==FLIGHT) {
                ta_output_t output=puff_blow_push(&blow_detector,frame);
                held=output.valid && output.active;
                throttle=output.strength_q15*100/32767;
            }
            step();
            if(state>=WON) {stop_microphone();return;}
        }
    }
    if(!mic_running)return;
    uint32_t stale=now_ms()-mic_last_received;
    if(bytes<0 || stale>500) {
        stop_microphone();mic_error=1;state=MENU;
    } else if(stale>100) {
        held=0;puff_blow_init(&blow_detector,&player_calibration);
    }
}
int32_t tinyrt_init(int32_t width, int32_t height) {
    if(width!=466 || height!=466)return -1;
    uint16_t palette[256];
    if(asset_read(PALETTE_OFFSET,palette,sizeof(palette))!=(int32_t)sizeof(palette))return -1;
    if(gfx_pal_upload(0,0,256,palette))return -1;
    if(gfx_tex_upload(0,TINYRT_GFX_INDEX8,ATLAS_WIDTH,ATLAS_HEIGHT,(const void *)0,
        ATLAS_WIDTH*ATLAS_HEIGHT,TINYRT_GFX_FROM_ASSET))return -1;
    uint32_t inputs=TINYRT_INPUT_LIFECYCLE_MASK;
    if(input_events(inputs))return -1;
    state=MENU;balloons=3;last_ms=now_ms();return clock_interval(33);
}
int32_t tinyrt_event(int32_t kind, int32_t x, int32_t y, int32_t arg) {
    if(kind==TINYRT_TOUCH_PRESS)button(1,x,y);
    else if(kind==TINYRT_TOUCH_RELEASE || kind==TINYRT_TOUCH_CANCEL)button(0,x,y);
    else if(kind==TINYRT_CLOCK_EVENT) {
        uint32_t now=now_ms(),delta=now-last_ms;last_ms=now;
        if(delta>250)delta=250;
        animation_ms+=delta;
        if(mic_running)read_microphone();
        else {
            accumulator+=delta;
            while(accumulator>=10) { accumulator-=10;step(); }
        }
    }
    return 0;
}
static void environment(int altitude) {
    for(int i=0;i<32;i++) {
        uint32_t color=(uint32_t)(153+i*3)<<16|(uint32_t)(201+i*30/31)<<8|(uint32_t)(223-i*13/31);
        rectangle(0,i*15,466,15,color,255);
    }
    sprite(&sun,333,83,64,64,0);
    for(int i=0;i<9;i++) {
        int width=72+(i%3)*26;
        int x=(i*97+36)%490-32;
        int y=(i*73+altitude/5)%570-52;
        sprite(&cloud,x,y,width,width/2,0);
    }
    int ground_y=355+altitude;
    if(ground_y<466) sprite(&ground,0,ground_y,466,70,0);
    int goal_y=PLAYER_Y+altitude-GOAL_HEIGHT-90;
    if(goal_y>-110 && goal_y<466)sprite(&goal,145,goal_y,176,104,0);
}
static void character(int cx, int cy, int visible_balloons) {
    for(int index=0;index<3;index++) {
        part_t part=player_part(index,cx,cy);
        if(part.balloon && !(visible_balloons&part.balloon))continue;
        if(!part.balloon && invulnerable && !(animation_ms/90%2))continue;
        sprite(part.image,part.x,part.y,part.w,part.h,0);
        if(part.balloon)rectangle(part.x+(part.balloon==1?23:14),
            part.y+(part.balloon==1?49:51),2,27,0x6b6e78,255);
    }
}
int32_t tinyrt_render(void) {
    used=0;
    int altitude=altitude_q/1000;
    environment(state==MENU?0:altitude);
    if(state==MENU) {
        centered(&title,94); centered(&subtitle,146);
        character(233,257,3);
        rectangle(139,315,188,43,0x547775,255);centered(&start,323);
        if(mic_error)centered(&microphone_error,422);
        rectangle(120,365,110,47,strength_mode?0x9dbbb9:0xfff5df,255);
        rectangle(236,365,110,47,strength_mode?0xfff5df:0x9dbbb9,255);
        sprite(&mode_fixed,175-mode_fixed.w/2,380,mode_fixed.w,mode_fixed.h,0);
        sprite(&mode_strength,291-mode_strength.w/2,380,mode_strength.w,mode_strength.h,0);
    } else {
        for(int lane=0;lane<6;lane++) {
            part_t enemy=enemy_part(lane);
            if(enemy.y>-50 && enemy.y<490) {
                sprite(enemy.image,enemy.x,enemy.y,enemy.w,enemy.h,0);
            }
        }
        character(character_x(),PLAYER_Y,balloons);
        if(pop_timer>0) {
            int span=(480-pop_timer)/16;
            for(int i=0;i<8;i++) {
                int dx=bob[(i*2+4)%16]*span/3;
                int dy=bob[i*2%16]*span/3;
                rectangle(pop_x+dx,pop_y+dy,4,4,(uint32_t)pop_color,255);
            }
        }
        rectangle(194,39,78,41,0xfff8e5,210);
        number((ROUND_MS-elapsed+999)/1000,233,46);
        // A small destination marker indicates remaining climb.
        rectangle(407,158,4,143,0x7fa8b5,100);
        rectangle(405,295-altitude*130/GOAL_HEIGHT,8,7,0xfff6d9,255);
        rectangle(402,153,13,3,0xf58b67,255);
        if(state==COUNTDOWN) {
            centered(&ready,155);number((countdown+999)/1000,233,190);
        } else if(state==FLIGHT && elapsed<900) centered(&go,161);
        if(state>=WON) {
            rectangle(76,126,314,254,0xfff5e0,255);
            centered(state==WON?&win:state==LOST?&lose:&timeout,155);
            if(state==WON) {
                centered(&time_label,210);number(elapsed/1000,215,239);
                sprite(&decimal,230,239,decimal.w,decimal.h,0);number(elapsed%1000/100,253,239);
            } else centered(&guide,225);
            rectangle(139,303,188,43,0x547775,255);centered(&retry,312);
        }
    }
    if(gfx_begin(0))return -1;
    if(gfx_submit(commands.bytes,used))return -1;
    return gfx_end();
}
