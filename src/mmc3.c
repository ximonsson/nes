#include "nes/cpu.h"
#include "nes/ppu.h"
#include <string.h>

/* Registers */
static uint8_t mmc3_bank_select;
static uint8_t mmc3_bank_data;
static uint8_t mmc3_mirroring;
static uint8_t mmc3_prg_ram_protect;
static uint8_t mmc3_irq_latch;
static uint8_t mmc3_irq_disable;

static uint8_t mmc3_registers[8];
#define REG(r) mmc3_registers[r]

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
		prg_banks[0] = n_prg_banks - 2;   // $8000 - $9FFF = -2
		prg_banks[1] = REG(7);            // $A000 - $BFFF = R7
		prg_banks[2] = REG(6);            // $C000 - $DFFF = R6
		prg_banks[3] = n_prg_banks - 1;   // $E000 - $FFFF = -1
	}
	else
	{
		prg_banks[0] = REG(6);            // $8000 - $9FFF = R6
		prg_banks[1] = REG(7);            // $A000 - $BFFF = R7
		prg_banks[2] = n_prg_banks - 2;   // $C000 - $DFFF = -2
		prg_banks[3] = n_prg_banks - 1;   // $E000 - $FFFF = -1
	}
}

#define PRG_ROM_BANK_SIZE 0x2000
#define PRG(address) prg + prg_banks[address / PRG_ROM_BANK_SIZE] * PRG_ROM_BANK_SIZE + address % PRG_ROM_BANK_SIZE

static int prg_read (uint16_t address, uint8_t* v)
{
	if (address >= 0x8000)
	{
		//*v = *(PRG ((address & 0x7FFF)));
		address &= 0x7FFF;
		int bank = address / PRG_ROM_BANK_SIZE;
		int offset = address % PRG_ROM_BANK_SIZE;
		*v = prg[prg_banks[bank] * PRG_ROM_BANK_SIZE + offset];
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
		chr_banks[0] = REG(2);        // $0000-$03FF 	R2
		chr_banks[1] = REG(3);        // $0400-$07FF 	R3
		chr_banks[2] = REG(4);        // $0800-$0BFF 	R4
		chr_banks[3] = REG(5);        // $0C00-$0FFF 	R5
		chr_banks[4] = REG(0) & 0xFE; // $1000-$13FF 	R0 AND $FE
		chr_banks[5] = REG(0) | 1;    // $1400-$17FF 	R0 OR 1
		chr_banks[6] = REG(1) & 0xFE; // $1800-$1BFF 	R1 AND $FE
		chr_banks[7] = REG(1) | 1;    // $1C00-$1FFF 	R1 OR 1
	}
	else
	{
		chr_banks[0] = REG(0) & 0xFE; // $0000-$03FF 	R0 AND $FE
		chr_banks[1] = REG(0) | 1;    // $0400-$07FF 	R0 OR 1
		chr_banks[2] = REG(1) & 0xFE; // $0800-$0BFF 	R1 AND $FE
		chr_banks[3] = REG(1) | 1;    // $0C00-$0FFF 	R1 OR 1
		chr_banks[4] = REG(2);        // $1000-$13FF 	R2
		chr_banks[5] = REG(3);        // $1400-$17FF 	R3
		chr_banks[6] = REG(4);        // $1800-$1BFF 	R4
		chr_banks[7] = REG(5);        // $1C00-$1FFF 	R5
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
}

static void prg_write (uint16_t address, uint8_t value)
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
}

/* Event handler for writes to MMC3 registers */
static int write (uint16_t address, uint8_t value)
{
	if (address >= 0x8000)
	{
		prg_write (address, value);
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
	prg = prg_;
	chr = chr_;
	n_prg_banks = n_prg_banks_ << 1; // n banks passed are in 16KB but the mapper treats them as 8KB
	n_chr_banks = n_chr_banks_ << 1; // same as with PRG

	mmc3_bank_select     = 0;
	mmc3_bank_data       = 0;
	mmc3_mirroring       = 0;
	mmc3_prg_ram_protect = 0;
	mmc3_irq_latch       = 0;
	mmc3_irq_disable     = 0;
	mmc3_counter         = 0;

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
