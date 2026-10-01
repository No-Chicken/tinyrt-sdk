/* The same public ABI scenarios run native C and the shipped Wasm in WAMR. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef TEST_WAMR
#include "tinyrt_runtime.h"
#else
#include "tinyrt.h"
#endif

enum { CLEAR, RECT, ROUND, TEXT };
enum { EMPTY, BODY, HEAD, FOOD };
typedef struct {
    int kind, x, y, w, h;
    uint32_t color;
    char text[64];
} command_t;
static command_t commands[128];
static unsigned count, peak_commands, checks, failures, writes;
static uint32_t clock_ms;
static int32_t stored;
static int present, fail_write;
static int cells[80], head_x, head_y, food_x, food_y, length;
#define CHECK(x) do { checks++; if (!(x)) { failures++; if(failures<40) printf("FAIL line %d: %s\n",__LINE__,#x); } } while (0)

static void record(int kind,int x,int y,int w,int h,uint32_t color,const char *text,uint32_t n) {
    CHECK(count<128 && n<64);
    if(count>=128 || n>=64) return;
    command_t *c=&commands[count++];
    c->kind=kind;c->x=x;c->y=y;c->w=w;c->h=h;c->color=color;
    if(n) memcpy(c->text,text,n);
    c->text[n]=0;
}
static int has(const char *s) {
    for(unsigned i=0;i<count;i++) if(!strcmp(commands[i].text,s)) return 1;
    return 0;
}
static void safe_point(int x,int y) {
    int64_t dx=x-233,dy=y-233;
    CHECK(dx*dx+dy*dy<=215*215);
    CHECK(y>76);
}
static void inspect_frame(void) {
    CHECK(count>0 && count<=128 && commands[0].kind==CLEAR);
    if(count>peak_commands) peak_commands=count;
    memset(cells,0,sizeof(cells));
    head_x=head_y=food_x=food_y=-1;length=0;
    unsigned heads=0,foods=0;
    for(unsigned i=1;i<count;i++) {
        command_t *c=&commands[i];
        CHECK(c->w>0 && c->h>0);
        safe_point(c->x,c->y);safe_point(c->x+c->w-1,c->y);
        safe_point(c->x,c->y+c->h-1);safe_point(c->x+c->w-1,c->y+c->h-1);
        if((c->kind==RECT || c->kind==ROUND) && c->w==18 && c->h==18) {
            int x=(c->x-125)/22,y=(c->y-138)/22;
            CHECK(c->x==125+x*22 && c->y==138+y*22 && x>=0 && x<10 && y>=0 && y<8);
            if(x<0 || x>=10 || y<0 || y>=8) continue;
            int k=c->color==0xe8ac59u?FOOD:c->color==0xf5efdfu?HEAD:BODY;
            CHECK(cells[y*10+x]==EMPTY);cells[y*10+x]=k;
            if(k==HEAD) { heads++;head_x=x;head_y=y;length++; }
            else if(k==FOOD) { foods++;food_x=x;food_y=y; }
            else { CHECK(c->color==0x6cbb8bu);length++; }
        }
    }
    CHECK(heads==1 && length>=3 && length<=80);
    CHECK(foods==(length==80?0u:1u));
}

#ifdef TEST_WAMR
static tinyrt_runtime_t *runtime;
static unsigned char *wasm;
static uint32_t wasm_size;
static size_t peak_memory;
static tinyrt_frame_t frame;
static int32_t get(void *ctx,uint32_t key,int32_t fallback) {
    (void)ctx;CHECK(key==0);return present?stored:fallback;
}
static tinyrt_status_t set(void *ctx,uint32_t key,int32_t value) {
    (void)ctx;CHECK(key==0);
    if(fail_write) return TINYRT_IO_ERROR;
    stored=value;present=1;writes++;return TINYRT_OK;
}
static uint32_t now(void *ctx) { (void)ctx;return clock_ms; }
static void close_app(void) { tinyrt_runtime_destroy(runtime);runtime=NULL; }
static void open_app(void) {
    const tinyrt_package_policy_t policy={1,15,2,100000};
    const tinyrt_runtime_host_t host={NULL,get,set,now};
    close_app();
    CHECK(tinyrt_runtime_create(wasm,wasm_size,&policy,&host,&runtime)==TINYRT_OK);
    if(!runtime) { fprintf(stderr,"could not create Snake Wasm\n");exit(2); }
    CHECK(tinyrt_runtime_init(runtime,466,466)==TINYRT_OK);
    size_t bytes=tinyrt_runtime_memory_used();if(bytes>peak_memory)peak_memory=bytes;
}
static int raw_event(int kind,int x,int y) {
    tinyrt_status_t result=tinyrt_runtime_event(runtime,kind,x,y,0);
    if(result!=TINYRT_OK && !fail_write)
        printf("WAMR event kind=%d x=%d y=%d time=%u error=%s\n",kind,x,y,clock_ms,tinyrt_runtime_last_error(runtime));
    return result==TINYRT_OK?0:-1;
}
static int stop_app(void) { return tinyrt_runtime_stop(runtime)==TINYRT_OK?0:-1; }
static void render(void) {
    count=0;CHECK(tinyrt_runtime_render(runtime,&frame)==TINYRT_OK);
    for(unsigned i=0;i<frame.count;i++) {
        const tinyrt_draw_command_t *c=&frame.commands[i];
        int k=c->kind==TINYRT_DRAW_CLEAR?CLEAR:c->kind==TINYRT_DRAW_RECT?RECT:
              c->kind==TINYRT_DRAW_ROUND_RECT?ROUND:TEXT;
        CHECK(k!=TEXT || c->kind==TINYRT_DRAW_TEXT_BOX);
        record(k,c->x,c->y,c->w,c->h,c->rgb,c->text,(uint32_t)strlen(c->text));
    }
    inspect_frame();
}
#else
int32_t kv_get(uint32_t key,int32_t fallback) { CHECK(key==0);return present?stored:fallback; }
int32_t kv_set(uint32_t key,int32_t value) {
    CHECK(key==0);if(fail_write)return -1;stored=value;present=1;writes++;return 0;
}
uint32_t now_ms(void) { return clock_ms; }
int32_t draw_clear(uint32_t rgb) { record(CLEAR,0,0,466,466,rgb,"",0);return 0; }
int32_t draw_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t rgb) {
    record(RECT,x,y,w,h,rgb,"",0);return 0;
}
int32_t draw_round_rect(int32_t x,int32_t y,int32_t w,int32_t h,int32_t radius,uint32_t rgb) {
    CHECK(radius>=0 && radius<=w/2 && radius<=h/2);record(ROUND,x,y,w,h,rgb,"",0);return 0;
}
int32_t draw_text_box(int32_t x,int32_t y,int32_t w,int32_t h,const char *s,uint32_t n,uint32_t rgb,int32_t font,int32_t align) {
    CHECK((font==18 || font==24) && align==1);record(TEXT,x,y,w,h,rgb,s,n);return 0;
}
static void close_app(void) {}
static void open_app(void) { CHECK(tinyrt_init(466,466)==0); }
static int raw_event(int kind,int x,int y) { return tinyrt_event(kind,x,y,0); }
static int stop_app(void) { return tinyrt_stop(); }
static void render(void) { count=0;CHECK(tinyrt_render()==0);inspect_frame(); }
#endif

static void fresh(uint32_t time) {
    close_app();clock_ms=time;present=0;stored=0;writes=0;fail_write=0;open_app();
}
static void event(int kind,int x,int y) { CHECK(raw_event(kind,x,y)==0); }
static void tick(uint32_t time) { clock_ms=time;event(2,0,0); }
static void toggle(void) { event(1,147,342); }
static void reset(void) { event(1,319,342); }
enum { RIGHT, DOWN, LEFT, UP };
static void turn(int direction) {
    const int x[]={289,233,177,233};
    const int y[]={396,396,396,340};
    event(1,x[direction],y[direction]);
}
static void step(void) { tick(clock_ms+400u);render(); }
static void expect_head(int x,int y) { render();CHECK(head_x==x && head_y==y); }
static void expect_score(unsigned score,unsigned best) {
    char s[32];snprintf(s,sizeof(s),"SCORE %02u   BEST %02u",score,best);render();CHECK(has(s));
}
static void eat_first(void) {
    toggle();step();step();step();CHECK(head_x==7 && head_y==3 && length==4);expect_score(1,1);
}

static void run_timing_and_controls(void) {
    fresh(100);render();CHECK(has("READY") && has("START"));CHECK(writes==0);
    CHECK(head_x==4 && head_y==3 && food_x==7 && food_y==3 && length==3);
    tick(9000);expect_head(4,3);
    event(1,233,50);event(1,90,342);event(1,209,316);event(1,209,317);
    render();CHECK(has("READY"));expect_head(4,3);
    turn(LEFT);turn(UP);render();CHECK(has("READY"));
    toggle();render();CHECK(has("PAUSE") && !has("READY"));
    tick(9199);expect_head(4,3);tick(9399);expect_head(4,3);tick(9400);expect_head(5,3);
    tick(9801);expect_head(6,3);tick(10199);expect_head(6,3);tick(10200);expect_head(7,3);
    CHECK(length==4);expect_score(1,1);CHECK(writes==1);
    clock_ms=10350;toggle();render();CHECK(has("PAUSED") && has("RESUME"));
    tick(999999);expect_head(7,3);turn(UP);expect_head(7,3);
    toggle();tick(1000198);expect_head(7,3);tick(1000199);expect_head(7,3);
    tick(1000399);expect_head(8,3); /* resume starts a new full movement interval */
    reset();render();CHECK(has("READY"));CHECK(length==3);expect_score(0,1);
    CHECK(writes==1);CHECK(stop_app()==0);CHECK(writes==1);

    fresh(0xfffffff0u);toggle();tick(0x17fu);expect_head(4,3);tick(0x180u);expect_head(5,3);
    fresh(0);toggle();tick(1000000);expect_head(6,3); /* at most two catch-up moves */
    tick(1000001);expect_head(6,3);tick(1000399);expect_head(6,3);tick(1000400);expect_head(7,3);
    fresh(0);toggle();turn(LEFT);step();CHECK(head_x==5 && head_y==3); /* reverse ignored */
    turn(UP);turn(LEFT);step();CHECK(head_x==5 && head_y==2); /* one queued turn per step */
    turn(LEFT);step();CHECK(head_x==4 && head_y==2);
    event(1,50,200);expect_head(4,2);
    fresh(0);toggle();clock_ms=399;turn(UP);tick(400);expect_head(4,2); /* release before a deadline */
    fresh(0);toggle();clock_ms=800;turn(UP);expect_head(6,3);
    tick(1200);expect_head(6,2); /* late touch cannot redirect past elapsed moves */
}

