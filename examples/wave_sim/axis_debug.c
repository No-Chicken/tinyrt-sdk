#include "tinyrt.h"
static int32_t ax,ay,az;
static uint32_t samples;
static uint32_t text(char *out,const char *label,int32_t value) {
    uint32_t n=0;while(*label)out[n++]=*label++;
    if(value<0){out[n++]='-';value=-value;}
    char digits[10];unsigned count=0;
    do{digits[count++]=(char)('0'+value%10);value/=10;}while(value);
    while(count)out[n++]=digits[--count];out[n++]=' ';out[n++]='m';out[n++]='g';return n;
}
int32_t tinyrt_init(int32_t w,int32_t h){
    if(w!=466||h!=466)return -1;ax=ay=az=0;samples=0;
    if(input_events(TINYRT_IN_MOTION)!=0)return -1;return clock_interval(100);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){if(kind==TINYRT_EV_MOTION){ax=x;ay=y;az=arg;samples++;}return 0;}
int32_t tinyrt_render(void){
    if(draw_clear(0x060a1a)!=0)return -1;
    if(draw_text_box(83,90,300,36,"RAW SENSOR AXES",15,0xa8f0ff,24,1)!=0)return -1;
    char line[24];int32_t values[3]={ax,ay,az};const char *labels[3]={"AX ","AY ","AZ "};
    for(int i=0;i<3;i++){uint32_t n=text(line,labels[i],values[i]);if(draw_text_box(83,155+i*50,300,42,line,n,0xffffff,36,1)!=0)return -1;}
    const char *status=samples?"Tilt right, then toward you":"Waiting for motion events";
    unsigned n=0;while(status[n])n++;
    return draw_text_box(53,330,360,36,status,n,0xa8f0ff,18,1);
}
int32_t tinyrt_stop(void){return 0;}
