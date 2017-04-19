#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/nes.h"

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
static int n_prg_banks;
static int n_chr_banks;

static void update_prg_banks ()
{
	if (mmc3_bank_select & 0x40)
	{
		// $8000 - $9FFF = -2
		// $A000 - $BFFF = R7
		// $C000 - $DFFF = R6
		// $E000 - $FFFF = -1
	}
	else
	{
		// $8000 - $9FFF = R6
		// $A000 - $BFFF = R7
		// $C000 - $DFFF = -2
		// $E000 - $FFFF = -1
	}
}

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
	}
}

static void write_bank_data (uint8_t v)
{
	mmc3_bank_data = v;
	// update register
	uint8_t reg = mmc3_bank_select & 7;
	mmc3_registers[reg] = v;
	// update banks
	update_prg_banks();
	update_chr_banks();
}

/* Event handler for writes to MMC3 registers */
static int write (uint16_t address, uint8_t value)
{
	if (address >= 0x8000)
	{
		if (address < 0xA000) // $8000 - $9FFF
		{
			if ((address & 1) == 0) // even
				mmc3_bank_select = value;
			else // odd
				write_bank_data (value);
		}
		else if (address < 0xC000) // $A000 - $BFFF
		{
			if ((address & 1) == 0) // even
			{
				if (value & 1)
					nes_ppu_set_mirroring (NES_PPU_MIRROR_HORIZONTAL);
				else
					nes_ppu_set_mirroring (NES_PPU_MIRROR_VERTICAL);
			}
			else // odd
				// // we do not implement this
				// http://wiki.nesdev.com/w/index.php/MMC3#PRG_RAM_protect_.28.24A001-.24BFFF.2C_odd.29
				;
		}
		else if (address < 0xE000) // $C000 - $DFFF
		{
			if ((address & 1) == 0) // even
				mmc3_irq_latch = value;
			else // odd
				mmc3_counter = 0; // reload IRQ
		}
		else // $E000 - $FFFF
		{
			if ((address & 1) == 0) // even
				mmc3_irq_disable = 1; // disable IRQ
			else // odd
				mmc3_irq_disable = 0; // enable IRQ
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
	nes_cpu_add_store_handler (write);
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
}
