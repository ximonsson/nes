#include "nes/cpu.h"
#include "nes/ppu.h"
#include <stdio.h>


/* MMC1 shift register */
static uint8_t mmc1_sr;
#define RESET_SR mmc1_sr = 0x10
#define SHIFT_SR(v) mmc1_sr = (mmc1_sr >> 1) | ((v & 1) << 4)

/* MMC1 control register */
static uint8_t mmc1_ctrl;

/* keep track of selected banks for control register updates */
static uint8_t prg_bank;

static uint8_t* prg;
static uint8_t* chr;
static int n_prg_banks;
static int n_chr_banks;

/**
 *  Reload CHR Banks.
 */
static int chr_banks[2];
static inline void switch_chr_bank ()
{
	if ((mmc1_ctrl & 0x10) != 0x10) // 8KB mode
	{
		chr_banks[0] &= 0x1E;
		chr_banks[1] = chr_banks[0] | 1;
	}
}

/* define CHR bank size and macro to calculate address within CHR */
#define CHR_BANK_SIZE 0x1000
#define CHR(address) chr + chr_banks[address / CHR_BANK_SIZE] * CHR_BANK_SIZE + address % CHR_BANK_SIZE

/* write_chr_rom is used for the PPU to write to CHR */
static void write_chr_rom (uint16_t address, uint8_t v)
{
	*(CHR (address)) = v;
}

/* read_chr_rom is used for the PPU to read from CHR */
static uint8_t read_chr_rom (uint16_t address)
{
	return *(CHR (address));
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
		prg_banks[0] = prg_bank & 0xE;
		prg_banks[1] = prg_bank | 1;
		break;
	case 2: // fix first bank at $8000 and switch 16 KB bank at $C000
		prg_banks[0] = 0;
		prg_banks[1] = prg_bank;
		break;
	case 3: // fix last bank at $C000 and switch 16 KB bank at $8000
		prg_banks[0] = prg_bank;
		prg_banks[1] = n_prg_banks - 1;
		break;
	}
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
					chr_banks[0] = mmc1_sr & 0x1F;
					switch_chr_bank();
					break;
				case 2: // CHR Bank 1 $C000-$DFFF
					chr_banks[1] = mmc1_sr & 0x1F;
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

void nes_mmc1_load (int _n_prg_banks, uint8_t* _prg, int _n_chr_banks, uint8_t* _chr)
{
	chr = _chr;
	prg = _prg;
	n_prg_banks = _n_prg_banks;
	n_chr_banks = _n_chr_banks << 1;

	nes_cpu_add_store_handler (&write_prg_rom);
	nes_cpu_add_read_handler (&read_prg_rom);

	nes_ppu_set_chr_writer (write_chr_rom);
	nes_ppu_set_chr_read (read_chr_rom);

	RESET_SR;
	prg_bank = 0;
	prg_banks[0] = prg_bank;
	prg_banks[1] = n_prg_banks - 1;
	chr_banks[0] = chr_banks[1] = 0;
}
