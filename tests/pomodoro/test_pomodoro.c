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
static int present;
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
static int32_t get(void *ctx,uint32_t key,int32_t fallback){(void)ctx;CHECK(key==0);return present?stored:fallback;}
static tinyrt_status_t set(void *ctx,uint32_t key,int32_t value){(void)ctx;CHECK(key==0);stored=value;present=1;writes++;return TINYRT_OK;}
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
static void render(void){static tinyrt_frame_t f;text_count=0;CHECK(tinyrt_runtime_render(runtime,&f)==TINYRT_OK);CHECK(f.count && f.commands[0].kind==TINYRT_DRAW_CLEAR);for(unsigned i=0;i<f.count;i++)if(f.commands[i].kind==TINYRT_DRAW_TEXT)capture(f.commands[i].text,(uint32_t)strlen(f.commands[i].text));}
#else
int32_t kv_get(uint32_t key,int32_t fallback){CHECK(key==0);return present?stored:fallback;}
int32_t kv_set(uint32_t key,int32_t value){CHECK(key==0);stored=value;present=1;writes++;return 0;}
uint32_t now_ms(void){return clock_ms;}
int32_t draw_clear(uint32_t rgb){(void)rgb;return 0;}
int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb){(void)x;(void)y;(void)w;(void)h;(void)rgb;return 0;}
int32_t draw_text(int32_t x,int32_t y,const char *s,uint32_t n,uint32_t rgb){(void)x;(void)y;(void)rgb;capture(s,n);return 0;}
static void close_app(void){}
static void open_app(void){CHECK(tinyrt_init(466,466)==0);}
static void event(int kind,int x,int y){CHECK(tinyrt_event(kind,x,y,0)==0);}
static void render(void){text_count=0;CHECK(tinyrt_render()==0);}
#endif
static void fresh(uint32_t t){close_app();present=0;stored=0;writes=0;clock_ms=t;open_app();}
static void tick(uint32_t t){clock_ms=t;event(2,0,0);}
static void toggle(void){event(1,90,406);}
static void reset(void){event(1,350,406);}
static void expect(const char *time,const char *phase,const char *status){render();CHECK(has(time));CHECK(has(phase));CHECK(has(status));}
static void run_tests(void){
    fresh(100);expect("25:00","WORK","PAUSED");CHECK(writes==0);
    tick(9000);expect("25:00","WORK","PAUSED");CHECK(writes==0);
    event(1,90,20);expect("25:00","WORK","PAUSED");CHECK(writes==0);
    toggle();CHECK(writes==1);expect("25:00","WORK","RUNNING");
    tick(9237);tick(11234);expect("24:58","WORK","RUNNING");CHECK(writes==1);
    clock_ms=12000;toggle();expect("24:57","WORK","PAUSED");CHECK(writes==2);
    clock_ms=0;open_app();expect("24:57","WORK","PAUSED");CHECK(writes==2);
    tick(100000);expect("24:57","WORK","PAUSED");toggle();tick(103000);expect("24:54","WORK","RUNNING");
    unsigned before=writes;reset();CHECK(writes==before+1);expect("25:00","WORK","PAUSED");
    fresh(0);toggle();for(uint32_t i=1;i<=100;i++)tick(i*100);CHECK(writes==1);expect("24:50","WORK","RUNNING");
    tick(59999);CHECK(writes==1);tick(60000);CHECK(writes==2);expect("24:00","WORK","RUNNING");
    tick(75000);close_app();clock_ms=0;open_app();expect("24:00","WORK","PAUSED");CHECK(writes==2);
    fresh(0);toggle();tick(1500000);CHECK(writes==2);expect("05:00","BREAK","PAUSED");
    tick(9999999);expect("05:00","BREAK","PAUSED");CHECK(writes==2);
    toggle();tick(10299999);expect("25:00","WORK","PAUSED");CHECK(writes==4);
    fresh(0);toggle();tick(5000000);expect("05:00","BREAK","PAUSED");CHECK(writes==2);
    fresh(0);toggle();clock_ms=1500000;toggle();expect("05:00","BREAK","PAUSED");CHECK(writes==2);
    fresh(0xfffffff0u);toggle();tick(0x3d8u);expect("24:59","WORK","RUNNING");CHECK(writes==1);
    fresh(0);toggle();tick(59999);clock_ms=60000;toggle();CHECK(writes==2);expect("24:00","WORK","PAUSED");
    /* Malformed version, phase/remaining and reserved bits all recover defaults. */
    const int32_t bad[]={0,-1,0x50000000,0x501fffff,0x50200000,0x503fffff,0x50400001,0x600061a8};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){close_app();stored=bad[i];present=1;writes=0;open_app();expect("25:00","WORK","PAUSED");CHECK(writes==0);}
}
static void run_fast_tests(void){
    fresh(0);expect("00:25","WORK","PAUSED");render();CHECK(has("TEST 25s/5s"));
    toggle();tick(237);tick(2234);expect("00:23","WORK","RUNNING");CHECK(writes==1);
    clock_ms=3000;toggle();expect("00:22","WORK","PAUSED");CHECK(writes==2);
    clock_ms=0;open_app();expect("00:22","WORK","PAUSED");
    toggle();tick(22000);expect("00:05","BREAK","PAUSED");CHECK(writes==4);
    tick(90000);expect("00:05","BREAK","PAUSED");CHECK(writes==4);
    toggle();tick(95000);expect("00:25","WORK","PAUSED");CHECK(writes==6);
    fresh(0xfffffff0u);toggle();tick(0x3d8u);expect("00:24","WORK","RUNNING");CHECK(writes==1);
    reset();expect("00:25","WORK","PAUSED");CHECK(writes==2);
    fresh(0);toggle();tick(90000);expect("00:05","BREAK","PAUSED");CHECK(writes==2);
    close_app();stored=0x5016e360;present=1;writes=0;open_app();expect("00:25","WORK","PAUSED");CHECK(writes==0);
}
int main(int argc,char **argv){
#ifdef TEST_WAMR
    if(argc<2||argc>3){fprintf(stderr,"usage: test_pomodoro app.wasm\n");return 2;}
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);wasm_size=(uint32_t)ftell(f);rewind(f);wasm=malloc(wasm_size);if(!wasm)return 2;CHECK(fread(wasm,1,wasm_size,f)==wasm_size);fclose(f);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
#else
    (void)argc;(void)argv;
#endif
    #ifdef TEST_WAMR
    if(argc==3 && !strcmp(argv[2],"--fast"))run_fast_tests();else run_tests();
#else
    if(argc==2 && !strcmp(argv[1],"--fast"))run_fast_tests();else run_tests();
#endif
    close_app();
#ifdef TEST_WAMR
    tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);CHECK(peak_memory<=2097152u);printf("WAMR peak tracked bytes=%zu\n",peak_memory);free(wasm);
#endif
    printf("pomodoro checks=%u failures=%u writes=%u\n",checks,failures,writes);return failures?1:0;
}
