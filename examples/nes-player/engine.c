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

/* TinyRT mapper-0 resource adapter, 2026. The scanline body below derives from PeakRacing/nes
 * revision 638096ae00d258700779be2af06478d1be5bf8a1 (Apache-2.0).
 * Changes: persistent continuation, bounded NROM-128/256 resource ROM, bounded initialization,
 * one scanline per step, full PPU redraw, guarded side-effect-free NROM self-JMP batching,
 * no audio or diagnostic CRC. Upstream files remain byte-identical.
 */
#include "engine.h"
#include "tinyrt.h"
static uint8_t rom_image[16+32768+8192];
static uint32_t rom_length, rom_loaded;
#include "../../third_party/nes/upstream/src/nes.c"
static nes_t machine;
static uint32_t clear_offset, ready, frames, held_buttons;
static uint8_t dot_remainder;
static uint16_t line;
#ifndef NES_IDLE_BATCH
#define NES_IDLE_BATCH 1
#endif
static void player_opcode(nes_t *n,uint16_t ticks) {
    nes_cpu_t *c=&n->nes_cpu;
    uint16_t pc=c->PC;
    /* Only immutable mapper-0 PRG reads: three-cycle JMP-to-self changes no
     * registers, RAM, PPU or bus state. Interrupts always take the real path.
     * No scanline/background cache: moving views and sprites are fully drawn. */
    if(NES_IDLE_BATCH && ticks>c->cycles && pc>=0x8000u && pc<=0xfffdu &&
       !c->irq_nmi && !c->irq_nmi_delay && !c->irq_pending &&
       !n->nes_mapper.mapper_read_prg && !n->nes_mapper.mapper_cpu_clock) {
        uint32_t mask=(uint32_t)n->nes_rom.prg_rom_size*16384u-1u;
        const uint8_t *prg=n->nes_rom.prg_rom;
        if(prg[(pc-0x8000u)&mask]==0x4c &&
           prg[(pc-0x7fffu)&mask]==(uint8_t)pc &&
           prg[(pc-0x7ffeu)&mask]==(uint8_t)(pc>>8)) {
            c->cycles=(uint16_t)((3u-(ticks-c->cycles)%3u)%3u);
            c->opcode=0x4c;c->write_burst=0;return;
        }
    }
    nes_opcode(n,ticks);
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
            } else if (!nes->nes_ppu.MASK_b) {
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
            player_opcode(nes,nes->timing.line_split); // ppu cycles: 85*3=255 (NTSC)
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
            player_opcode(nes,scanline_ticks-nes->timing.line_split);
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
    /* Header was bounded and validated before resource bytes reached this adapter. */
    n->nes_rom.prg_rom=(uint8_t *)(rom_image+16);
    n->nes_rom.chr_rom=(uint8_t *)(rom_image+16+(uint32_t)rom_image[4]*16384);
    n->nes_rom.prg_rom_size=rom_image[4];n->nes_rom.chr_rom_size=1;
    n->nes_rom.mirroring_type=rom_image[6]&1u;
    for(unsigned i=0;i<4;i++)n->nes_cpu.prg_banks[i]=n->nes_rom.prg_rom+(i % (rom_image[4]*2u))*8192;
    for(unsigned i=0;i<8;i++)n->nes_ppu.pattern_table[i]=n->nes_rom.chr_rom+i*1024;
    n->timing.line_clocks=113;n->timing.line_split=85;n->timing.remainder_add=2;n->timing.remainder_mod=3;
    n->timing.vblank_lines=20;n->timing.frame_rate=60;n->timing.cpu_clock=1789773;
    nes_cpu_init(n);nes_ppu_init(n);nes_cpu_reset(n);
}
int nes_player_start(void) {
    if(asset_read(0,rom_image,16)!=16)return -1;
    if(rom_image[0]!='N'||rom_image[1]!='E'||rom_image[2]!='S'||rom_image[3]!=26 ||
       (rom_image[4]!=1&&rom_image[4]!=2)||rom_image[5]!=1 || (rom_image[6]&0xfeu)||rom_image[7])return -1;
    for(unsigned i=8;i<16;i++)if(rom_image[i])return -1;
    rom_length=16u+(uint32_t)rom_image[4]*16384u+8192u;rom_loaded=16;
    clear_offset=0;ready=0;frames=0;
    held_buttons=0;dot_remainder=0;line=0;
    return 0;
}
void nes_player_buttons(uint32_t held) {held_buttons=held&255u;}
unsigned nes_player_step(void) {
    if(rom_loaded<rom_length){
        uint32_t count=rom_length-rom_loaded;if(count>2048)count=2048;
        if(asset_read(rom_loaded,rom_image+rom_loaded,count)!=(int32_t)count)return 2;
        rom_loaded+=count;return 1;
    }
    if(!ready) {
        /* Even restart avoids an unmetered/full framebuffer clear in init. */
        uint32_t remaining=(uint32_t)sizeof(machine)-clear_offset;
        uint32_t count=remaining>512u?512u:remaining;
        unsigned char *p=(unsigned char *)&machine;
        for(uint32_t i=0;i<count;i++)p[clear_offset+i]=0;
        clear_offset+=count;
        if(clear_offset==sizeof(machine)){nrom_attach();ready=1;}
        return 1;
    }
    nes_t *n=&machine;n->scanline=line;n->nes_cpu.joypad.joypad=(uint16_t)(held_buttons<<8);
    unsigned work=1;
    if(line<240) {
        if(line==0){n->nes_ppu.STATUS_S=0;n->nes_ppu.STATUS_O=0;nes_palette_generate(n);}
        visible_line(n);

    } else {
        if(line==241){n->nes_ppu.STATUS_V=1;if(n->nes_ppu.CTRL_V){n->nes_cpu.irq_nmi=1;work=1;}}
        if(line==261)n->nes_ppu.ppu_status=0;
        player_opcode(n,ticks_for_line());
        if(line==261) {
            if(n->nes_ppu.MASK_b||n->nes_ppu.MASK_s)n->nes_ppu.v_reg=n->nes_ppu.t_reg;
            frames++;line=0;return work;
        }
    }
    line++;
    return work;
}
nes_player_status_t nes_player_status(void) {
    uint8_t *ram=machine.nes_cpu.cpu_ram;
    nes_player_status_t s={ready,frames,line,ram[0],ram[3],ram[2],ram[4],ram[5],ram[6]};return s;
}
const uint16_t *nes_player_pixels(void) {return machine.nes_draw_data;}

#ifdef NES_NATIVE_TRACE
static uint32_t hash_bytes(uint32_t h,const void *data,uint32_t size) {
    const uint8_t *p=data;for(uint32_t i=0;i<size;i++){h^=p[i];h*=16777619u;}return h;
}
uint32_t nes_player_state_hash(void) {
    nes_cpu_t *c=&machine.nes_cpu;nes_ppu_t *p=&machine.nes_ppu;
    uint32_t h=hash_bytes(2166136261u,c,(uint32_t)((uint8_t*)c->prg_banks-(uint8_t*)c));
    h=hash_bytes(h,&c->joypad,sizeof(c->joypad));
    /* PPU registers before pointer tables and every writable memory region. */
    h=hash_bytes(h,p,(uint32_t)((uint8_t*)p->pattern_table-(uint8_t*)p));
    h=hash_bytes(h,p->ppu_vram,sizeof(p->ppu_vram));
    h=hash_bytes(h,p->oam_data,sizeof(p->oam_data));
    h=hash_bytes(h,p->palette_indexes,sizeof(p->palette_indexes));
    h=hash_bytes(h,&dot_remainder,sizeof(dot_remainder));return h;
}
#endif
