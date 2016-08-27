/** -------------------------------------------------------------------------------------
 *  File: ppu.h
 *  Author: ximon
 *  Description: Public interface towards NES ppu to be used within the nes
 *               module.
 ---------------------------------------------------------------------------------------- */
#ifndef NES_PPU_H_
#define NES_PPU_H_

#include <stdint.h>

/**
 * Total number of scanlines in one frame.
 */
#define SCANLINES_PER_FRAME 262
/**
 * Number of blank scanlines (of the total amount of scanlines).
 */
#define BLANK_SCANLINES 22
/*
 * PPU clock cycles for each scanline.
 */
#define PPUCC_PER_SCANLINE 341
/**
 * Start of PPU register's location in RAM memory.
 */
#define PPU_REGISTER_MEM_LOC 0x2000

/**
 *  Nametable mirroring modes.
 */
typedef enum mirroring_modes
{
	HORIZONTAL,
	VERTICAL,
	FOUR_SCREEN
}
nes_ppu_mirroring_mode;

/**
 *  PPU Registers.
 */
typedef enum ppu_registers
{
	PPUCTRL,
	PPUMASK,
	PPUSTATUS,
	OAMADDR,
	OAMDATA,
	PPUSCROLL,
	PPUADDR,
	PPUDATA
}
nes_ppu_register;


/**
 *  Initialize the PPU to its startup state.
 *  This needs to be called before running the game to make
 *  sure it runs correctly.
 */
void nes_ppu_init ();

/**
 *  Render a number of pixels to the frame buffer.
 *  If the PPU finishes an entire frame it will display
 *  on the screen.
 */
void nes_ppu_render (int pixels);

/**
 *  Tell the PPU how to mirror the tiles.
 *  If this is not set it will default to HORIZONTAL.
 */
void nes_ppu_set_mirroring_mode (nes_ppu_mirroring_mode mirror);

/**
 *  Load data into VRAM.
 */
void nes_ppu_load_vram (void *data) ;

/**
 *  Write value to PPU register.
 *  This function should be called when modifying their values as to the
 *  necessary callbacks that alter PPU state when writing to registers.
 */
void nes_ppu_register_write (nes_ppu_register reg, uint8_t value) ;

/**
 *  Read value from PPU register.
 *  Use this function to make sure necessary callbacks are called that alter
 *  PPU state.
 */
uint8_t nes_ppu_register_read (nes_ppu_register reg) ;

/**
 *  Load data into the PPUs OAM data.
 */
void nes_ppu_load_oam_data (void *data);

#endif
