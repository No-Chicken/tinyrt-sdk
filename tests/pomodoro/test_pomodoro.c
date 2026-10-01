/* Same behavioral scenarios execute against native C and the real TinyRT/WAMR. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TEST_WAMR
#include "tinyrt_runtime.h"
#else
#include "tinyrt.h"
#endif
static unsigned checks, failures, writes;
static uint32_t clock_ms;
static int32_t stored;
static int present, fail_write;
static char texts[32][64];
static unsigned text_count;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static int has(const char *s) { for (unsigned i=0;i<text_count;i++) if (!strcmp(texts[i],s)) return 1; return 0; }
static void capture(const char *s,uint32_t n) { CHECK(text_count<32 && n<64); if(text_count<32 && n<64){memcpy(texts[text_count],s,n);texts[text_count++][n]=0;} }
#ifdef TEST_WAMR
static tinyrt_runtime_t *runtime;
static unsigned char *wasm;
static uint32_t wasm_size;
static size_t peak_memory;
static tinyrt_frame_t frame;
static void safe_point(int32_t x,int32_t y){
    int64_t dx=x-233,dy=y-233;CHECK(dx*dx+dy*dy<=215*215);
    CHECK(!(x>=203 && x<=263 && y>=24 && y<=76));
}
static void check_frame(void){
    unsigned large_time=0;
    CHECK(frame.count<=128 && frame.count>0 && frame.commands[0].kind==TINYRT_DRAW_CLEAR);
    for(unsigned i=1;i<frame.count;i++){
        const tinyrt_draw_command_t *c=&frame.commands[i];
        if(c->kind==TINYRT_DRAW_ARC){
            int64_t dx=c->x-233,dy=c->y-233,room=215-c->radius;
            CHECK(room>=0 && dx*dx+dy*dy<=room*room);
            CHECK(c->y-c->radius>76 || c->x+c->radius<203 || c->x-c->radius>263);
        }else{
            safe_point(c->x,c->y);safe_point(c->x+c->w-1,c->y);
            safe_point(c->x,c->y+c->h-1);safe_point(c->x+c->w-1,c->y+c->h-1);
        }
        if(c->kind==TINYRT_DRAW_TEXT_BOX && strlen(c->text)==5 && c->text[2]==':'){
            CHECK(c->font_px==48 && c->align==1);large_time++;
        }
    }
    CHECK(large_time==1);
}
static int32_t get(void *ctx,uint32_t key,int32_t fallback){(void)ctx;CHECK(key==0);return present?stored:fallback;}
static tinyrt_status_t set(void *ctx,uint32_t key,int32_t value){(void)ctx;CHECK(key==0);if(fail_write)return TINYRT_IO_ERROR;stored=value;present=1;writes++;return TINYRT_OK;}
static uint32_t now(void *ctx){(void)ctx;return clock_ms;}
static void close_app(void){tinyrt_runtime_destroy(runtime);runtime=NULL;}
static void open_app(void){
    tinyrt_package_policy_t p={1,15,2,100000};
    tinyrt_runtime_host_t host={NULL,get,set,now};
    close_app();
    CHECK(tinyrt_runtime_create(wasm,wasm_size,&p,&host,&runtime)==TINYRT_OK);
    if(!runtime){fprintf(stderr,"could not create Wasm app\n");exit(2);}
    CHECK(tinyrt_runtime_init(runtime,466,466)==TINYRT_OK);
    size_t n=tinyrt_runtime_memory_used();if(n>peak_memory)peak_memory=n;
}
static void event(int kind,int x,int y){CHECK(tinyrt_runtime_event(runtime,kind,x,y,0)==TINYRT_OK);}
static int stop_app(void){return tinyrt_runtime_stop(runtime)==TINYRT_OK?0:-1;}
static void render(void){text_count=0;CHECK(tinyrt_runtime_render(runtime,&frame)==TINYRT_OK);check_frame();for(unsigned i=0;i<frame.count;i++)if(frame.commands[i].kind==TINYRT_DRAW_TEXT || frame.commands[i].kind==TINYRT_DRAW_TEXT_BOX)capture(frame.commands[i].text,(uint32_t)strlen(frame.commands[i].text));}
#else
int32_t kv_get(uint32_t key,int32_t fallback){CHECK(key==0);return present?stored:fallback;}
int32_t kv_set(uint32_t key,int32_t value){CHECK(key==0);if(fail_write)return -1;stored=value;present=1;writes++;return 0;}
uint32_t now_ms(void){return clock_ms;}
int32_t draw_clear(uint32_t rgb){(void)rgb;return 0;}
int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb){(void)x;(void)y;(void)w;(void)h;(void)rgb;return 0;}
int32_t draw_text(int32_t x,int32_t y,const char *s,uint32_t n,uint32_t rgb){(void)x;(void)y;(void)rgb;capture(s,n);return 0;}
int32_t draw_round_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t r,uint32_t rgb){(void)x;(void)y;(void)w;(void)h;(void)r;(void)rgb;return 0;}
int32_t draw_arc(int32_t x,int32_t y,int32_t r,int32_t t,int32_t a,int32_t b,uint32_t rgb){(void)x;(void)y;(void)r;(void)t;(void)a;(void)b;(void)rgb;return 0;}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *s,uint32_t n,uint32_t rgb,int32_t font,int32_t align){(void)x;(void)y;(void)w;(void)h;(void)rgb;(void)font;(void)align;capture(s,n);return 0;}
static void close_app(void){}
static void open_app(void){CHECK(tinyrt_init(466,466)==0);}
static void event(int kind,int x,int y){CHECK(tinyrt_event(kind,x,y,0)==0);}
static int stop_app(void){return tinyrt_stop();}
static void render(void){text_count=0;CHECK(tinyrt_render()==0);}
#endif
static void fresh(uint32_t t){close_app();present=0;stored=0;writes=0;fail_write=0;clock_ms=t;open_app();}
static void tick(uint32_t t){clock_ms=t;event(2,0,0);}
static void toggle(void){event(1,200,384);}
static void reset(void){event(1,320,384);}
static void expect(const char *time,const char *phase,const char *status){render();CHECK(has(time));CHECK(has(phase));CHECK(has(status));}
static void run_tests(void){
    fresh(100);expect("25:00","FOCUS","PAUSED");CHECK(writes==0);
    tick(9000);expect("25:00","FOCUS","PAUSED");CHECK(writes==0);
    event(1,90,20);expect("25:00","FOCUS","PAUSED");CHECK(writes==0);
    toggle();CHECK(writes==1);expect("25:00","FOCUS","RUNNING");
    tick(9237);tick(11234);expect("24:58","FOCUS","RUNNING");CHECK(writes==1);
    clock_ms=12000;toggle();expect("24:57","FOCUS","PAUSED");CHECK(writes==2);
    clock_ms=0;open_app();expect("24:57","FOCUS","PAUSED");CHECK(writes==2);
    tick(100000);expect("24:57","FOCUS","PAUSED");toggle();tick(103000);expect("24:54","FOCUS","RUNNING");
    unsigned before=writes;reset();CHECK(writes==before+1);expect("25:00","FOCUS","PAUSED");
    fresh(0);toggle();for(uint32_t i=1;i<=100;i++)tick(i*100);CHECK(writes==1);expect("24:50","FOCUS","RUNNING");
    tick(59999);CHECK(writes==1);tick(60000);CHECK(writes==2);expect("24:00","FOCUS","RUNNING");
    tick(75000);close_app();clock_ms=0;open_app();expect("24:00","FOCUS","PAUSED");CHECK(writes==2);
    fresh(0);toggle();tick(1500000);CHECK(writes==2);expect("05:00","BREAK","PAUSED");CHECK(has("FOCUS DONE"));
    tick(9999999);expect("05:00","BREAK","PAUSED");CHECK(writes==2);
    toggle();tick(10299999);expect("25:00","FOCUS","PAUSED");CHECK(writes==4);CHECK(has("BREAK DONE"));
    fresh(0);toggle();tick(5000000);expect("05:00","BREAK","PAUSED");CHECK(writes==2);
    fresh(0);toggle();clock_ms=1500000;toggle();expect("05:00","BREAK","PAUSED");CHECK(writes==2);
    fresh(0xfffffff0u);toggle();tick(0x3d8u);expect("24:59","FOCUS","RUNNING");CHECK(writes==1);
    fresh(0);toggle();tick(59999);clock_ms=60000;toggle();CHECK(writes==2);expect("24:00","FOCUS","PAUSED");
    /* Malformed version, phase/remaining and reserved bits all recover defaults. */
    const int32_t bad[]={0,-1,0x50000000,0x501fffff,0x50200000,0x503fffff,0x50400001,0x600061a8};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){close_app();stored=bad[i];present=1;writes=0;open_app();expect("25:00","FOCUS","PAUSED");CHECK(writes==0);}
}
static void run_fast_tests(void){
    fresh(0);expect("00:25","FOCUS","PAUSED");render();CHECK(has("TEST 25s/5s"));
    toggle();tick(237);tick(2234);expect("00:23","FOCUS","RUNNING");CHECK(writes==1);
    clock_ms=3000;toggle();expect("00:22","FOCUS","PAUSED");CHECK(writes==2);
    clock_ms=0;open_app();expect("00:22","FOCUS","PAUSED");
    toggle();tick(22000);expect("00:05","BREAK","PAUSED");CHECK(writes==4);CHECK(has("FOCUS DONE"));
    tick(90000);expect("00:05","BREAK","PAUSED");CHECK(writes==4);
    toggle();tick(95000);expect("00:25","FOCUS","PAUSED");CHECK(writes==6);CHECK(has("BREAK DONE"));
    fresh(0xfffffff0u);toggle();tick(0x3d8u);expect("00:24","FOCUS","RUNNING");CHECK(writes==1);
    reset();expect("00:25","FOCUS","PAUSED");CHECK(writes==2);
    fresh(0);toggle();tick(90000);expect("00:05","BREAK","PAUSED");CHECK(writes==2);
    close_app();stored=0x5016e360;present=1;writes=0;open_app();expect("00:25","FOCUS","PAUSED");CHECK(writes==0);
}
static void run_stop_tests(int fast){
    fresh(1000);toggle();tick(2234);clock_ms=3456;
    CHECK(stop_app()==0);CHECK(writes==2);CHECK(((uint32_t)stored&0x1fffffu)==(fast?22544u:1497544u));
    close_app();clock_ms=0;open_app();expect(fast?"00:23":"24:58","FOCUS","PAUSED");
    tick(100000);expect(fast?"00:23":"24:58","FOCUS","PAUSED");CHECK(writes==2);
    CHECK(stop_app()==0);CHECK(writes==3);close_app();
    fresh(0);toggle();clock_ms=fast?25000u:1500000u;CHECK(stop_app()==0);
    close_app();clock_ms=0;open_app();expect(fast?"00:05":"05:00","BREAK","PAUSED");
    toggle();clock_ms=fast?5000u:300000u;CHECK(stop_app()==0);close_app();
    clock_ms=0;open_app();expect(fast?"00:25":"25:00","FOCUS","PAUSED");
    fresh(0xfffffff0u);toggle();clock_ms=0x3d8u;CHECK(stop_app()==0);
    CHECK(((uint32_t)stored&0x1fffffu)==(fast?24000u:1499000u));
    fresh(0);toggle();int32_t before=stored;clock_ms=999;fail_write=1;
    CHECK(stop_app()!=0);CHECK(stored==before && writes==1);
    close_app();fail_write=0;clock_ms=0;open_app();expect(fast?"00:25":"25:00","FOCUS","PAUSED");
}
#ifdef TEST_WAMR
static void save_frame(const char *directory,const char *name){
    char path[1024];int n=snprintf(path,sizeof(path),"%s/%s.bin",directory,name);CHECK(n>0 && (size_t)n<sizeof(path));
    render();FILE *out=fopen(path,"wb");CHECK(out!=NULL);if(!out)return;
    CHECK(fwrite(&frame,sizeof(frame),1,out)==1);CHECK(fclose(out)==0);
    printf("frame=%s bytes=%zu commands=%u\n",path,sizeof(frame),frame.count);
}
static void export_frames(const char *directory,int fast){
    CHECK(sizeof(frame)==14340);
    fresh(0);save_frame(directory,"ready");toggle();tick(fast?3000u:62000u);save_frame(directory,"running");
    clock_ms=fast?4234u:63456u;CHECK(stop_app()==0);close_app();clock_ms=0;open_app();save_frame(directory,"restored");
    fresh(0);toggle();tick(fast?25000u:1500000u);save_frame(directory,"break");
}
#endif
int main(int argc,char **argv){
#ifdef TEST_WAMR
    if(argc<2){fprintf(stderr,"usage: test_pomodoro app.wasm [--fast] [--frames existing-directory]\n");return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);wasm_size=(uint32_t)ftell(f);rewind(f);wasm=malloc(wasm_size);if(!wasm)return 2;CHECK(fread(wasm,1,wasm_size,f)==wasm_size);fclose(f);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
#else
    (void)argc;(void)argv;
#endif
    int fast=0;
#ifdef TEST_WAMR
    const char *frames=NULL;
    for(int i=2;i<argc;i++){
        if(!strcmp(argv[i],"--fast"))fast=1;
        else if(!strcmp(argv[i],"--frames") && i+1<argc)frames=argv[++i];
        else{fprintf(stderr,"unknown or incomplete argument: %s\n",argv[i]);return 2;}
    }
#else
    fast=argc==2 && !strcmp(argv[1],"--fast");
#endif
    if(fast)run_fast_tests();else run_tests();
    run_stop_tests(fast);
#ifdef TEST_WAMR
    if(frames)export_frames(frames,fast);
#endif
    close_app();
#ifdef TEST_WAMR
    tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);CHECK(peak_memory<=2097152u);printf("WAMR peak tracked bytes=%zu\n",peak_memory);free(wasm);
#endif
    printf("pomodoro checks=%u failures=%u writes=%u\n",checks,failures,writes);return failures?1:0;
}
