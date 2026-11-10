#include "tinyrt.h"

static uint32_t sequence;
static uint32_t previous;
static char message[64];

static unsigned decimal(char *out,uint32_t value)
{
    char digits[10];
    unsigned n=0;
    do { digits[n++]=(char)('0'+value%10); value/=10; } while(value);
    for(unsigned i=0;i<n;i++) out[i]=digits[n-1-i];
    return n;
}

int32_t tinyrt_init(int32_t w,int32_t h)
{
    tinyrt_sys_info_t info;
    if(sys_info(&info,sizeof(info)-1)!=TINYRT_ERR_UNAVAILABLE ||
       sys_info(&info,sizeof(info))!=0 || info.screen_w!=(uint32_t)w ||
       info.screen_h!=(uint32_t)h || info.api_level<2 ||
       info.safe_x+info.safe_w>info.screen_w || info.safe_y+info.safe_h>info.screen_h ||
       !info.safe_w || !info.safe_h || info.mem_pages_max<2 || info.budget_max<100000)
        return -1;
    previous=random_u32();
    return clock_interval(500);
}

int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg)
{
    (void)x;(void)y;(void)arg;
    if(kind!=TINYRT_CLOCK_EVENT) return 0;
    unsigned start=now_ms();
    volatile float f=0.0f;
    volatile double d=0.0;
    for(unsigned i=0;i<512;i++) { f+=0.25f; d+=0.25; }
    if(f!=128.0f || d!=128.0) return -1;
    unsigned elapsed=now_ms()-start;
    uint32_t next=random_u32();
    const char prefix[]="probe seq=";
    unsigned n=0;
    while(n<sizeof(prefix)-1) {message[n]=prefix[n];n++;}
    n+=decimal(message+n,++sequence);
    const char timing[]=" float_double_ms=";
    for(unsigned i=0;i<sizeof(timing)-1;i++) message[n++]=timing[i];
    n+=decimal(message+n,elapsed);
    message[n++]=' ';message[n++]=(next!=previous?'Y':'N');
    previous=next;
    return debug_log(message,n);
}

int32_t tinyrt_render(void) {return draw_clear(0x123c28);}