static void run_collisions_and_save(void) {
    fresh(0);eat_first();
    turn(DOWN);step();turn(LEFT);step();turn(UP);step();
    CHECK(head_x==6 && head_y==3 && !has("GAME OVER")); /* vacating tail is legal */
    turn(RIGHT);step();CHECK(!has("GAME OVER"));
    fresh(0);toggle();for(unsigned i=0;i<6;i++)step();
    CHECK(has("GAME OVER") && has("START"));CHECK(head_x==9 && head_y==3);expect_score(1,1);
    turn(UP);tick(99999);expect_head(9,3);CHECK(has("GAME OVER"));
    toggle();expect_head(4,3);CHECK(has("PAUSE")); /* START after collision restarts */
    reset();CHECK(stop_app()==0);close_app();clock_ms=0;open_app();expect_score(0,1);
    CHECK(has("READY"));CHECK(writes==1);
    const int32_t bad[]={0,-1,0x53000050,0x53000101,0x52000001};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        close_app();stored=bad[i];present=1;writes=0;open_app();expect_score(0,0);CHECK(writes==0);
    }
    close_app();stored=0x5300004d;present=1;open_app();expect_score(0,77);
    toggle();step();step();step();CHECK(writes==0);CHECK(stop_app()==0);CHECK(writes==0);
    /* Save failure is surfaced to the host; the previously committed best survives. */
    fresh(0);toggle();step();step();fail_write=1;clock_ms+=400;
    CHECK(raw_event(2,0,0)!=0);CHECK(writes==0 && !present);
    close_app();fail_write=0;clock_ms=0;open_app();expect_score(0,0);
    fresh(0);toggle();step();step();clock_ms+=400;CHECK(stop_app()==0);
    close_app();clock_ms=0;open_app();expect_score(0,1); /* stop captures the final elapsed move */
    fresh(0);toggle();step();step();clock_ms+=400;fail_write=1;
    CHECK(stop_app()!=0);CHECK(!present && writes==0);
    close_app();fail_write=0;open_app();expect_score(0,0);
}

