/* Actual pinned TinyRT + WAMR, all assertions are based on the original ROM spec. */
#include "tinyrt_runtime.h"
#include "wasm_export.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4244)
#endif
#include "wasm.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,calls,slices;
static size_t peak;
static double durations[400000];
static LARGE_INTEGER frequency;
static tinyrt_runtime_t *rt;
static tinyrt_frame_t frame;
static unsigned frame_no,crc,nmi,button,signature,ready,line;
#define CHECK(x) do{checks++;if(!(x)){printf("FAIL line=%d %s; guest=%s\n",__LINE__,#x,rt?tinyrt_runtime_last_error(rt):"none");return 1;}}while(0)
static int record(tinyrt_status_t result,LARGE_INTEGER start) {
    LARGE_INTEGER end;QueryPerformanceCounter(&end);
    if(calls<400000)durations[calls++]=1e6*(double)(end.QuadPart-start.QuadPart)/(double)frequency.QuadPart;
    size_t used=tinyrt_runtime_memory_used();if(used>peak)peak=used;
    if(result!=TINYRT_OK)printf("CONTEXT calls=%u slices=%u frame=%u ready=%u line=%u\n",calls,slices,frame_no,ready,line);
    CHECK(result==TINYRT_OK);return 0;
}
static int render(void) {
    LARGE_INTEGER start;QueryPerformanceCounter(&start);
    if(record(tinyrt_runtime_render(rt,&frame),start))return 1;
    CHECK(frame.count==7);unsigned found=0;
    for(unsigned i=0;i<frame.count;i++){
        char *s=frame.commands[i].text;
        if(sscanf(s,"FRAME %u",&frame_no)==1)found|=1;
        if(sscanf(s,"CRC %x",&crc)==1)found|=2;
        if(sscanf(s,"NMI %u A %u SIG %u",&nmi,&button,&signature)==3)found|=4;
        if(sscanf(s,"READY %u LINE %u",&ready,&line)==2)found|=8;
    }
    CHECK(found==15);return 0;
}
static int event(int kind) {
    LARGE_INTEGER start;QueryPerformanceCounter(&start);
    if(record(tinyrt_runtime_event(rt,kind,20,20,(int32_t)(slices*100u)),start))return 1;
    if(kind==2)slices++;
    return render();
}
static int until(unsigned wanted) {
    for(unsigned i=0;i<200000&&frame_no<wanted;i++)if(event(2))return 1;
    CHECK(frame_no==wanted);return 0;
}
static int compare_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return x<y?-1:x>y;}
int main(int argc,char **argv) {
    CHECK(argc>=2);unsigned budget=argc>2?(unsigned)strtoul(argv[2],NULL,10):100000;
    FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    CHECK(size>8&&size+256<=296*1024);unsigned char *bytes=malloc((size_t)size);CHECK(bytes!=NULL);
    CHECK(fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);
    tinyrt_package_policy_t policy={1,11,16,budget};tinyrt_runtime_host_t host={0};
    QueryPerformanceFrequency(&frequency);LARGE_INTEGER total;QueryPerformanceCounter(&total);
    CHECK(tinyrt_runtime_system_init()==TINYRT_OK);
    if(argc>3 && !strcmp(argv[3],"inspect")) {
        unsigned char *copy=malloc((size_t)size);CHECK(copy!=NULL);memcpy(copy,bytes,(size_t)size);
        char error[160]={0};WASMModule *module=(WASMModule*)wasm_runtime_load(copy,(uint32_t)size,error,sizeof(error));CHECK(module!=NULL);
        for(unsigned i=0;i<module->function_count;i++) {
            WASMFunction *fn=module->functions[i];
            if(fn->max_block_num>20)printf("FUNCTION %u blocks=%u stack_cells=%u locals=%u\n",i,fn->max_block_num,fn->max_stack_cell_num,fn->local_cell_num);
        }
        wasm_runtime_unload((wasm_module_t)module);free(copy);
    }
    size_t baseline=tinyrt_runtime_memory_used();
    for(unsigned round=0;round<2;round++){
        CHECK(tinyrt_runtime_create(bytes,(uint32_t)size,&policy,&host,&rt)==TINYRT_OK);
        size_t used=tinyrt_runtime_memory_used();if(used>peak)peak=used;
        LARGE_INTEGER start;QueryPerformanceCounter(&start);
        if(record(tinyrt_runtime_init(rt,466,466),start)||render())return 1;
        CHECK(frame_no==0&&crc==0&&ready==0);
        if(until(8))return 1;
        CHECK(nmi==8&&signature==0x33&&button==0&&crc==0x7fdd3027);
        printf("WASM round=%u frame=%u crc=%08X nmi=%u signature=%u\n",round,frame_no,crc,nmi,signature);
        if(round==0){
            for(unsigned i=0;i<240;i++)if(event(2))return 1;
            CHECK(frame_no==8&&crc==0x7fdd3027);
            if(until(9))return 1;CHECK(crc==0xee67c189);
            if(event(1)||until(11))return 1;
            CHECK(button==1&&crc==0x7ac3eb7f);
            if(event(1)||until(13))return 1;
            CHECK(button==0&&crc==0xee67c189);
            unsigned extra=argc>3?(unsigned)strtoul(argv[3],NULL,10):0;
            if(extra>13){if(until(extra))return 1;CHECK(nmi==(extra&255u));CHECK(crc==((extra&1u)?0xee67c189u:0x7fdd3027u));}
        }
        tinyrt_runtime_destroy(rt);rt=NULL;CHECK(tinyrt_runtime_memory_used()==baseline);
    }
    tinyrt_runtime_system_shutdown();free(bytes);CHECK(tinyrt_runtime_memory_used()==0);
    LARGE_INTEGER end;QueryPerformanceCounter(&end);qsort(durations,calls,sizeof(double),compare_double);
    printf("WASM PASS checks=%u budget=%u wasm_bytes=%ld tracked_peak=%zu slices=%u calls=%u total_ms=%.3f p50_us=%.3f p95_us=%.3f max_us=%.3f\n",checks,budget,size,peak,slices,calls,1000.0*(double)(end.QuadPart-total.QuadPart)/(double)frequency.QuadPart,durations[calls/2],durations[calls*95/100],durations[calls-1]);
    return 0;
}
