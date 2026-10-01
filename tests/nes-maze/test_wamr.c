#include "tinyrt_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
static tinyrt_runtime_t *rt;
static tinyrt_frame_t frame;
static unsigned checks,images,skips,ticks,calls;
static size_t peak;
static double durations[200000];
static LARGE_INTEGER frequency;
static const char *output;
#define CHECK(condition) do{checks++;if(!(condition)){printf("FAIL line=%d %s ticks=%u images=%u guest=%s\n",__LINE__,#condition,ticks,images,rt?tinyrt_runtime_last_error(rt):"none");return 1;}}while(0)
static int record(tinyrt_status_t result,LARGE_INTEGER start){
 LARGE_INTEGER end;QueryPerformanceCounter(&end);
 if(calls<200000)durations[calls++]=1e6*(double)(end.QuadPart-start.QuadPart)/(double)frequency.QuadPart;
 size_t used=tinyrt_runtime_memory_used();if(used>peak)peak=used;
 CHECK(result==TINYRT_OK);return 0;
}
static int render(void){
 LARGE_INTEGER start;QueryPerformanceCounter(&start);if(record(tinyrt_runtime_render(rt,&frame),start))return 1;
 if(frame.count==0){skips++;return 0;}
 unsigned found=0;
 for(unsigned i=0;i<frame.count;i++)if(frame.commands[i].kind==TINYRT_DRAW_RGB565){
  tinyrt_draw_command_t*c=&frame.commands[i];CHECK(c->x==105&&c->y==95&&c->w==256&&c->h==240);found++;
 }
 CHECK(found<=1);if(found){CHECK(frame.pixel_bytes==122880);if(!images&&output)printf("FIRST_PIXELS ticks=%u bytes=%u\n",ticks,frame.pixel_bytes);images++;}return 0;
}
static int event(int kind,int x,int y){
 LARGE_INTEGER start;QueryPerformanceCounter(&start);
 if(record(tinyrt_runtime_event(rt,kind,x,y,(int32_t)ticks),start))return 1;
 if(kind==2)ticks++;return render();
}
static int advance(unsigned frames){
 unsigned target=images+frames;
 for(unsigned i=0;i<(frames+3)*100+500&&images<target;i++)if(event(2,0,0))return 1;
 CHECK(images==target);return 0;
}
static unsigned pixel(unsigned x,unsigned y){unsigned i=(y*256+x)*2;return frame.pixels[i]|((unsigned)frame.pixels[i+1]<<8);}
static int player(unsigned x,unsigned y){
 unsigned players=0;
 for(unsigned row=0;row<10;row++)for(unsigned col=0;col<12;col++){
  unsigned white=pixel(32+col*16+2,40+row*16+2)==0xffff;
  CHECK(white==((row==y&&col==x)?1u:0u));players+=white;
 }
 CHECK(players==1);return 0;
}
static int dump(const char *name){
 if(!output)return 0;char path[512];snprintf(path,sizeof(path),"%s/wamr-%s.rgb565",output,name);
 FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(frame.pixels,1,122880,f)==122880);CHECK(fclose(f)==0);return 0;
}
static int click(char key){
 unsigned before=ticks;
 int x=233,y=365;
 if(key=='D')y=405;else if(key=='L'){x=183;y=405;}else if(key=='R'){x=283;y=405;}else if(key=='S'){x=326;y=365;}
 if(event(1,x,y)||advance(3))return 1;CHECK(ticks-before<=64);return 0;
}
static int compare(const void*a,const void*b){double x=*(const double*)a,y=*(const double*)b;return(x>y)-(x<y);}
int main(int argc,char**argv){
 CHECK(argc>=2);unsigned budget=argc>2?(unsigned)strtoul(argv[2],NULL,10):100000;
 unsigned extra=argc>3?(unsigned)strtoul(argv[3],NULL,10):260;CHECK(extra<=260);if(argc>4)output=argv[4];
 FILE*f=fopen(argv[1],"rb");CHECK(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);CHECK(size>8&&size<2*1024*1024);
 unsigned char*bytes=malloc((size_t)size);CHECK(bytes);CHECK(fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);
 tinyrt_package_policy_t policy={1,11,16,budget};tinyrt_runtime_host_t host={0};
 QueryPerformanceFrequency(&frequency);LARGE_INTEGER begin,end;QueryPerformanceCounter(&begin);
 CHECK(tinyrt_runtime_system_init()==TINYRT_OK);size_t baseline=tinyrt_runtime_memory_used();
 for(unsigned round=0;round<2;round++){
  images=0;CHECK(tinyrt_runtime_create(bytes,(uint32_t)size,&policy,&host,&rt)==TINYRT_OK);
  LARGE_INTEGER start;QueryPerformanceCounter(&start);if(record(tinyrt_runtime_init(rt,466,466),start))return 1;
  CHECK(tinyrt_runtime_clock_interval_ms(rt)==1);if(render()||advance(4)||player(1,1))return 1;
  if(dump("title")||click('S')||dump("start")||click('U')||player(1,1)||dump("wall"))return 1;
  if(click('R')||player(2,1)||dump("right-once")||click('R')||player(3,1))return 1;
  if(dump("play")||click('S')||player(1,1))return 1;
  /* One queued key is retained; extra taps cannot overwrite it or burst moves. */
  if(event(1,283,405)||event(1,283,405)||event(1,283,405)||advance(3)||player(2,1))return 1;
  if(event(1,283,405)||advance(1)||event(1,283,405)||advance(4)||player(4,1))return 1;
  if(event(1,100,100)||advance(3)||player(4,1)||click('S')||player(1,1))return 1;
  const char*path="RRRDDRRUURRRRDDDDDDD";unsigned x=1,y=1;
  if(dump("route-00"))return 1;
  for(unsigned i=0;path[i];i++){
   char key=path[i];if(key=='R')x++;else if(key=='L')x--;else if(key=='D')y++;else y--;
   if(click(key)||player(x,y))return 1;
   char label[32];snprintf(label,sizeof(label),"route-%02u",i+1);if(dump(label))return 1;
  }
  CHECK(x==10&&y==8);if(dump("won"))return 1;if(click('L')||player(10,8))return 1;
  if(click('S')||player(1,1))return 1;
  if(dump("restart")||advance(extra))return 1;
  CHECK(skips>images*2);
  QueryPerformanceCounter(&start);if(record(tinyrt_runtime_stop(rt),start))return 1;
  tinyrt_runtime_destroy(rt);rt=NULL;CHECK(tinyrt_runtime_memory_used()==baseline);
 }
 tinyrt_runtime_system_shutdown();free(bytes);CHECK(tinyrt_runtime_memory_used()==0);
 QueryPerformanceCounter(&end);qsort(durations,calls,sizeof(double),compare);
 printf("WAMR PASS checks=%u budget=%u wasm=%ld ticks=%u calls=%u skips=%u peak=%zu total_ms=%.3f p50_us=%.3f p95_us=%.3f max_us=%.3f\n",checks,budget,size,ticks,calls,skips,peak,1e3*(double)(end.QuadPart-begin.QuadPart)/(double)frequency.QuadPart,durations[calls/2],durations[calls*95/100],durations[calls-1]);return 0;
}