/* A public-input player follows a Hamiltonian circuit, so every food is reachable
 * without shortcuts or editing guest memory, even when all 80 cells are occupied. */
static int circuit_turn(int x,int y) {
    if(y==0) return x==9?DOWN:RIGHT;
    if(x==0) return UP;
    if(y%2) return x==1?(y==7?LEFT:DOWN):LEFT;
    return x==9?DOWN:RIGHT;
}
static unsigned play_until(unsigned target) {
    unsigned moves=0;
    while(length<(int)target && moves<7000 && !has("GAME OVER") && !failures) {
        turn(circuit_turn(head_x,head_y));step();moves++;
    }
    CHECK(length==(int)target);return moves;
}
static void enter_circuit(void) {
    fresh(0);toggle();turn(UP);step();step();step();CHECK(head_x==4 && head_y==0);
}
static void run_full_board_and_body_collision(void) {
    enter_circuit();unsigned moves=play_until(80);
    CHECK(has("YOU WIN") && has("START"));expect_score(77,77);CHECK(writes==77);
    tick(clock_ms+1000000);render();CHECK(length==80 && has("YOU WIN"));
    CHECK(stop_app()==0);close_app();clock_ms=0;open_app();expect_score(0,77);
    printf("full-board circuit moves=%u\n",moves);

    /* Find an occupied neighbour other than the neck or vacating tail, then hit it.
     * The expected collision is derived from actual rendered board history. */
    enter_circuit();play_until(24);
    int collided=0;
    for(unsigned tries=0;tries<200 && !collided && !failures;tries++) {
        int x=head_x,y=head_y,d=circuit_turn(x,y);
        turn(d);step(); /* ensures the neck is the previous observed head */
        int nx[]={head_x+1,head_x,head_x-1,head_x};
        int ny[]={head_y,head_y+1,head_y,head_y-1};
        for(int candidate=0;candidate<4;candidate++) {
            if(nx[candidate]<0 || nx[candidate]>9 || ny[candidate]<0 || ny[candidate]>7)continue;
            if(nx[candidate]==x && ny[candidate]==y)continue;
            if(cells[ny[candidate]*10+nx[candidate]]!=BODY)continue;
            /* A 24-cell snake on this circuit has distant occupied neighbours;
             * if the first candidate is the tail, it moves legally and we retry. */
            turn(candidate);step();collided=has("GAME OVER");
            if(!collided) { enter_circuit();play_until(25); }
            break;
        }
    }
    CHECK(collided);
}

