#include "nes/mmc1.h"
#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/nes.h"
#include <stdio.h>


/* MMC1 shift register */
static uint8_t mmc1_sr;
#define RESET_SR mmc1_sr = 0x10
#define SHIFT_SR(v) mmc1_sr = (mmc1_sr >> 1) | ((v & 1) << 4)

/* MMC1 load register */
static uint8_t mmc1_load;

/* MMC1 control register */
static uint8_t mmc1_ctrl;


/**
 *  Write to CHR Bank 0 register the value mmc1_sr.
 */
static inline void switch_chr_bank0 ()
{
	uint8_t mode = (mmc1_ctrl >> 4) & 1;
	if (mode == 1)
		nes_chr_load_4kb_bank (mmc1_load, 0);
	else
		nes_chr_load_8kb_bank (mmc1_load & 0xFE);
}

/**
 *  Write to CHR Bank 1 register the value mmc1_sr.
 */
static inline void switch_chr_bank1 ()
{
	uint8_t mode = (mmc1_ctrl >> 4) & 1;
	if (mode == 1) // only switch in 4KB mode
		nes_chr_load_4kb_bank (mmc1_load, 1);
}

/**
 *  Write to PRG bank register the value mmc1_sr.
 */
static inline void switch_prg_bank ()
{
	uint8_t mode = (mmc1_ctrl >> 2) & 3;
	uint8_t bank = mmc1_load & 0xF;
	switch (mode)
	{
	case 0: // switch 32 KB at $8000, ignoring low bit of bank number
	case 1:
		nes_prg_load_32k_bank (bank & 0xFE);
		break;
	case 2: // fix first bank at $8000 and switch 16 KB bank at $C000
		nes_prg_load_16k_bank (0, 1);
		nes_prg_load_16k_bank (bank, 1);
		break;
	case 3: // fix last bank at $C000 and switch 16 KB bank at $8000
		nes_prg_load_16k_bank (-1, 1);
		nes_prg_load_16k_bank (bank, 0);
		break;
	}
}

/**
 *  Write to control register the value mmc1_sr.
 */
static inline void write_control ()
{
	mmc1_ctrl = mmc1_load;
	//printf ("MMC1 Control = %.2X\n", mmc1_ctrl);
	// TODO make sure banks follow the new mode
	// fix mirroring
	switch (mmc1_ctrl & 3)
	{
	case 0: // one screen lower bank
		break;
	case 1: // one screen upper bank
		break;
	case 2: // vertical
		nes_ppu_set_mirroring (NES_PPU_MIRROR_VERTICAL);
		break;
	case 3: // horizontal
		nes_ppu_set_mirroring (NES_PPU_MIRROR_HORIZONTAL);
		break;
	}
}


/**
 *   Write to shift register.
 */
static int write (uint16_t addr, uint8_t v)
{
	if (addr >= 0x8000)
	{
		if ((v & 0x80) == 0x80) // reset shift register
		{
			//printf ("MMC1 reset SR\n");
			RESET_SR;
			mmc1_ctrl |= 0xC0;
			nes_prg_load_16k_bank (-1, 1);
		}
		else
		{
			int done = (mmc1_sr & 1) == 1;
			SHIFT_SR (v); // shift LSB of v into shift register
			if (done) { // 5th write - write to correct register
				mmc1_load = mmc1_sr;
				switch ((addr >> 13) & 3) // write to correct register
				{
				case 0: // Control $8000-9FFF
					write_control();
					break;
				case 1: // CHR Bank 0 $A000-$BFFF
					switch_chr_bank0();
					break;
				case 2: // CHR Bank 1 $C000-$DFFF
					switch_chr_bank1();
					break;
				case 3: // PRG Bank $E000-$FFFF
					switch_prg_bank();
					break;
				}
				RESET_SR;
			}
		}
		return 1;
	}
	return 0;
}

void nes_mmc1_load ()
{
	nes_cpu_add_store_handler (&write);
	RESET_SR;
	nes_prg_load_16k_bank (-1, 1);
}
