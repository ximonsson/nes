#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/nes.h"
#include <string.h>

/* Registers */
static uint8_t mmc3_bank_select;
static uint8_t mmc3_bank_data;
static uint8_t mmc3_mirroring;
static uint8_t mmc3_prg_ram_protect;
static uint8_t mmc3_irq_latch;
static uint8_t mmc3_irq_disable;

static uint8_t mmc3_registers[8];
static uint8_t mmc3_counter = 0;

static uint8_t* prg;
static uint8_t* chr;

#define N_PRG_BANKS 4
static int n_prg_banks;
static int prg_banks[N_PRG_BANKS];

static void update_prg_banks ()
{
	if (mmc3_bank_select & 0x40)
	{
		// $8000 - $9FFF = -2
		// $A000 - $BFFF = R7
		// $C000 - $DFFF = R6
		// $E000 - $FFFF = -1
		prg_banks[0] = n_prg_banks - 2;
		prg_banks[1] = mmc3_registers[7];
		prg_banks[2] = mmc3_registers[6];
		prg_banks[3] = n_prg_banks - 1;
	}
	else
	{
		// $8000 - $9FFF = R6
		// $A000 - $BFFF = R7
		// $C000 - $DFFF = -2
		// $E000 - $FFFF = -1
		prg_banks[0] = mmc3_registers[6];
		prg_banks[1] = mmc3_registers[7];
		prg_banks[2] = n_prg_banks - 2;
		prg_banks[3] = n_prg_banks - 1;
	}
}

#define PRG_ROM_BANK_SIZE 0x2000
#define PRG(address) prg + prg_banks[address / PRG_ROM_BANK_SIZE] * PRG_ROM_BANK_SIZE + address % PRG_ROM_BANK_SIZE

int prg_read (uint16_t address, uint8_t* v)
{
	if (address >= 0x8000)
	{
		*v = *(PRG ((address & 0x7FFF)));
		return 1;
	}
	return 0;
}

#define N_CHR_BANKS 8
static int n_chr_banks;
static int chr_banks[N_CHR_BANKS];

static void update_chr_banks ()
{
	if (mmc3_bank_select & 0x80)
	{
		// $0000-$03FF 	R2
		// $0400-$07FF 	R3
		// $0800-$0BFF 	R4
		// $0C00-$0FFF 	R5
		// $1000-$13FF 	R0 AND $FE
		// $1400-$17FF 	R0 OR 1
		// $1800-$1BFF 	R1 AND $FE
		// $1C00-$1FFF 	R1 OR 1
		chr_banks[0] = mmc3_registers[2];
		chr_banks[1] = mmc3_registers[3];
		chr_banks[2] = mmc3_registers[4];
		chr_banks[3] = mmc3_registers[5];
		chr_banks[4] = mmc3_registers[0] & 0xFE;
		chr_banks[5] = mmc3_registers[0] | 1;
		chr_banks[6] = mmc3_registers[1] & 0xFE;
		chr_banks[7] = mmc3_registers[1] | 1;
	}
	else
	{
		// $0000-$03FF 	R0 AND $FE
		// $0400-$07FF 	R0 OR 1
		// $0800-$0BFF 	R1 AND $FE
		// $0C00-$0FFF 	R1 OR 1
		// $1000-$13FF 	R2
		// $1400-$17FF 	R3
		// $1800-$1BFF 	R4
		// $1C00-$1FFF 	R5
		chr_banks[0] = mmc3_registers[0] & 0xFE;
		chr_banks[1] = mmc3_registers[0] | 1;
		chr_banks[2] = mmc3_registers[1] & 0xFE;
		chr_banks[3] = mmc3_registers[1] | 1;
		chr_banks[4] = mmc3_registers[2];
		chr_banks[5] = mmc3_registers[3];
		chr_banks[6] = mmc3_registers[4];
		chr_banks[7] = mmc3_registers[5];
	}
}

#define CHR_BANK_SIZE 0x0400
#define CHR(adr) chr + chr_banks[adr / CHR_BANK_SIZE] * CHR_BANK_SIZE + adr % CHR_BANK_SIZE

uint8_t chr_read (uint16_t address)
{
	return *(CHR (address));
}

void chr_write (uint16_t address, uint8_t v)
{
	*(CHR (address)) = v;
}

static void write_bank_data (uint8_t v)
{
	mmc3_bank_data = v;
	// update register
	uint8_t reg = mmc3_bank_select & 7;
	mmc3_registers[reg] = v;
	// update banks
	//update_prg_banks();
	//update_chr_banks();
}

/* Event handler for writes to MMC3 registers */
static int write (uint16_t address, uint8_t value)
{
	if (address >= 0x8000)
	{
		int even = (address & 1) == 0;
		if (address < 0xA000) // $8000 - $9FFF
		{
			if (even) // even
				mmc3_bank_select = value;
			else // odd
				write_bank_data (value);

			// update banks
			update_prg_banks();
			update_chr_banks();
		}
		else if (address < 0xC000) // $A000 - $BFFF
		{
			if (even) // even
			{
				if (value & 1)
					nes_ppu_set_mirroring (NES_PPU_MIRROR_HORIZONTAL);
				else
					nes_ppu_set_mirroring (NES_PPU_MIRROR_VERTICAL);
			}
			else // odd - we do not implement this
				; // http://wiki.nesdev.com/w/index.php/MMC3#PRG_RAM_protect_.28.24A001-.24BFFF.2C_odd.29
		}
		else if (address < 0xE000) // $C000 - $DFFF
		{
			if (even) // even
				mmc3_irq_latch = value;
			else // odd
				mmc3_counter = 0; // reload IRQ
		}
		else // $E000 - $FFFF
		{
			mmc3_irq_disable = even;
			/*
			if (even) // even
				mmc3_irq_disable = 1; // disable IRQ
			else // odd
				mmc3_irq_disable = 0; // enable IRQ
			*/
		}
		return 1;
	}
	return 0;
}


void nes_mmc3_step ()
{
	if (mmc3_counter == 0)
		mmc3_counter = mmc3_irq_latch;
	else
	{
		mmc3_counter --;
		if (mmc3_counter == 0 && !mmc3_irq_disable)
			nes_cpu_signal (IRQ);
	}
}


void nes_mmc3_load (int n_prg_banks_, uint8_t* prg_, int n_chr_banks_, uint8_t* chr_)
{
	mmc3_bank_select     = 0;
	mmc3_bank_data       = 0;
	mmc3_mirroring       = 0;
	mmc3_prg_ram_protect = 0;
	mmc3_irq_latch       = 0;
	mmc3_irq_disable     = 0;
	mmc3_counter         = 0;

	prg = prg_;
	chr = chr_;
	n_prg_banks = n_prg_banks_;
	n_chr_banks = n_chr_banks_;

	prg_banks[0] = 0;
	prg_banks[1] = 1;
	prg_banks[2] = n_prg_banks - 2;
	prg_banks[3] = n_prg_banks - 1;

	memset (chr_banks, 0, N_CHR_BANKS * sizeof (int));

	nes_cpu_add_store_handler (write);
	nes_cpu_add_read_handler (prg_read);

	nes_ppu_set_chr_writer (chr_write);
	nes_ppu_set_chr_read (chr_read);
}