#ifdef TEST_WAMR
static void save_frame(const char *directory,const char *name) {
    char path[1024];int n=snprintf(path,sizeof(path),"%s/%s.bin",directory,name);
    CHECK(n>0 && (size_t)n<sizeof(path));render();
    FILE *out=fopen(path,"wb");CHECK(out!=NULL);if(!out)return;
    CHECK(fwrite(&frame,sizeof(frame),1,out)==1);CHECK(fclose(out)==0);
    printf("frame=%s bytes=%zu commands=%u\n",path,sizeof(frame),frame.count);
}
static void export_frames(const char *directory) {
    fresh(0);save_frame(directory,"ready");eat_first();save_frame(directory,"running");
    toggle();save_frame(directory,"paused");toggle();step();step();step();save_frame(directory,"game-over");
    CHECK(stop_app()==0);close_app();clock_ms=0;open_app();save_frame(directory,"restored");
    enter_circuit();play_until(80);save_frame(directory,"win");
}
#endif

int main(int argc,char **argv) {
#ifdef TEST_WAMR
    if(argc<2) { fprintf(stderr,"usage: test_snake app.wasm [--frames existing-directory]\n");return 2; }
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    fseek(file,0,SEEK_END);wasm_size=(uint32_t)ftell(file);rewind(file);
    wasm=malloc(wasm_size);if(!wasm)return 2;
    CHECK(fread(wasm,1,wasm_size,file)==wasm_size);fclose(file);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
#else
    (void)argc;(void)argv;
#endif
    run_timing_and_controls();run_collisions_and_save();run_full_board_and_body_collision();
#ifdef TEST_WAMR
    if(argc==4 && !strcmp(argv[2],"--frames"))export_frames(argv[3]);
#endif
    close_app();
#ifdef TEST_WAMR
    tinyrt_runtime_system_shutdown();CHECK(tinyrt_runtime_memory_used()==0);
    CHECK(peak_memory<=2097152u);printf("WAMR peak tracked bytes=%zu\n",peak_memory);free(wasm);
#endif
    printf("snake checks=%u failures=%u max_commands=%u\n",checks,failures,peak_commands);
    return failures?1:0;
}
