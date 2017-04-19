#include "nes/mmc1.h"
#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/nes.h"
#include <stdio.h>


/* MMC1 shift register */
static uint8_t mmc1_sr;
#define RESET_SR mmc1_sr = 0x10
#define SHIFT_SR(v) mmc1_sr = (mmc1_sr >> 1) | ((v & 1) << 4)

/* MMC1 control register */
static uint8_t mmc1_ctrl;

/* keep track of selected banks for control register updates */
static uint8_t prg_bank;
static uint8_t chr_bank0;
static uint8_t chr_bank1;

static uint8_t* prg;
static uint8_t* chr;
static int n_prg_banks;
static int n_chr_banks;

/**
 *  Reload CHR Banks.
 */
static inline void switch_chr_bank ()
{
	uint8_t mode = (mmc1_ctrl & 0x10) == 0x10;
	if (mode) // 4KB mode
	{
		nes_ppu_switch_chr_rom_bank (chr_bank0, 0);
		nes_ppu_switch_chr_rom_bank (chr_bank1, 1);
	}
	else // 8KB mode
	{
		nes_ppu_switch_chr_rom_bank (chr_bank0 & 0x1E, 0);
		nes_ppu_switch_chr_rom_bank (chr_bank0 | 1, 1);
	}
}

/**
 *  Reload PRG Banks.
 */
static int prg_banks[2];
static inline void switch_prg_bank ()
{
	uint8_t mode = (mmc1_ctrl >> 2) & 3;
	switch (mode)
	{
	case 0: // switch 32 KB at $8000, ignoring low bit of bank number
	case 1:
		//nes_prg_load_bank (prg_bank & 0xE, 0);
		//nes_prg_load_bank (prg_bank | 1, 1);
		prg_banks[0] = prg_bank & 0xE;
		prg_banks[1] = prg_bank | 1;
		break;
	case 2: // fix first bank at $8000 and switch 16 KB bank at $C000
		//nes_prg_load_bank (0, 0);
		//nes_prg_load_bank (prg_bank, 1);
		prg_banks[0] = 0;
		prg_banks[1] = prg_bank;
		break;
	case 3: // fix last bank at $C000 and switch 16 KB bank at $8000
		//nes_prg_load_bank (-1, 1);
		//nes_prg_load_bank (prg_bank, 0);
		prg_banks[0] = prg_bank;
		prg_banks[1] = n_prg_banks - 1;
		break;
	}
}

/**
 *  Write to control register the value mmc1_sr.
 */
static inline void write_control (uint8_t v)
{
	mmc1_ctrl = v;
	// rectify banks to the new control value
	switch_prg_bank();
	switch_chr_bank();

	// switch mirroring
	switch (mmc1_ctrl & 3)
	{
	case 0: // one screen lower bank
		nes_ppu_set_mirroring (NES_PPU_MIRROR_SINGLE0);
		break;
	case 1: // one screen upper bank
		nes_ppu_set_mirroring (NES_PPU_MIRROR_SINGLE1);
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
static int write_prg_rom (uint16_t addr, uint8_t v)
{
	if (addr >= 0x8000)
	{
		if ((v & 0x80) == 0x80) // reset shift register
		{
			RESET_SR;
			write_control (mmc1_ctrl | 0x0C);
		}
		else
		{
			int done = (mmc1_sr & 1) == 1;
			SHIFT_SR (v); // shift LSB of v into shift register
			if (done) { // 5th write - write to correct register
				switch ((addr >> 13) & 3) // write to correct register
				{
				case 0: // Control $8000-9FFF
					write_control (mmc1_sr);
					break;
				case 1: // CHR Bank 0 $A000-$BFFF
					chr_bank0 = mmc1_sr & 0x1F;
					switch_chr_bank();
					break;
				case 2: // CHR Bank 1 $C000-$DFFF
					chr_bank1 = mmc1_sr & 0x1F;
					switch_chr_bank();
					break;
				case 3: // PRG Bank $E000-$FFFF
					prg_bank = mmc1_sr & 0xF;
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

/* read_prg_rom defines a read event handler for PRG ROM making sure value from the correct bank is returned */
static int read_prg_rom (uint16_t address, uint8_t* v)
{
	if (address >= 0x8000)
	{
		address &= 0x7FFF;
		int bank = (address >> 14) & 1; // by looking @ bit 14 we can see which bank it is
		int offset = address & (NES_PRG_ROM_BANK_SIZE - 1);
		*v = prg[prg_banks[bank] * NES_PRG_ROM_BANK_SIZE + offset];
		return 1;
	}
	return 0;
}

void nes_mmc1_load (int _n_prg_banks, uint8_t* _prg, int _n_chr_banks, uint8_t* _chr)
{
	prg_bank  = 0;
	chr_bank0 = 0;
	chr_bank1 = 0;

	chr = _chr;
	prg = _prg;
	n_prg_banks = _n_prg_banks;
	n_chr_banks = _n_chr_banks;

	nes_cpu_add_store_handler (&write_prg_rom);
	nes_cpu_add_read_handler (&read_prg_rom);

	RESET_SR;
	nes_prg_load_bank (-1, 1);
	prg_banks[0] = prg_bank;
	prg_banks[1] = n_prg_banks - 1;
}
