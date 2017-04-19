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
	NES_PPU_MIRROR_HORIZONTAL,
	NES_PPU_MIRROR_VERTICAL,
	NES_PPU_MIRROR_SINGLE0,
	NES_PPU_MIRROR_SINGLE1,
	NES_PPU_MIRROR_FOUR_SCREEN,
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
void nes_ppu_reset ();

/**
 *  nes_ppu_step performs a cycle in the PPU.
*/ void nes_ppu_step () ;

/**
 *  Load data into VRAM.
 *  This can be used when restoring a previous session.
 */
void nes_ppu_load_vram (void* /* data */) ;

/**
 *  nes_ppu_load_chr_rom will copy data to the PPU CHR ROM.
 */
void nes_ppu_load_chr_rom (void* /* data */) ;

/**
 *  nes_ppu_switch_chr_rom_bank switches which CHR ROM bank is loaded @ bank 0/1.
 */
void nes_ppu_switch_chr_rom_bank (int /* bank */, int /* chr_bank */) ;

/**
 *  nes_ppu_set_mirroring sets the mirroring mode in the nametables.
 */
void nes_ppu_set_mirroring (nes_ppu_mirroring_mode /* mode */);

/**
 *  Write value to PPU register.
 *  This function should be called when modifying their values as to the
 *  necessary callbacks that alter PPU state when writing to registers.
 */
void nes_ppu_register_write (nes_ppu_register /* reg */, uint8_t /* value */) ;

/**
 *  Read value from PPU register.
 *  Use this function to make sure necessary callbacks are called that alter
 *  PPU state.
 */
uint8_t nes_ppu_register_read (nes_ppu_register /* reg */) ;

/**
 *  Load data into the PPUs OAM data.
 */
void nes_ppu_load_oam_data (void* /* data */);

/**
 * nes_ppu_chr_reader defines a function type that takes an address and returns it's corresponding
 * value within the CHR ROM.
 */
typedef uint8_t (*nes_ppu_chr_reader) (uint16_t /* address */) ;

/**
 * nes_ppu_set_chr_read is used to override default functionality reading CHR ROM which would be loaded
 * data into VRAM.
 * Mappers are to use this to make sure the PPU gets correct CHR data.
 */
void nes_ppu_set_chr_read (nes_ppu_chr_reader /* reader */) ;

/**
 * nes_ppu_chr_writer defines a function type that takes an address, a value and stores it to CHR ROM.
 */
typedef void (*nes_ppu_chr_writer) (uint16_t /* address */, uint8_t /* value */) ;

/**
 * nes_ppu_set_chr_writer is used to override the default functionality of writing CHR data to the VRAM and
 * instead to custom locations.
 * Mappers are to use this to better handle CHR data for the PPU.
 */
void nes_ppu_set_chr_writer (nes_ppu_chr_writer /* writer */) ;

#endif
