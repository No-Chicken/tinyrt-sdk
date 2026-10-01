#include "tinyrt.h"
#ifdef POMODORO_FAST
#define WORK_MS 25000u
#define BREAK_MS 5000u
#define SAVE_TAG 0x60000000u
#else
#define WORK_MS 1500000u
#define BREAK_MS 300000u
#define SAVE_TAG 0x50000000u
#endif
#define REMAIN_MASK 0x001fffffu
#define PHASE_BIT 0x00200000u
#define CHECKPOINT_MS 60000u
static int32_t width,height;
static uint32_t remaining,last_ms,checkpoint_ms,phase;
static int running;
static uint32_t duration(void){return phase?BREAK_MS:WORK_MS;}
/* One i32 snapshot avoids partially updated phase/remaining pairs. No clock
 * deadline or running flag is persisted: reopening always resumes paused. */
static int32_t save(void){return kv_set(0,(int32_t)(SAVE_TAG|(phase?PHASE_BIT:0u)|remaining));}
static int advance(uint32_t now){
    uint32_t elapsed=now-last_ms;
    last_ms=now;
    if(!running)return 0;
    if(elapsed>=remaining){phase^=1u;remaining=duration();running=0;checkpoint_ms=0;return 1;}
    remaining-=elapsed;checkpoint_ms+=elapsed;return 0;
}
int32_t tinyrt_init(int32_t w,int32_t h){
    if(w<160||h<240)return -1;
    width=w;height=h;phase=0;remaining=WORK_MS;running=0;checkpoint_ms=0;last_ms=now_ms();
    uint32_t saved=(uint32_t)kv_get(0,0);
    if((saved&~(REMAIN_MASK|PHASE_BIT))==SAVE_TAG){
        uint32_t saved_phase=(saved&PHASE_BIT)?1u:0u;
        uint32_t saved_remaining=saved&REMAIN_MASK;
        if(saved_remaining>0&&saved_remaining<=(saved_phase?BREAK_MS:WORK_MS)){
            phase=saved_phase;remaining=saved_remaining;
        }
    }
    return 0;
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){
    (void)arg;
    if(kind==TINYRT_CLOCK_EVENT){
        int changed=advance(now_ms());
        if(changed||(running&&checkpoint_ms>=CHECKPOINT_MS)){checkpoint_ms=0;return save();}
    }else if(kind==TINYRT_TOUCH_RELEASE&&x>=20&&x<width-20&&y>=height-100&&y<height-30){
        if(x>=width/2){phase=0;remaining=WORK_MS;running=0;}
        else if(running){(void)advance(now_ms());running=0;}
        else{running=1;last_ms=now_ms();}
        checkpoint_ms=0;return save();
    }
    return 0;
}
static void digit(int value,int32_t x,int32_t y){
    static const uint8_t segments[10]={0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f};
    static const int8_t positions[7][4]={{7,0,28,6},{35,7,6,28},{35,42,6,28},{7,70,28,6},{0,42,6,28},{0,7,6,28},{7,35,28,6}};
    for(unsigned i=0;i<7;i++)if(segments[value]&(1u<<i))draw_rect(x+positions[i][0],y+positions[i][1],positions[i][2],positions[i][3],0xffffff);
}
int32_t tinyrt_render(void){
    uint32_t seconds=(remaining+999u)/1000u;
    int digits[4]={(int)(seconds/600u),(int)(seconds/60u%10u),(int)(seconds%60u/10u),(int)(seconds%10u)};
    char time[5]={(char)('0'+digits[0]),(char)('0'+digits[1]),':',(char)('0'+digits[2]),(char)('0'+digits[3])};
    draw_clear(0x172329);
#ifdef POMODORO_FAST
    draw_text(30,35,"TEST 25s/5s",11,0xffc857);
#else
    draw_text(30,35,"POMODORO",8,0xffffff);
#endif
    draw_text(30,70,phase?"BREAK":"WORK",phase?5:4,phase?0x80d8b0:0xffc857);
    draw_text(30,100,running?"RUNNING":"PAUSED",running?7:6,0xc4cdd3);
    draw_text(width/2-20,height/2-65,time,5,0xc4cdd3);
    int32_t left=width/2-103,top=height/2-30;
    for(int i=0;i<4;i++)digit(digits[i],left+i*49+(i>=2?12:0),top);
    draw_rect(left+96,top+22,5,5,0xffffff);draw_rect(left+96,top+49,5,5,0xffffff);
    draw_rect(30,height-140,width-60,8,0x3b4a52);
    uint32_t complete=duration()-remaining;
    int32_t progress=(int32_t)(complete/(duration()/100u));
    if(progress>0)draw_rect(30,height-140,(width-60)*progress/100,8,0x80d8b0);
    draw_rect(20,height-100,width/2-25,70,0x326b57);draw_rect(width/2+5,height-100,width/2-25,70,0x445361);
    draw_text(40,height-74,running?"PAUSE":"START",5,0xffffff);draw_text(width/2+25,height-74,"RESET",5,0xffffff);
    return 0;
}
