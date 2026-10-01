/* The guest supplies input and scheduling; the ROM owns all game rules. */
#include "tinyrt.h"
#include "engine.h"
static int32_t center_x;
static uint32_t queued,phase,last_frame,loading,running;
static int hit(int32_t x,int32_t y,int32_t left,int32_t top,int32_t width,int32_t height){return x>=left&&x<left+width&&y>=top&&y<top+height;}
static void frame_input(maze_status_t status){
    if(!status.ready||status.scanline!=0||status.frame<4)return;
    if(phase==1){maze_buttons(0);phase=2;}
    else if(phase==2)phase=0;
    if(phase==0&&queued){maze_buttons(queued);queued=0;phase=1;}
}
int32_t tinyrt_init(int32_t width,int32_t height){
    if(width<466||height<466)return -1;
    center_x=width/2;queued=0;phase=0;last_frame=0;loading=1;running=1;
    maze_start();return clock_interval(1);
}
int32_t tinyrt_event(int32_t kind,int32_t x,int32_t y,int32_t arg){
    (void)arg;if(!running)return 0;
    if(kind==TINYRT_CLOCK_EVENT){
        uint32_t frame=maze_status().frame;
        /* Cached idle lines are cheap; retain the three-work-line budget for
         * real CPU/PPU work and bound every callback to 64 total scanlines. */
        for(unsigned i=0,work=0;i<64&&work<3;i++){
            frame_input(maze_status());work+=maze_step();
            if(maze_status().frame!=frame)break;
        }
        return 0;
    }
    if(kind==TINYRT_TOUCH_RELEASE){
        uint32_t button=0;
        if(hit(x,y,center_x-19,347,38,36))button=MAZE_UP;
        else if(hit(x,y,center_x-69,389,40,36))button=MAZE_LEFT;
        else if(hit(x,y,center_x-19,389,38,36))button=MAZE_DOWN;
        else if(hit(x,y,center_x+31,389,40,36))button=MAZE_RIGHT;
        else if(hit(x,y,center_x+63,347,60,36))button=MAZE_START;
        if(button&&!queued)queued=button;
    }
    return 0;
}
static int control(int32_t x,int32_t y,int32_t width,const char*text,uint32_t length){
    if(draw_round_rect(x,y,width,36,8,0x263c50))return -1;
    return draw_text_box(x,y,width,36,text,length,0xf0f4f8,18,1);
}
int32_t tinyrt_render(void){
    maze_status_t status=maze_status();
    if(!loading&&(status.frame<5||status.frame==last_frame))return draw_skip();
    if(draw_clear(0x0b1520))return -1;
    if(status.frame<5){
        loading=0;
        return draw_text_box(center_x-128,195,256,48,"Starting NES...",15,0xf0f4f8,24,1);
    }
    if(draw_rect(center_x-130,93,260,244,0x365570))return -1;
    if(draw_rgb565(center_x-128,95,256,240,(const uint8_t*)maze_pixels(),256u*240u*2u))return -1;
    if(control(center_x-19,347,38,"UP",2)||control(center_x-69,389,40,"<",1)||
       control(center_x-19,389,38,"DN",2)||control(center_x+31,389,40,">",1)||
       control(center_x+63,347,60,status.state==0?"START":"RESET",5))return -1;
    if(draw_text_box(center_x-68,428,136,24,"TAP TO MOVE",11,0x92a8bb,18,1))return -1;
    loading=0;last_frame=status.frame;return 0;
}
int32_t tinyrt_stop(void){running=0;queued=0;phase=0;maze_buttons(0);return 0;}
