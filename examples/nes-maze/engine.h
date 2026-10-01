#ifndef NES_MAZE_ENGINE_H
#define NES_MAZE_ENGINE_H
#include <stdint.h>
enum {MAZE_RIGHT=1,MAZE_LEFT=2,MAZE_DOWN=4,MAZE_UP=8,MAZE_START=16};
typedef struct {uint32_t ready,frame,scanline,state,x,y,held,moves,nmi;} maze_status_t;
void maze_start(void);
void maze_step(void);
void maze_buttons(uint32_t held);
maze_status_t maze_status(void);
const uint16_t *maze_pixels(void);
#endif
