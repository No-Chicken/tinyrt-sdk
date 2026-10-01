/*
 * Copyright PeakRacing
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* TinyRT N1 adapter, 2026. The scanline body below derives from PeakRacing/nes
 * revision 638096ae00d258700779be2af06478d1be5bf8a1 (Apache-2.0).
 * Changes: persistent continuation, NROM-only fixed ROM, bounded initialization,
 * one scanline per step, incremental CRC. Upstream files remain byte-identical.
 */
#include "engine.h"
#include "rom_data.h"
#include "../../third_party/nes/upstream/src/nes.c"
static nes_t machine;
static uint32_t clear_offset, ready, frames, frame_crc, running_crc, held_a;
static uint8_t dot_remainder;
static uint16_t line;
static const uint32_t crc_nibble[16]={
    0x00000000,0x1db71064,0x3b6e20c8,0x26d930ac,
    0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,
    0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,
    0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c
};
static uint32_t crc_line(uint32_t crc,const uint16_t *pixels) {
    /* Explicit little endian RGB565 bytes, identical on every host. */
    for(unsigned x=0;x<256;x++) {
        uint32_t value=pixels[x];
        for(unsigned b=0;b<2;b++) {
            crc^=value&255u;value>>=8;
            crc=(crc>>4)^crc_nibble[crc&15u];
            crc=(crc>>4)^crc_nibble[crc&15u];
        }
    }
    return crc;
}
static void visible_line(nes_t *nes) {
 // 0-239 Visible frame
            uint16_t scanline_ticks = nes->timing.line_clocks;
            sprite_line_t sprite_line = {0};
            dot_remainder += nes->timing.remainder_add;
            if (dot_remainder >= nes->timing.remainder_mod) { dot_remainder -= nes->timing.remainder_mod; scanline_ticks++; }
            if (nes->nes_ppu.MASK_s){
                NES_PROF_BEGIN(nes, NES_PROF_SPRITE);
                nes_prepare_sprite_line(nes, nes->scanline, &sprite_line);
                NES_PROF_END(nes, NES_PROF_SPRITE);
            }
            if (nes->nes_ppu.MASK_b){
                if (nes->nes_mapper.mapper_render_screen)
                    nes->nes_mapper.mapper_render_screen(nes, 1);
                NES_PROF_BEGIN(nes, NES_PROF_BG);
#if (NES_FRAME_SKIP != 0)
                if (nes->nes_frame_skip_count == 0)
#endif
                {
#if (NES_RAM_LACK == 1)
                nes_render_background_line(nes, nes->scanline, nes->nes_draw_data + nes->scanline%(NES_HEIGHT/2) * NES_WIDTH);
#else
                nes_render_background_line(nes, nes->scanline, nes->nes_draw_data + nes->scanline * NES_WIDTH);
#endif
                }
#if (NES_FRAME_SKIP != 0)
                else {
                    /* Sprite 0 hit has to see this frame's background, so a skipped
                     * frame still refreshes the opacity map (no pixels, no palettes). */
                    nes_render_background_opacity_line(nes);
                }
#endif
                NES_PROF_END(nes, NES_PROF_BG);
            } else {
#if (NES_FRAME_SKIP != 0)
                if (nes->nes_frame_skip_count == 0)
#endif
                {
#if (NES_RAM_LACK == 1)
                nes_color_t* line_data = nes->nes_draw_data + nes->scanline%(NES_HEIGHT/2) * NES_WIDTH;
#else
                nes_color_t* line_data = nes->nes_draw_data + nes->scanline * NES_WIDTH;
#endif
                for (uint16_t x = 0; x < NES_WIDTH; x++) {
                    line_data[x] = nes->nes_ppu.background_palette[0];
                }
                nes_memset(nes->nes_ppu.bg_opaque, 0, sizeof(nes->nes_ppu.bg_opaque));
                }
            }
            if (nes->nes_ppu.MASK_s){
                if (nes->nes_mapper.mapper_render_screen)
                    nes->nes_mapper.mapper_render_screen(nes, 0);
                NES_PROF_BEGIN(nes, NES_PROF_SPRITE);
#if (NES_RAM_LACK == 1)
                nes_render_sprite_line(nes, &sprite_line,nes->nes_draw_data + nes->scanline%(NES_HEIGHT/2) * NES_WIDTH);
#else
                nes_render_sprite_line(nes, &sprite_line,nes->nes_draw_data + nes->scanline * NES_WIDTH);
#endif
                NES_PROF_END(nes, NES_PROF_SPRITE);
            }
            nes_opcode(nes,nes->timing.line_split); // ppu cycles: 85*3=255 (NTSC)
            // https://www.nesdev.org/wiki/PPU_scrolling#Wrapping_around
            if (nes->nes_ppu.MASK_b || nes->nes_ppu.MASK_s){
                // Rendering resets OAMADDR during sprite evaluation; at line granularity,
                // keep it with the existing dot-256/257 scroll update.
                nes->nes_ppu.oam_addr = 0;
                // https://www.nesdev.org/wiki/PPU_scrolling#At_dot_256_of_each_scanline
                if ((nes->nes_ppu.v.fine_y) < 7) {
                    nes->nes_ppu.v.fine_y++;
                }else {
                    nes->nes_ppu.v.fine_y = 0;
                    uint8_t y = (uint8_t)(nes->nes_ppu.v.coarse_y);
                    if (y == 29) {
                        y = 0;
                        nes->nes_ppu.v_reg ^= 0x0800;
                    }else if (y == 31) {
                        y = 0;
                    }else {
                        y++;
                    }
                    nes->nes_ppu.v.coarse_y = y;
                }
                // https://www.nesdev.org/wiki/PPU_scrolling#At_dot_257_of_each_scanline
                // v: ....A.. ...BCDEF <- t: ....A.. ...BCDEF
                nes->nes_ppu.v_reg = (nes->nes_ppu.v_reg & (uint16_t)0xFBE0) | (nes->nes_ppu.t_reg & (uint16_t)0x041F);
            }
            if (nes->nes_mapper.mapper_hsync) {
                nes->nes_mapper.mapper_hsync(nes);
            }
            nes_opcode(nes,scanline_ticks-nes->timing.line_split);
#if (NES_ENABLE_SOUND==1)
            if ((uint16_t)nes->scanline % nes->timing.apu_frame_divisor == (uint16_t)(nes->timing.apu_frame_divisor - 1u)) {
                NES_PROF_BEGIN(nes, NES_PROF_APU);
                nes_apu_frame(nes);
                NES_PROF_END(nes, NES_PROF_APU);
            }
#endif
#if (NES_RAM_LACK == 1)
#if (NES_FRAME_SKIP != 0)
            if(nes->nes_frame_skip_count == 0)
#endif
            {
                if (nes->scanline == NES_HEIGHT/2-1){
                    NES_PROF_BEGIN(nes, NES_PROF_DRAW);
                    nes_draw(0, 0, NES_WIDTH-1, NES_HEIGHT/2-1, nes->nes_draw_data);
                    NES_PROF_END(nes, NES_PROF_DRAW);
                }else if(nes->scanline == NES_HEIGHT-1){
                    NES_PROF_BEGIN(nes, NES_PROF_DRAW);
                    nes_draw(0, NES_HEIGHT/2, NES_WIDTH-1, NES_HEIGHT-1, nes->nes_draw_data);
                    NES_PROF_END(nes, NES_PROF_DRAW);
                }
            }
#endif

}
static uint16_t ticks_for_line(void) {
    uint16_t ticks=113;dot_remainder+=2;
    if(dot_remainder>=3){dot_remainder-=3;ticks++;}
    return ticks;
}
static void nrom_attach(void) {
    nes_t *n=&machine;
    /* Only the compiled-in original NROM-128 fixture is accepted. No input parser. */
    n->nes_rom.prg_rom=(uint8_t *)(n1_rom+16);
    n->nes_rom.chr_rom=(uint8_t *)(n1_rom+16+16384);
    n->nes_rom.prg_rom_size=1;n->nes_rom.chr_rom_size=1;
    for(unsigned i=0;i<4;i++)n->nes_cpu.prg_banks[i]=n->nes_rom.prg_rom+(i&1u)*8192;
    for(unsigned i=0;i<8;i++)n->nes_ppu.pattern_table[i]=n->nes_rom.chr_rom+i*1024;
    n->timing.line_clocks=113;n->timing.line_split=85;n->timing.remainder_add=2;n->timing.remainder_mod=3;
    n->timing.vblank_lines=20;n->timing.frame_rate=60;n->timing.cpu_clock=1789773;
    nes_cpu_init(n);nes_ppu_init(n);nes_cpu_reset(n);
}
void n1_start(void) {
    clear_offset=0;ready=0;frames=0;frame_crc=0;running_crc=0;
    held_a=0;dot_remainder=0;line=0;
}
void n1_button(uint32_t held) {held_a=held?1u:0u;}
void n1_step(void) {
    if(!ready) {
        /* Even restart avoids an unmetered/full framebuffer clear in init. */
        uint32_t remaining=(uint32_t)sizeof(machine)-clear_offset;
        uint32_t count=remaining>512u?512u:remaining;
        unsigned char *p=(unsigned char *)&machine;
        for(uint32_t i=0;i<count;i++)p[clear_offset+i]=0;
        clear_offset+=count;
        if(clear_offset==sizeof(machine)){nrom_attach();ready=1;}
        return;
    }
    nes_t *n=&machine;n->scanline=line;n->nes_cpu.joypad.A1=(uint8_t)held_a;
    if(line<240) {
        if(line==0){n->nes_ppu.STATUS_S=0;n->nes_ppu.STATUS_O=0;nes_palette_generate(n);running_crc=0xffffffffu;}
        visible_line(n);
        running_crc=crc_line(running_crc,n->nes_draw_data+(unsigned)line*256u);
        /* Publish the checksum together with the completed frame below. */
    } else {
        if(line==241){n->nes_ppu.STATUS_V=1;if(n->nes_ppu.CTRL_V)n->nes_cpu.irq_nmi=1;}
        if(line==261)n->nes_ppu.ppu_status=0;
        nes_opcode(n,ticks_for_line());
        if(line==261) {
            if(n->nes_ppu.MASK_b||n->nes_ppu.MASK_s)n->nes_ppu.v_reg=n->nes_ppu.t_reg;
            frame_crc=~running_crc;frames++;line=0;return;
        }
    }
    line++;
}
n1_status_t n1_status(void) {
    n1_status_t s={ready,frames,frame_crc,line,machine.nes_cpu.cpu_ram[0],machine.nes_cpu.cpu_ram[1],machine.nes_cpu.cpu_ram[2],machine.nes_cpu.PC};return s;
}
const uint16_t *n1_pixels(void) {return machine.nes_draw_data;}
