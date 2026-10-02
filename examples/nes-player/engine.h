#ifndef NES_PLAYER_ENGINE_H
#define NES_PLAYER_ENGINE_H
#include <stdint.h>
enum {NES_PAD_RIGHT=1,NES_PAD_LEFT=2,NES_PAD_DOWN=4,NES_PAD_UP=8,NES_PAD_START=16,NES_PAD_SELECT=32,NES_PAD_B=64,NES_PAD_A=128};
typedef struct {uint32_t ready,frame,scanline,state,x,y,held,moves,nmi;} nes_player_status_t;
int nes_player_start(void);
/* Return one for bounded work, two for malformed/truncated resource data. */
unsigned nes_player_step(void);
void nes_player_buttons(uint32_t held);
nes_player_status_t nes_player_status(void);
const uint16_t *nes_player_pixels(void);

#ifdef NES_NATIVE_TRACE
uint32_t nes_player_state_hash(void);
#endif
#endif
