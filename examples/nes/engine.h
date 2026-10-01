#ifndef NES_N1_ENGINE_H
#define NES_N1_ENGINE_H
#include <stdint.h>
typedef struct {
    uint32_t ready, frame, crc, scanline, nmi, button, signature, pc;
} n1_status_t;
void n1_start(void);
void n1_step(void);
void n1_button(uint32_t held);
n1_status_t n1_status(void);
const uint16_t *n1_pixels(void);
#endif
