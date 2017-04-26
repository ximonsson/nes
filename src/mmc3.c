#include "nes/cpu.h"
#include "nes/ppu.h"
#include "nes/nes.h"
#include <string.h>
#include <stdio.h>

/* Registers */
static uint8_t mmc3_bank_select;
static uint8_t mmc3_bank_data;
static uint8_t mmc3_mirroring;
static uint8_t mmc3_prg_ram_protect;
static uint8_t mmc3_irq_latch;
static uint8_t mmc3_irq_disable;

/* MMC3 bank registers for switching PRG and CHR banks. */
static uint8_t mmc3_registers[8];
#define REG(r) mmc3_registers[r]

/* IRQ counter */
static uint8_t mmc3_counter = 0;

/**
 * PRG
 */

#define N_PRG_BANKS 4
#define PRG_ROM_BANK_SIZE 0x2000

/* PRG ROM data */
static uint8_t* prg;

/* Number of PRG ROM banks (= len(PRG) / PRG_ROM_BANK_SIZE) */
static int n_prg_banks;

/* Offsets to currently loaded banks in PRG memory space ($8000 - $FFFF) */
static int prg_banks[N_PRG_BANKS];

/* update_prg_banks updates the offsets pointing to the correct banks for when reading. */
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

/* prg_read reads from the correct bank within PRG ROM */
static int prg_read (uint16_t address, uint8_t* v)
{
	if (address >= 0x8000)
	{
		address &= 0x7FFF;
		*v = prg[
			prg_banks[address / PRG_ROM_BANK_SIZE] // bank index
				* PRG_ROM_BANK_SIZE
			 + address % PRG_ROM_BANK_SIZE // offset
		];
		return 1;
	}
	return 0;
}

/**
 * CHR
 */
#define N_CHR_BANKS 8
#define CHR_BANK_SIZE 0x0400

/* CHR ROM data */
static uint8_t* chr;

/* Number of CHR ROM banks in total (= len(CHR) / CHR_ROM_BANK_SIZE) */
static int n_chr_banks;

/* Offsets pointing to correct bank when reading from CHR */
static int chr_banks[N_CHR_BANKS];

/* update_chr_banks updates offset depending on current status of registers */
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

#define CHR(adr) chr + chr_banks[adr / CHR_BANK_SIZE] * CHR_BANK_SIZE + adr % CHR_BANK_SIZE

uint8_t chr_read (uint16_t address)
{
	return *(CHR (address));
}

void chr_write (uint16_t address, uint8_t v)
{
	*(CHR (address)) = v;
}

/* write_bank_data writes to the MMC Bank Data register */
static void write_bank_data (uint8_t v)
{
	mmc3_bank_data = v;
	// update register
	uint8_t reg = mmc3_bank_select & 7;
	if (reg == 7 || reg == 6)
		// some games write a too large bank index to the register...
		// we need to therefor make sure it wraps
		// http://forums.nesdev.com/viewtopic.php?f=3&t=13569&p=160076&hilit=mmc3+prg#p160076
		v &= n_prg_banks - 1;
	REG(reg) = v;
}

/*
 * prg_write handles writes towards PRG ROM memory space ($8000 - $FFFF), and updates the registers
 * accordingly.
 * This function is registered as a RAM store handler.
 */
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
		if (even) // even - set PPU mirroring mode
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
		else // odd - reload IRQ counter
		{
			// printf ("MMC3: reloading when counter = %d\n", mmc3_counter);
			mmc3_counter = 0;
		}
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

static uint8_t ppu_a12 = 1;
/* nes_mmc3_step steps the MMC3 counter and signals an IRQ when counter is zero */
static void step ()
{
	// check loop V
	uint16_t v = nes_ppu_loopy_v ();
	uint8_t a12 = (v >> 12) & 1;

	// TODO this is not correct but it makes games playable
	// Another solution really needs to be found though, because ther are a lot of glitches
	// in all games
	if (a12 != ppu_a12) // change in PPU A12 ?
	{
		ppu_a12 = a12;
		// if (a12 == 0) // A12 is low, we are only interrested in 0 -> 1 events
			// return;
	}
	else
		return;

	if (mmc3_counter == 0) // reload IRQ
		mmc3_counter = mmc3_irq_latch;
	else
	{
		// decrement counter and trigger IRQ if counter == 0 and not disabled
		// printf ("MMC3: step counter (%d, IRQ disabled: %d)\n", mmc3_counter, mmc3_irq_disable);
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
	n_chr_banks = n_chr_banks_ << 3; // n chr banks passed are in 8KB but the mapper divides into 1KB banks.

	mmc3_bank_select     = 0;
	mmc3_bank_data       = 0;
	mmc3_mirroring       = 0;
	mmc3_prg_ram_protect = 0;
	mmc3_irq_latch       = 0;
	mmc3_irq_disable     = 0;
	mmc3_counter         = 0;

	memset (mmc3_registers, 0, 8 * sizeof (uint8_t));

	prg_banks[0] = 0;
	prg_banks[1] = 1;
	prg_banks[2] = n_prg_banks - 2;
	prg_banks[3] = n_prg_banks - 1;

	memset (chr_banks, 0, N_CHR_BANKS * sizeof (int));

	nes_cpu_add_store_handler (write);
	nes_cpu_add_read_handler (prg_read);

	nes_ppu_set_chr_writer (chr_write);
	nes_ppu_set_chr_read (chr_read);
	nes_step_callback (step);
}
